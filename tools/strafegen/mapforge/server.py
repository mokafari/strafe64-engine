#!/usr/bin/env python3
"""mapforge/server.py — local web backend for the browser map forge.

A stdlib-only HTTP server (no pip deps) that drives the strafegen generator live:
generate any kind in-memory, serialize it to a renderable scene, accept entity +
geometry edits, and export .bsp / .map / .pk3 into ``generated/``.

    python3 mapforge/server.py --port 8765 [--open]
    # then open http://127.0.0.1:8765

The server is STATELESS: /api/generate and /api/export are pure functions of
(kind, params, edits). Builds are deterministic per seed and millisecond-fast, so
ids stay stable across rebuilds and the frontend holds the only authoritative
scene. A single global lock serializes builds because cfg.THEME is module state
and BspWriter.write() mutates the course (so a course is never written twice).
"""
import argparse
import json
import os
import sys
import threading
import traceback
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
STRAFEGEN = os.path.dirname(HERE)            # tools/strafegen
STATIC = os.path.join(HERE, "static")
GENERATED = os.path.join(STRAFEGEN, "generated")
sys.path.insert(0, STRAFEGEN)                # so `import scene` / `import strafegen` resolve

import scene                                  # noqa: E402
import strafegen as sg                        # noqa: E402
import bsp_import                             # noqa: E402
import bsp_learn                              # noqa: E402

# directories the importer is allowed to read maps from (local dev tool):
# generated/ plus anything in $MAPFORGE_MAPS (colon-separated)
_MAP_ROOTS = [GENERATED] + [p for p in os.environ.get("MAPFORGE_MAPS", "").split(":") if p]

_BUILD_LOCK = threading.Lock()                # cfg.THEME is global; serialize builds


def _safe_aas(bsp_path):
    """compile_aas, but never let a broken/foreign bspc binary fail the export —
    degrade to no-bots (compile_aas itself only guards a non-zero exit, not an
    OSError from a binary that can't exec on this platform)."""
    try:
        return sg.compile_aas(bsp_path)
    except Exception as e:                     # noqa: BLE001
        sys.stderr.write(f"bspc unavailable ({e}); exporting without .aas\n")
        return None

_MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".css": "text/css; charset=utf-8", ".json": "application/json",
         ".svg": "image/svg+xml", ".ico": "image/x-icon"}


def _course_from(req):
    """Build a fresh course from a request body {kind, params, edits}."""
    kind = req.get("kind", "course")
    params = req.get("params", {}) or {}
    edits = req.get("edits", []) or []
    course = scene.build_kind(kind, params)
    return scene.apply_edits(course, edits)


def _list_maps():
    """Every .bsp / .pk3 found under the allowed map roots."""
    out = []
    for root in _MAP_ROOTS:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirs, files in os.walk(root):
            for fn in sorted(files):
                if fn.lower().endswith((".bsp", ".pk3")):
                    full = os.path.join(dirpath, fn)
                    out.append({"path": full, "label": os.path.relpath(full, root),
                                "root": root})
    return {"maps": out, "roots": _MAP_ROOTS}


def _import_map(req):
    """Decompile a .bsp (or a map inside a .pk3) into a renderable scene. The
    path must resolve under an allowed root."""
    path = os.path.realpath(req.get("path", ""))
    if not any(path.startswith(os.path.realpath(r) + os.sep) or path == os.path.realpath(r)
               for r in _MAP_ROOTS):
        raise ValueError("path is outside the allowed map directories")
    if not os.path.isfile(path):
        raise ValueError(f"no such file: {path}")
    if path.lower().endswith((".pk3", ".zip")):
        data, name = bsp_import._load_pk3(path, req.get("map"))
        return bsp_import.import_bsp(data, os.path.splitext(name)[0])
    return bsp_import.import_bsp(path, os.path.splitext(os.path.basename(path))[0])


def _analyze(req):
    """Decompile + learn from a chosen map, or the whole map corpus."""
    path = req.get("path")
    if path:
        path = os.path.realpath(path)
        if not any(path.startswith(os.path.realpath(r) + os.sep) for r in _MAP_ROOTS):
            raise ValueError("path is outside the allowed map directories")
        return bsp_learn.analyze_corpus([path])
    roots = [r for r in _MAP_ROOTS if os.path.isdir(r)]
    return bsp_learn.analyze_corpus(roots)


def _calibrate(req):
    """Suggest generator scale knobs to match a map / corpus's proportions."""
    path = req.get("path")
    if path:
        path = os.path.realpath(path)
        if not any(path.startswith(os.path.realpath(r) + os.sep) for r in _MAP_ROOTS):
            raise ValueError("path is outside the allowed map directories")
        return scene.calibrate([path])
    return scene.calibrate([r for r in _MAP_ROOTS if os.path.isdir(r)])


def _compose_export(req):
    """Bake placed section parts into a sealed map and write the formats."""
    placed = req.get("placed") or []
    opts = req.get("opts") or {}
    brushes = req.get("brushes") or []
    place_entities = req.get("entities") or []
    formats = set(req.get("formats") or ["bsp"])
    name = (req.get("name") or "").strip() or "strafe64_forged"
    name = "".join(c for c in name if c.isalnum() or c in "_-")
    os.makedirs(GENERATED, exist_ok=True)

    def build():
        return scene.compose(placed, opts, brushes, place_entities)
    sg.validate_spawns(build())                         # fail fast on a bad layout
    bsp_path = os.path.join(GENERATED, f"{name}.bsp")
    stats = sg.BspWriter(build()).write(bsp_path)       # fresh course (write mutates)
    sg.check_bsp(bsp_path)
    out = {"name": name, "bsp": os.path.relpath(bsp_path, STRAFEGEN), "stats": stats,
           "outputs": [os.path.relpath(bsp_path, STRAFEGEN)]}
    aas_path = None
    if "pk3" in formats:
        aas_path = _safe_aas(bsp_path)
        if aas_path:
            out["outputs"].append(os.path.relpath(aas_path, STRAFEGEN))
    if "map" in formats:
        map_path = os.path.join(GENERATED, f"{name}.map")
        sg.write_map(build(), map_path)
        out["map"] = os.path.relpath(map_path, STRAFEGEN)
        out["outputs"].append(out["map"])
    if "pk3" in formats:
        import strafegen_config as cfg
        pk3 = sg.write_pk3(bsp_path, name, GENERATED, aas_path, gfx_on=cfg.GFX)
        out["outputs"].append(os.path.relpath(pk3, STRAFEGEN))
        out["outputs"].append(os.path.relpath(sg.write_shared_assets(GENERATED, cfg.GFX),
                                              STRAFEGEN))
        out["bots"] = bool(aas_path)
    return out


def _export(req):
    """Build + edit + validate + write the requested formats into generated/."""
    kind = req.get("kind", "course")
    params = req.get("params", {}) or {}
    formats = set(req.get("formats") or ["bsp"])
    name = (req.get("name") or "").strip() or scene.default_name(kind, params)
    name = "".join(c for c in name if c.isalnum() or c in "_-")  # filesystem-safe
    os.makedirs(GENERATED, exist_ok=True)

    course = _course_from(req)
    sg.validate_spawns(course)                # raises ValueError on a stuck spawn
    bsp_path = os.path.join(GENERATED, f"{name}.bsp")
    stats = sg.BspWriter(course).write(bsp_path)   # NB: mutates `course`
    sg.check_bsp(bsp_path)
    out = {"name": name, "bsp": os.path.relpath(bsp_path, STRAFEGEN), "stats": stats,
           "outputs": []}
    out["outputs"].append(out["bsp"])

    aas_path = None
    if "pk3" in formats:
        aas_path = _safe_aas(bsp_path)   # None if no bspc binary
        if aas_path:
            out["aas"] = os.path.relpath(aas_path, STRAFEGEN)
            out["outputs"].append(out["aas"])
    if "map" in formats:
        # write_map needs a course with its original (un-fogged) solids — rebuild
        map_course = scene.apply_edits(scene.build_kind(kind, params),
                                       req.get("edits", []) or [])
        map_path = os.path.join(GENERATED, f"{name}.map")
        sg.write_map(map_course, map_path)
        out["map"] = os.path.relpath(map_path, STRAFEGEN)
        out["outputs"].append(out["map"])
    if "pk3" in formats:
        import strafegen_config as cfg
        pk3 = sg.write_pk3(bsp_path, name, GENERATED, aas_path, gfx_on=cfg.GFX)
        out["pk3"] = os.path.relpath(pk3, STRAFEGEN)
        out["outputs"].append(out["pk3"])
        shared = sg.write_shared_assets(GENERATED, cfg.GFX)
        out["shared"] = os.path.relpath(shared, STRAFEGEN)
        out["outputs"].append(out["shared"])
        out["bots"] = bool(aas_path)
    return out


class Handler(BaseHTTPRequestHandler):
    server_version = "MapForge/1.0"

    def log_message(self, fmt, *args):       # quieter console
        sys.stderr.write("  %s - %s\n" % (self.address_string(), fmt % args))

    # ---- response helpers ----
    def _json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _err(self, msg, code=400):
        self._json({"error": str(msg)}, code)

    def _read_body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}") if n else {}

    def _serve_static(self, path):
        if path in ("", "/"):
            path = "/index.html"
        rel = path.lstrip("/")
        full = os.path.normpath(os.path.join(STATIC, rel))
        if not full.startswith(STATIC) or not os.path.isfile(full):
            self._err("not found", 404)
            return
        ext = os.path.splitext(full)[1]
        with open(full, "rb") as fh:
            data = fh.read()
        self.send_response(200)
        self.send_header("Content-Type", _MIME.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    # ---- routes ----
    def do_GET(self):
        if self.path == "/api/meta":
            self._json(scene.meta())
            return
        if self.path == "/api/parts":
            with _BUILD_LOCK:
                self._json(scene.parts_catalog())
            return
        if self.path == "/api/maps":
            self._json(_list_maps())
            return
        self._serve_static(self.path)

    def do_POST(self):
        try:
            req = self._read_body()
        except (ValueError, json.JSONDecodeError) as e:
            self._err(f"bad JSON: {e}", 400)
            return
        try:
            if self.path == "/api/generate":
                with _BUILD_LOCK:
                    s = scene.serialize(_course_from(req))
                self._json(s)
            elif self.path == "/api/export":
                with _BUILD_LOCK:
                    res = _export(req)
                self._json(res)
            elif self.path == "/api/compose_export":
                with _BUILD_LOCK:
                    res = _compose_export(req)
                self._json(res)
            elif self.path == "/api/import_bsp":
                with _BUILD_LOCK:
                    res = _import_map(req)
                self._json(res)
            elif self.path == "/api/analyze":
                with _BUILD_LOCK:
                    res = _analyze(req)
                self._json(res)
            elif self.path == "/api/calibrate":
                with _BUILD_LOCK:
                    res = _calibrate(req)
                self._json(res)
            else:
                self._err("not found", 404)
        except ValueError as e:
            # build / edit / validate_spawns rejections — actionable for the user
            self._err(str(e), 422)
        except Exception as e:                # noqa: BLE001  surface generator faults
            traceback.print_exc()
            self._err(f"{type(e).__name__}: {e}", 500)


def main():
    ap = argparse.ArgumentParser(description="MapForge browser backend")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--open", action="store_true", help="open a browser tab")
    args = ap.parse_args()
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://{args.host}:{args.port}"
    print(f"MapForge serving on {url}  (generated/ = {GENERATED})")
    print("  Ctrl-C to stop")
    if args.open:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
        srv.shutdown()


if __name__ == "__main__":
    main()
