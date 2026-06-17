#!/usr/bin/env python3
"""STRAFE 64 engine — MCP server.

Exposes the engine control surface (engine_api.EngineSession) as MCP tools so
Claude / any LLM can hook directly into a running STRAFE 64 engine: launch it,
read and set cvars, tune the movement model, change maps, run console commands,
take screenshots, read bot telemetry, and rebuild the native mod.

Zero third-party dependencies: speaks newline-delimited JSON-RPC 2.0 over stdio,
which is exactly what Claude Code's stdio MCP transport expects.

Register it (project-local .mcp.json already does this):

    {
      "mcpServers": {
        "strafe64-engine": {
          "command": "python3",
          "args": ["tools/strafegen/engine_mcp.py"]
        }
      }
    }

One engine session is held per server process. `engine_launch` starts it,
`engine_shutdown` stops it; the process also stops the engine on exit.
"""

from __future__ import annotations

import atexit
import base64
import json
import sys
import traceback

import engine_api
from engine_api import (EngineSession, EngineError, MOVEMENT_CVARS, EFFECT_CVARS,
                        live_home, LIVE_BASE)

PROTOCOL_VERSION = "2024-11-05"
SERVER_INFO = {"name": "strafe64-engine", "version": "1.0.0"}

# Surfaced to the model on connect (MCP InitializeResult.instructions) so it gets
# the map of tools + workflows up front instead of guessing across 49 tools.
INSTRUCTIONS = """Drive a live Quake3-based engine (STRAFE 64) for game development.

GET GOING: engine_open (persistent, survives restarts; `name` for several at once)
or engine_launch (session-scoped). Omit `map` to boot the sandbox. Then
engine_look for instant orientation (positions + a screenshot). engine_help
re-fetches this; engine_selftest verifies the whole toolchain.

TOOL GROUPS
- Lifecycle: engine_open/attach/close/list, engine_launch/shutdown, engine_status,
  engine_processes/kill_orphans (list & clean up leftover windows).
- Observe: engine_look (one-call orient), engine_state (positions, speed, airborne,
  walljump/airjump counts), engine_map_bounds (level extent).
- Tune (LIVE cvars): engine_movement_get/set, engine_effects_get/set,
  engine_get/set_cvar, engine_save/load/list_preset, engine_compare (sweep values
  → collage or bar chart).
- Tune (SOURCE, rebuild): engine_source_constants lists ALL ~40 movement constants
  (live vs rebuild-only); engine_set_source_constant edits a rebuild-only one in
  place; engine_rebuild applies it. Full movement model = live cvars + source.
- Measure/judge: engine_measure (field over time), engine_playtest_report (bot fitness).
- Camera — POINT & SHOOT, never guess coordinates: engine_frame(subject),
  engine_orbit(subject), engine_camera, engine_dolly. `subject` = a bot/player
  name, [x,y,z], or 'action' (the players' centroid); angles are computed for you.
- Capture: engine_screenshot, engine_capture_clip/angles, engine_capture_event
  (console regex) / engine_capture_state (measured condition), engine_clip_video,
  engine_cinematic (bullet-time), engine_record/play/render_demo.
- Interact/author: engine_input (drive the player), engine_spawn/clear (live
  entities), engine_audition_model, engine_generate_map, engine_reload.

RECIPES
- Tune live feel: engine_compare("pm_strafeAccelerate",[70,140,220],mode="movement")
  → pick a value → engine_movement_set; checkpoint with engine_save_preset.
- Tune a non-cvar constant: engine_source_constants → engine_set_source_constant(
  name, value, rebuild=true).
- Film bots: engine_launch(mode="client", bots=4) → engine_spectate →
  engine_clip_video or engine_cinematic("action").
- Audit a course: engine_generate_map(seed=...) → engine_playtest_report.
- Dress a scene: engine_spawn(classname, keys) → engine_frame → tweak → engine_clear.
- Catch a moment: engine_capture_state(field="speed", op=">", value=400) or
  engine_capture_event(pattern="was killed").

Screenshots/clips come back as images you can see. Visual tools need mode="client".
"""

_session: EngineSession | None = None
_current_name: str = "default"   # which persistent instance this process targets


# --- tool handlers ----------------------------------------------------------

def _require():
    """Return the active session, transparently reattaching to the current
    persistent engine (opened in a prior MCP process) if we have no session."""
    global _session
    if _session is not None and _session.alive:
        return _session
    try:
        _session = EngineSession.attach(live_home(_current_name))
        return _session
    except EngineError:
        pass
    raise EngineError(f"no engine running for '{_current_name}' — call engine_launch "
                      "(session) or engine_open (persistent)")


def t_help(**_):
    """Re-fetch the grouped tool map + workflow recipes on demand (same text the
    server sends on connect, in case it scrolled out of context)."""
    return {"_mcp_content": [{"type": "text", "text": INSTRUCTIONS}]}


def t_status(**_):
    if _session is not None:
        return {"running": _session.alive, "name": _session.name or _current_name,
                "mode": _session.mode, "map": _session.map, "port": _session.port,
                "home": str(_session.home), "pid": _session._pid,
                "persistent": _session.detached, "attached": _session._attached}
    st = EngineSession.peek(live_home(_current_name))
    if st:
        return {"running": st.get("alive", False), "persistent": True,
                "in_process": False, **st}
    return {"running": False, "name": _current_name}


def t_list(**_):
    """List all persistent engine instances (across sessions) under LIVE_BASE."""
    return {"current": _current_name, "base": LIVE_BASE,
            "engines": EngineSession.list_live()}


def t_launch(mode="dedicated", map=None, fullscreen=False, bots=0, extra=None,
             width=None, height=None):
    global _session
    if _session is not None and _session.alive:
        raise EngineError("an engine is already running — engine_shutdown first")
    _session = EngineSession(mode=mode, map=map, fullscreen=bool(fullscreen),
                             width=width, height=height, bots=int(bots or 0),
                             extra=list(extra or []))
    _session.start()
    return t_status()


def t_open(mode="dedicated", map=None, fullscreen=False, bots=0, extra=None,
           name="default", width=None, height=None):
    """Open a *persistent* engine that keeps running after this MCP process
    exits; reattach to it later from any process. `name` lets multiple sessions
    each run their own engine (isolated home + port)."""
    global _session, _current_name
    if _session is not None and _session.alive and (_session.name or _current_name) == name:
        raise EngineError(f"engine '{name}' is already running here — engine_close first")
    st = EngineSession.peek(live_home(name))
    if st and st.get("alive"):
        raise EngineError(f"a persistent engine named '{name}' (pid {st['pid']}) is already "
                          "running — engine_attach or engine_close it")
    _current_name = name
    _session = EngineSession(mode=mode, map=map, fullscreen=bool(fullscreen),
                             width=width, height=height, bots=int(bots or 0),
                             extra=list(extra or []),
                             home=live_home(name), detached=True, name=name)
    _session.start()
    return t_status()


def t_attach(name="default"):
    global _session, _current_name
    _session = EngineSession.attach(live_home(name))
    _current_name = name
    return t_status()


def t_close(name=None):
    global _session, _current_name
    target = name or _current_name
    if _session is not None and (_session.name or _current_name) == target:
        _session.close()
        _session = None
        return {"running": False, "name": target}
    try:
        s = EngineSession.attach(live_home(target))
    except EngineError:
        return {"running": False, "name": target}
    s.close()
    return {"running": False, "name": target}


def t_shutdown(**_):
    global _session
    if _session is None:
        return {"running": False}
    _session.stop()   # a persistent engine keeps running; use engine_close to quit it
    _session = None
    return {"running": False}


def t_command(command):
    _require().command(command)
    return {"ok": True, "command": command}


def t_console(command, timeout=6.0):
    return {"output": _require().console(command, timeout=float(timeout))}


def t_get_cvar(name):
    return {"name": name, "value": _require().get_cvar(name)}


def t_set_cvar(name, value, confirm=True):
    got = _require().set_cvar(name, value, confirm=bool(confirm))
    return {"name": name, "requested": value, "value": got}


def t_movement_get(**_):
    return _require().get_movement()


def t_movement_set(name, value, clamp=True):
    got = _require().set_movement(name, value, clamp=bool(clamp))
    return {"name": name, "requested": value, "value": got,
            "range": MOVEMENT_CVARS[name]["range"]}


def t_map(name, devmap=False):
    _require().change_map(name, devmap=bool(devmap))
    return {"map": name}


def _shot_content(note: str = ""):
    """Take a screenshot and return MCP content: a note + the image itself, so
    Claude can actually see the frame and adapt."""
    path, data = _require().screenshot_bytes(jpeg=True)
    items = [{"type": "text", "text": (note + " " if note else "") + f"(screenshot: {path})"}]
    if data:
        items.append({"type": "image",
                      "data": base64.b64encode(data).decode("ascii"),
                      "mimeType": "image/jpeg"})
    else:
        items.append({"type": "text", "text": "WARNING: screenshot file not found "
                      "(client mode + a loaded map required)"})
    return {"_mcp_content": items}


def t_screenshot(jpeg=True):
    return _shot_content("frame captured")


def t_camera(x, y, z, yaw=0, pitch=None, look_at=None, noclip=False, shoot=True):
    eng = _require()
    if noclip:
        eng.noclip()
    if look_at is not None:
        info = eng.look_at((x, y, z), look_at)
        note = f"cam @ ({x},{y},{z}) looking at {look_at} (yaw {info['yaw']}, pitch {info['pitch']})"
    else:
        eng.setviewpos(x, y, z, yaw, pitch)
        note = f"cam @ ({x},{y},{z}) yaw {yaw}" + (f" pitch {pitch}" if pitch is not None else "")
    if shoot:
        return _shot_content(note)
    return {"viewpos": [x, y, z, yaw, pitch], "look_at": look_at}


def t_dolly(path, frames=None, interval=0.25, look=True, fps=None, title=None):
    res = _require().dolly(path, frames=frames, interval=float(interval),
                           look=bool(look), fps=int(fps) if fps else None, title=title)
    note = f"dolly: {len(res['frames'])} frames over {len(path)} waypoints"
    if res.get("video"):
        note += f" | video: {res['video']}"
    return {"_mcp_content": _image_content(res["collage"], note)}


def t_input(actions, play=False, weapon=None, shoot=False):
    """Drive the live player. `actions` is a list of steps, each one of:
    {"move":[fwd,side], "secs":s} | {"turn":[yaw,pitch]} | {"attack":secs} |
    {"jump":n} | {"use":item} | {"wait":secs}."""
    eng = _require()
    if play:
        eng.play_mode(weapon=int(weapon) if weapon is not None else None)
    done = eng.run_actions(actions)
    if shoot:
        return _shot_content("after: " + ", ".join(done))
    return {"did": done}


def t_compare(cvar, values, mode="visual", subject=None, actions=None,
              seconds=2.5, dist=240, trials=3, title=None):
    res = _require().compare(cvar, values, mode=mode, subject=subject,
                             actions=actions, seconds=float(seconds), dist=dist,
                             trials=int(trials), title=title)
    img = res.get("collage") or res.get("chart")
    note = (f"{cvar} {mode} sweep over {res.get('values') or [r['value'] for r in res.get('results',[])]}")
    c = _image_content(img, note)
    out = {"_mcp_content": c}
    if res.get("results"):
        out["_mcp_content"].append({"type": "text", "text": json.dumps(res["results"])})
    return out


def t_capture_event(pattern, frames=9, interval=0.3, timeout=30, mode="after",
                    pre=4, post=5, fps=None, title=None):
    eng = _require()
    if mode == "window":
        res = eng.record_window(pattern, pre=int(pre), post=int(post),
                                interval=float(interval), timeout=float(timeout),
                                fps=int(fps) if fps else None, title=title)
        note = f"window around '{pattern}': fired={res['fired']} pre={res['pre']} post={res['post']}"
    else:
        res = eng.capture_on_event(pattern, frames=int(frames), interval=float(interval),
                                   timeout=float(timeout), fps=int(fps) if fps else None,
                                   title=title)
        note = f"on '{pattern}': fired={res['fired']} | {res.get('event') or ''}"
    if res.get("video"):
        note += f" | video: {res['video']}"
    return {"_mcp_content": _image_content(res["collage"], note)}


def t_audition_model(model, headmodel=None, weapon=None, shoot=True):
    info = _require().audition_model(model, headmodel=headmodel,
                                     weapon=int(weapon) if weapon is not None else None)
    if shoot:
        return _shot_content(f"auditioning model {model}")
    return info


def t_give(what="all"):
    _require().give(what)
    return {"gave": what}


def t_spawn(classname, keys=None, shoot=False):
    eng = _require()
    eng.spawn(classname, **(keys or {}))
    if shoot:
        # it spawned ~96u in front of the view, so the current frame shows it
        return _shot_content(f"spawned {classname}")
    return {"spawned": classname, "keys": keys or {}}


def t_clear(classname=None):
    return _require().clear_spawns(classname=classname)


def _image_content(path, note=""):
    items = [{"type": "text", "text": (note + " " if note else "") + f"({path})"}]
    if path and __import__("os").path.exists(path):
        data = open(path, "rb").read()
        items.append({"type": "image",
                      "data": base64.b64encode(data).decode("ascii"),
                      "mimeType": "image/jpeg"})
    return items


def t_state(**_):
    """Where everything is — so shots can be framed by name, not coordinates."""
    return _require().state()


def t_map_bounds(**_):
    b = _require().map_bounds()
    return b or {"bounds": None, "note": "pk3-only map — no loose .bsp to read bounds from"}


def t_look(**_):
    """One-call orientation: status + who's present + an auto-framed wide shot."""
    eng = _require()
    ov = eng.overview()
    lines = [f"map={ov.get('map')}  mode={ov.get('mode')}  name={ov.get('name')}  alive={ov.get('alive')}"]
    if ov.get("clients"):
        lines.append("present:")
        for c in ov["clients"]:
            tag = "spec" if c.get("spectator") else ("bot" if c.get("bot") else "player")
            lines.append(f"  #{c['num']} {c.get('name')} [{tag}] hp={c.get('health')} "
                         f"spd={c.get('speed')} @ {c.get('origin')}")
    elif ov.get("state_error"):
        lines.append(f"(no live positions: {ov['state_error']})")
    content = [{"type": "text", "text": "ENGINE LOOK\n" + "\n".join(lines)}]
    if eng.mode == "client":
        try:
            eng.camera_mode()
            try:
                eng.establishing_shot()
            except EngineError:
                pass
            path, data = eng.screenshot_bytes()
            if data:
                content.append({"type": "image",
                                "data": base64.b64encode(data).decode("ascii"),
                                "mimeType": "image/jpeg"})
        except EngineError:
            pass
    return {"_mcp_content": content}


def t_capture_state(subject="any", field="speed", op=">", value=0, frames=6,
                    interval=0.3, timeout=20, fps=None, title=None):
    spec = {"subject": subject, "field": field, "op": op, "value": value}
    res = _require().capture_on_state(spec, frames=int(frames), interval=float(interval),
                                      timeout=float(timeout), fps=int(fps) if fps else None,
                                      title=title)
    m = res.get("matched") or {}
    note = (f"fired={res['fired']}"
            + (f" — {m.get('name')} {field}={m.get(field)}" if m else " (timed out)"))
    return {"_mcp_content": _image_content(res["collage"], note)}


def t_measure(subject="self", seconds=4.0, hz=10, field="speed"):
    return _require().measure(subject=subject, seconds=float(seconds), hz=int(hz), field=field)


def t_frame(subject=None, dist=240, height=110, yaw=35, mode="subject",
            radius=None, shoot=True):
    """Point & shoot: auto-place the camera to frame what you name. subject =
    a bot/player name or number, [x,y,z], or 'action' (the players' centroid).
    mode='wide' ignores subject and frames the whole scene (pass `radius` to pull
    back over an empty course)."""
    eng = _require()
    eng.camera_mode()
    if mode == "wide":
        info = eng.establishing_shot(radius=float(radius) if radius else None)
        note = f"wide shot (target {info['target']}, span {info['span']})"
    else:
        info = eng.frame(subject, dist=dist, height=height, yaw=yaw)
        note = f"framed {subject!r} @ dist {info['dist']} (target {info['target']})"
    if shoot:
        c = _shot_content(note); c["_note"] = info
        return c
    return info


def t_orbit(subject=None, angles=None, dist=260, height=120, title=None):
    res = _require()
    res.camera_mode()
    out = res.orbit_shot(subject, angles=angles, dist=dist, height=height, title=title)
    return {"_mcp_content": _image_content(out["collage"],
            f"orbit of {subject!r} (target {out['target']})")}


def t_spectate(target=None, third_person=True, range=130, angle=25, shoot=False):
    info = _require().spectate(target=target, third_person=bool(third_person),
                               range=range, angle=angle)
    if shoot:
        return _shot_content(f"spectating {info['following']}")
    return info


def t_record_demo(seconds=6.0, name="clip"):
    return _require().clip_demo(seconds=float(seconds), name=name)


def t_play_demo(name):
    return _require().play_demo(name)


def t_clip_video(seconds=5.0, name="clip"):
    return _require().clip_video(seconds=float(seconds), name=name)


def t_cinematic(subject=None, seconds=5.0, slowmo=0.3, orbit_deg=140, name="cinematic"):
    return _require().cinematic_clip(subject=subject, seconds=float(seconds),
                                     slowmo=float(slowmo), orbit_deg=float(orbit_deg), name=name)


def t_render_demo(name, fps=25):
    return _require().render_demo(name, fps=int(fps))


def t_capture_clip(frames=9, interval=0.4, cols=3, title=None, fps=None, clean=True):
    res = _require().clip_collage(frames=int(frames), interval=float(interval),
                                  cols=int(cols), title=title,
                                  fps=int(fps) if fps else None, clean=bool(clean))
    note = f"clip collage: {len(res['frames'])} frames"
    if res.get("video"):
        note += f" | video: {res['video']}"
    return {"_mcp_content": _image_content(res["collage"], note)}


def t_capture_angles(angles=None, range=130, freeze=True, title=None, clean=True):
    res = _require().angles_collage(angles=angles, range=range, freeze=bool(freeze),
                                    title=title, clean=bool(clean))
    return {"_mcp_content": _image_content(
        res["collage"], f"multi-angle collage: {res['angles']}")}


def t_save_preset(name, cvars=None):
    return _require().save_preset(name, cvars=cvars)


def t_load_preset(name):
    return _require().load_preset(name)


def t_list_presets(**_):
    return {"presets": _require().list_presets()}


def t_effects_get(**_):
    return _require().get_effects()


def t_effects_set(name, value, clamp=True, restart=None, shoot=False):
    got = _require().set_effect(name, value, clamp=bool(clamp), restart=restart)
    if shoot:
        c = _shot_content(f"{name} = {got}")
        c["_note"] = {"name": name, "value": got}
        return c
    return {"name": name, "requested": value, "value": got,
            "range": EFFECT_CVARS[name]["range"]}


def t_telemetry(tag=None, limit=50):
    return {"records": _require().telemetry(tag=tag, limit=int(limit))}


def t_source_constants(**_):
    return {"constants": engine_api.source_constants()}


def t_config_overrides(**_):
    return {"overrides": engine_api.config_overrides()}


def t_set_source_constant(name, value, rebuild=False):
    return engine_api.set_source_constant(name, value, rebuild=bool(rebuild))


def t_rebuild(clean=False):
    # source tier: rebuild the native mod for compile-time constant changes
    return engine_api.rebuild(clean=bool(clean))


def t_generate_map(seed=None, kind="course", difficulty=None, length=None,
                   load=True, devmap=True, shoot=False):
    res = _require().generate_map(seed=seed, kind=kind, difficulty=difficulty,
                                  length=length, load=bool(load), devmap=bool(devmap))
    if shoot and res.get("loaded"):
        import time as _t; _t.sleep(2)
        c = _shot_content(f"generated + loaded {res['name']} ({kind})")
        c["_note"] = res
        return c
    return res


def t_reload(visual=True, sound=False):
    _require().reload(visual=bool(visual), sound=bool(sound))
    return {"reloaded": {"visual": bool(visual), "sound": bool(sound)}}


def t_playtest_report(map=None, bots=5, seconds=30, skill=4):
    # spins its own dedicated server, so it doesn't touch the current session
    return engine_api.playtest_report(map=map, bots=int(bots), seconds=float(seconds),
                                      skill=int(skill))


def t_selftest(**_):
    # spins its own client engine; verifies the whole API surface
    return engine_api.selftest()


def t_processes(**_):
    return {"processes": engine_api.list_processes()}


def t_kill_orphans(**_):
    keep = _session._pid if (_session is not None and _session.alive) else None
    return engine_api.kill_orphans(keep_pid=keep)


# --- tool registry ----------------------------------------------------------

TOOLS = [
    {
        "name": "engine_help",
        "description": "Re-fetch the grouped tool map + workflow recipes for this engine API (handy if the connect-time instructions scrolled out of context).",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_help,
    },
    {
        "name": "engine_status",
        "description": "Report whether a STRAFE 64 engine session is running, its mode, current map, and pid.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_status,
    },
    {
        "name": "engine_launch",
        "description": ("Launch a STRAFE 64 engine instance to drive. mode='dedicated' is a fast "
                        "headless server (best for cvar/telemetry iteration); mode='client' opens "
                        "the windowed game (needed for screenshots/visual work). Optionally load a "
                        "map and add bots. One session at a time."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "mode": {"type": "string", "enum": ["dedicated", "client"], "default": "dedicated"},
                "map": {"type": "string", "description": "map to load; omit to boot the default playtest "
                        "sandbox (dojo_arena), or 'none' for the menu"},
                "fullscreen": {"type": "boolean", "default": False, "description": "client mode only"},
                "bots": {"type": "integer", "default": 0, "description": "number of bots to add"},
                "width": {"type": "integer", "default": 640, "description": "client window width (px); 0 = desktop res"},
                "height": {"type": "integer", "default": 360, "description": "client window height (px); 0 = desktop res"},
                "extra": {"type": "array", "items": {"type": "string"},
                          "description": "extra engine +args, e.g. ['+set','g_gravity','800']"},
            },
        },
        "handler": t_launch,
    },
    {
        "name": "engine_shutdown",
        "description": ("Release the current session. A session-scoped engine (engine_launch) is quit; "
                        "a persistent engine (engine_open) keeps running — use engine_close to quit it."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_shutdown,
    },
    {
        "name": "engine_open",
        "description": ("Open a PERSISTENT engine that keeps running after this session/MCP process "
                        "exits, so you can leave it up and reattach across sessions. Same options as "
                        "engine_launch. Pass `name` to run multiple engines at once (each gets its own "
                        "home + port) — e.g. one per session. Use engine_attach to reconnect, "
                        "engine_close to quit, engine_list to see them all."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "mode": {"type": "string", "enum": ["dedicated", "client"], "default": "dedicated"},
                "map": {"type": "string", "description": "map to load; omit for the default sandbox (dojo_arena)"},
                "fullscreen": {"type": "boolean", "default": False},
                "bots": {"type": "integer", "default": 0},
                "width": {"type": "integer", "default": 640, "description": "client window width (px); 0 = desktop res"},
                "height": {"type": "integer", "default": 360, "description": "client window height (px); 0 = desktop res"},
                "extra": {"type": "array", "items": {"type": "string"}},
                "name": {"type": "string", "default": "default",
                         "description": "instance name — distinct engines run side by side"},
            },
        },
        "handler": t_open,
    },
    {
        "name": "engine_attach",
        "description": ("Reconnect to a persistent engine opened earlier (by engine_open in this or a "
                        "prior session) and make it this process's current target. Pass `name` to pick "
                        "which instance. Tools also auto-attach to the current instance on demand."),
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string", "default": "default"}},
        },
        "handler": t_attach,
    },
    {
        "name": "engine_close",
        "description": "Quit a persistent engine for good and remove its state file (defaults to the current instance; pass `name` to close another).",
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string"}},
        },
        "handler": t_close,
    },
    {
        "name": "engine_list",
        "description": "List all persistent engine instances running across sessions (name, mode, map, port, pid, alive).",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_list,
    },
    {
        "name": "engine_command",
        "description": ("Send a raw console command to the running engine (fire-and-forget, no reply "
                        "captured). Use for actions: map, addbot, kick, give, noclip, exec <cfg>, etc."),
        "inputSchema": {
            "type": "object",
            "properties": {"command": {"type": "string"}},
            "required": ["command"],
        },
        "handler": t_command,
    },
    {
        "name": "engine_console",
        "description": ("Run a console command and return whatever the engine prints in response. "
                        "Use for queries: 'cvarlist pm_', 'serverinfo', 'print <cvar>', 'condump'."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string"},
                "timeout": {"type": "number", "default": 6.0},
            },
            "required": ["command"],
        },
        "handler": t_console,
    },
    {
        "name": "engine_get_cvar",
        "description": "Read a single cvar's current value from the running engine.",
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string"}},
            "required": ["name"],
        },
        "handler": t_get_cvar,
    },
    {
        "name": "engine_set_cvar",
        "description": "Set any cvar live and (by default) read it back to confirm.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "value": {"description": "new value (string or number)"},
                "confirm": {"type": "boolean", "default": True},
            },
            "required": ["name", "value"],
        },
        "handler": t_set_cvar,
    },
    {
        "name": "engine_movement_get",
        "description": ("Snapshot the live-tunable STRAFE 64 movement model — the cvars mirrored into "
                        "the pmove physics each frame (air-strafe accel, air control, wish-speed clamp, "
                        "gravity, etc.) with their current values, valid ranges, and docs."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_movement_get,
    },
    {
        "name": "engine_movement_set",
        "description": ("Tune one live movement cvar (value clamped to its valid range by default). "
                        "Changes movement feel immediately, no rebuild. Use engine_movement_get for the "
                        "list of tunables and ranges. Compile-time constants need engine_rebuild instead."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "enum": sorted(MOVEMENT_CVARS)},
                "value": {"type": "number"},
                "clamp": {"type": "boolean", "default": True},
            },
            "required": ["name", "value"],
        },
        "handler": t_movement_set,
    },
    {
        "name": "engine_map",
        "description": "Change to a map on the running engine (devmap=true enables cheats/dev mode).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "devmap": {"type": "boolean", "default": False},
            },
            "required": ["name"],
        },
        "handler": t_map,
    },
    {
        "name": "engine_screenshot",
        "description": ("Take a screenshot (client mode) and return the actual image so you can SEE "
                        "the current frame and adapt the game live. The core of the audition loop."),
        "inputSchema": {
            "type": "object",
            "properties": {"jpeg": {"type": "boolean", "default": True}},
        },
        "handler": t_screenshot,
    },
    {
        "name": "engine_audition_model",
        "description": ("Audition a player/character model: swap to it, force it, respawn so it loads, "
                        "frame it in third person, and return a screenshot. Optionally give+select a "
                        "weapon so its model shows on the player. Client mode + cheats required "
                        "(engine_launch mode='client' does this)."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "model": {"type": "string", "description": "model path, e.g. 'sarge' or 'sarge/red'"},
                "headmodel": {"type": "string"},
                "weapon": {"type": "integer", "description": "weapon number to give+select"},
                "shoot": {"type": "boolean", "default": True, "description": "return a screenshot"},
            },
            "required": ["model"],
        },
        "handler": t_audition_model,
    },
    {
        "name": "engine_camera",
        "description": ("Place the free camera at a world position and aim it (setviewpos cheat). Aim "
                        "by yaw+pitch, OR pass look_at=[x,y,z] to auto-aim at a world point. Toggle "
                        "noclip to free-fly first. Returns a screenshot. (Pitch needs the rebuilt "
                        "qagame — engine_rebuild — already built here.)"),
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"},
                "yaw": {"type": "number", "default": 0},
                "pitch": {"type": "number", "description": "look up(-)/down(+) in degrees"},
                "look_at": {"type": "array", "items": {"type": "number"},
                            "description": "[x,y,z] world point to auto-aim at (overrides yaw/pitch)"},
                "noclip": {"type": "boolean", "default": False},
                "shoot": {"type": "boolean", "default": True},
            },
            "required": ["x", "y", "z"],
        },
        "handler": t_camera,
    },
    {
        "name": "engine_state",
        "description": ("Report where everything is right now: each connected client (name, number, "
                        "bot, team, health, origin, angles) plus the current camera. Use this so you "
                        "can frame/aim by name or position instead of guessing coordinates."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_state,
    },
    {
        "name": "engine_map_bounds",
        "description": ("The current map's true geometry extent (mins/maxs/center/size) read from the "
                        ".bsp — so you can frame or place things by the actual level, not just where "
                        "players are. engine_frame(mode='wide') uses this automatically for empty "
                        "courses. Loose-bsp maps only (None for pk3-only maps)."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_map_bounds,
    },
    {
        "name": "engine_look",
        "description": ("One-call orientation — get situated fast. Returns the current map/mode, who's "
                        "present (bots/players with positions, speed, health), and an auto-framed wide "
                        "screenshot of the scene (client mode). Use this first when you (re)attach to an "
                        "engine instead of calling status/state/screenshot separately."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_look,
    },
    {
        "name": "engine_capture_state",
        "description": ("Time a shot to a MEASURED game condition (not a log line): waits until a "
                        "subject's field crosses a threshold, then auto-frames that subject and films "
                        "it. E.g. field='speed' op='>' value=400 (catch a fast run), or field='health' "
                        "op='<' value=30. subject = name/number, 'any', 'self', or 'follow'."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "subject": {"type": "string", "default": "any"},
                "field": {"type": "string", "enum": ["speed", "health", "air", "wj", "dj", "team"],
                          "default": "speed", "description": "wj/dj = walljump/airjump count, air = airborne (1)"},
                "op": {"type": "string", "enum": [">", "<", ">=", "<=", "==", "!="], "default": ">"},
                "value": {"type": "number", "default": 0},
                "frames": {"type": "integer", "default": 6},
                "timeout": {"type": "number", "default": 20},
                "fps": {"type": "integer"},
                "title": {"type": "string"},
            },
        },
        "handler": t_capture_state,
    },
    {
        "name": "engine_measure",
        "description": ("Measure movement: sample a subject's speed over a window and return metrics "
                        "(max/avg/final speed, peak time, series) — quantify a bhop chain, slide-jump, "
                        "or surf line to tune movement by the numbers. subject='self' (the player you're "
                        "driving via engine_input play), 'follow' (a spectated bot), or a name/number."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "subject": {"type": "string", "default": "self"},
                "field": {"type": "string", "enum": ["speed", "health", "air", "wj", "dj"],
                          "default": "speed", "description": "wj/dj = walljump/airjump count, air = airborne"},
                "seconds": {"type": "number", "default": 4.0},
                "hz": {"type": "integer", "default": 10, "description": "samples per second"},
            },
        },
        "handler": t_measure,
    },
    {
        "name": "engine_compare",
        "description": ("Sweep a cvar across values and return a comparison in ONE call — decide which "
                        "value you want without eyeballing separate runs. mode='visual': frame `subject` "
                        "(or current view) and screenshot each value → a side-by-side collage (great for "
                        "effects/look/FOV). mode='movement': respawn + run `actions` (default: run "
                        "forward) under each value while measuring speed → a max-speed bar chart + a "
                        "metrics table (great for movement cvars like pm_*/g_speed/g_gravity)."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "cvar": {"type": "string"},
                "values": {"type": "array", "description": "values to sweep, e.g. [70,140,220]"},
                "mode": {"type": "string", "enum": ["visual", "movement"], "default": "visual"},
                "subject": {"description": "visual mode: what to frame (name/num/[x,y,z]/'action')"},
                "actions": {"type": "array", "items": {"type": "object"},
                            "description": "movement mode: input steps per value (default: run forward)"},
                "seconds": {"type": "number", "default": 2.5, "description": "movement mode run length"},
                "trials": {"type": "integer", "default": 3, "description": "movement mode: runs per value, median taken (scripted air-strafe is noisy)"},
                "title": {"type": "string"},
            },
            "required": ["cvar", "values"],
        },
        "handler": t_compare,
    },
    {
        "name": "engine_frame",
        "description": ("POINT & SHOOT: auto-place the camera to frame what you name and return the "
                        "screenshot — no coordinates or angles to guess. subject = a bot/player name or "
                        "number, [x,y,z], or 'action' (the players' centroid; auto-pulls back to fit a "
                        "spread-out group). mode='wide' frames the whole scene. dist/height/yaw are "
                        "optional standoff tweaks."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "subject": {"description": "bot/player name or number, [x,y,z], or 'action'"},
                "mode": {"type": "string", "enum": ["subject", "wide"], "default": "subject"},
                "dist": {"type": "number", "default": 240},
                "height": {"type": "number", "default": 110},
                "yaw": {"type": "number", "default": 35, "description": "orbit bearing around subject (deg)"},
                "radius": {"type": "number", "description": "mode=wide: pull-back radius for an empty course"},
                "shoot": {"type": "boolean", "default": True},
            },
        },
        "handler": t_frame,
    },
    {
        "name": "engine_orbit",
        "description": ("Turntable around a subject BY NAME/position — camera positions are computed "
                        "from the subject's live origin (time nearly frozen), baked into one collage. "
                        "No coordinates to guess."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "subject": {"description": "bot/player name or number, [x,y,z], or 'action'"},
                "angles": {"type": "array", "items": {"type": "number"}},
                "dist": {"type": "number", "default": 260},
                "height": {"type": "number", "default": 120},
                "title": {"type": "string"},
            },
        },
        "handler": t_orbit,
    },
    {
        "name": "engine_dolly",
        "description": ("Move a free camera along a path of waypoints, capturing a moving cinematic "
                        "clip → one collage (+ optional mp4). Waypoints are [x,y,z] eye points, or "
                        "[x,y,z,tx,ty,tz] eye+look-at so the camera tracks a target through the move."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "array", "items": {"type": "array", "items": {"type": "number"}},
                         "description": "list of [x,y,z] or [x,y,z,tx,ty,tz] waypoints"},
                "frames": {"type": "integer"},
                "interval": {"type": "number", "default": 0.25},
                "look": {"type": "boolean", "default": True},
                "fps": {"type": "integer"},
                "title": {"type": "string"},
            },
            "required": ["path"],
        },
        "handler": t_dolly,
    },
    {
        "name": "engine_input",
        "description": ("Drive the live player to interact with the world. `actions` is a sequence of "
                        "steps: {\"move\":[fwd,side],\"secs\":s} (fwd/side are ±1), {\"turn\":[yaw,pitch]} "
                        "(degrees; +yaw left, +pitch up), {\"attack\":secs} (fire/sword swing), "
                        "{\"jump\":n}, {\"use\":item}, {\"wait\":secs}. Set play=true to leave spectator "
                        "and spawn as a controllable player first. shoot=true returns a screenshot."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "actions": {"type": "array", "items": {"type": "object"}},
                "play": {"type": "boolean", "default": False, "description": "spawn as a live player first"},
                "weapon": {"type": "integer"},
                "shoot": {"type": "boolean", "default": False},
            },
            "required": ["actions"],
        },
        "handler": t_input,
    },
    {
        "name": "engine_capture_event",
        "description": ("Time a shot/clip to something you want to OBSERVE. Watches the live console for "
                        "`pattern` (regex — kills 'was killed'/'fragged', 'entered the game', race "
                        "finish, telemetry markers, your own echo beacons). mode='after' films the "
                        "aftermath once it fires; mode='window' rolls a pre-trigger buffer and keeps "
                        "`pre` frames before + `post` after, bracketing the moment. Returns a collage."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "pattern": {"type": "string"},
                "mode": {"type": "string", "enum": ["after", "window"], "default": "after"},
                "frames": {"type": "integer", "default": 9, "description": "mode=after frame count"},
                "interval": {"type": "number", "default": 0.3},
                "timeout": {"type": "number", "default": 30},
                "pre": {"type": "integer", "default": 4, "description": "mode=window pre-trigger frames"},
                "post": {"type": "integer", "default": 5, "description": "mode=window post-trigger frames"},
                "fps": {"type": "integer"},
                "title": {"type": "string"},
            },
            "required": ["pattern"],
        },
        "handler": t_capture_event,
    },
    {
        "name": "engine_spectate",
        "description": ("Become a spectator and follow a bot/player to film their gameplay "
                        "(third-person chase cam). target = client name/number, or omit to cycle to "
                        "the next. Use before engine_capture_clip / engine_capture_angles."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "target": {"description": "client name or number to follow (omit = next)"},
                "third_person": {"type": "boolean", "default": True},
                "range": {"type": "number", "default": 130},
                "angle": {"type": "number", "default": 25},
                "shoot": {"type": "boolean", "default": False},
            },
        },
        "handler": t_spectate,
    },
    {
        "name": "engine_record_demo",
        "description": ("Record real gameplay into a Quake demo (.dm_71) — a smooth full-motion, "
                        "replayable, shareable artifact (unlike the screenshot-collage clips). Records "
                        "`seconds` of whatever's happening now (bots playing, or you driving via "
                        "engine_input). Replay with engine_play_demo. Client mode."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "seconds": {"type": "number", "default": 6.0},
                "name": {"type": "string", "default": "clip"},
            },
        },
        "handler": t_record_demo,
    },
    {
        "name": "engine_play_demo",
        "description": "Play back a recorded demo by name.",
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string"}},
            "required": ["name"],
        },
        "handler": t_play_demo,
    },
    {
        "name": "engine_clip_video",
        "description": ("Capture a real-time mp4 of the current view for `seconds` and return the file "
                        "path — reliable smooth-ish footage (grabs frames as fast as possible and "
                        "stitches at the achieved rate so playback is real-time). Spectate a bot "
                        "(engine_spectate) or drive the player first to choose what it films."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "seconds": {"type": "number", "default": 5.0},
                "name": {"type": "string", "default": "clip"},
            },
        },
        "handler": t_clip_video,
    },
    {
        "name": "engine_cinematic",
        "description": ("A bullet-time hero shot to mp4: ramps time into slow-mo (slowmo, 0.05–1) and "
                        "orbits the camera around a subject while filming — the Matrix-rotate that fits "
                        "the sword / time-bind game. subject = a bot/player name, [x,y,z], or 'action'."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "subject": {"description": "bot/player name, [x,y,z], or 'action'"},
                "seconds": {"type": "number", "default": 5.0},
                "slowmo": {"type": "number", "default": 0.3, "description": "time scale 0.05–1 (lower = slower)"},
                "orbit_deg": {"type": "number", "default": 140, "description": "degrees the camera sweeps"},
                "name": {"type": "string", "default": "cinematic"},
            },
        },
        "handler": t_cinematic,
    },
    {
        "name": "engine_render_demo",
        "description": ("Render a recorded demo (engine_record_demo) to an mp4 via the engine's "
                        "framebuffer capture. Highest quality when the demo is dense; note demos "
                        "recorded on a listen server can be short (sparse snapshots) — for reliable "
                        "footage prefer engine_clip_video."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "fps": {"type": "integer", "default": 25},
            },
            "required": ["name"],
        },
        "handler": t_render_demo,
    },
    {
        "name": "engine_capture_clip",
        "description": ("Film the current view over time and bake the frames into a single collage "
                        "(contact sheet) image, returned so you can see it. Great for capturing a bot's "
                        "gameplay as a sequence. clean=true hides the HUD. Set fps for an mp4 too."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "frames": {"type": "integer", "default": 9},
                "interval": {"type": "number", "default": 0.4, "description": "seconds between frames"},
                "cols": {"type": "integer", "default": 3},
                "title": {"type": "string"},
                "fps": {"type": "integer", "description": "if set, also render an mp4 at this fps"},
                "clean": {"type": "boolean", "default": True, "description": "hide HUD for clean footage"},
            },
        },
        "handler": t_capture_clip,
    },
    {
        "name": "engine_capture_angles",
        "description": ("Capture the current third-person subject (a followed bot, or an auditioned "
                        "model) from several orbit angles and bake them into one collage image. "
                        "freeze=true nearly pauses time so every angle is the same moment (turntable)."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "angles": {"type": "array", "items": {"type": "number"},
                           "description": "orbit angles in degrees, e.g. [0,90,180,270]"},
                "range": {"type": "number", "default": 130},
                "freeze": {"type": "boolean", "default": True},
                "title": {"type": "string"},
                "clean": {"type": "boolean", "default": True},
            },
        },
        "handler": t_capture_angles,
    },
    {
        "name": "engine_give",
        "description": "Cheat: give items/weapons — 'all', 'weapon <n>', 'health', 'ammo', etc.",
        "inputSchema": {
            "type": "object",
            "properties": {"what": {"type": "string", "default": "all"}},
        },
        "handler": t_give,
    },
    {
        "name": "engine_spawn",
        "description": ("Spawn an entity into the live world to dress/edit a running map (cheat). "
                        "classname like 'item_health_mega', 'weapon_rocketlauncher', 'item_armor_body', "
                        "'target_push'/'trigger_push', 'item_quad', lights, etc. `keys` are entity spawn "
                        "key/values; defaults to ~96u in front of the view unless you pass "
                        "keys={'origin':'x y z'}. shoot=true screenshots it. NOTE: 'misc_model' does NOT "
                        "render at runtime (it's compile-time/q3map-only) — to show a custom mesh, make "
                        "it a player/weapon model (engine_audition_model) or bake it into a map."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "classname": {"type": "string"},
                "keys": {"type": "object", "description": "entity spawn key/values, e.g. {'origin':'0 0 64'}"},
                "shoot": {"type": "boolean", "default": False},
            },
            "required": ["classname"],
        },
        "handler": t_spawn,
    },
    {
        "name": "engine_clear",
        "description": ("Remove entities spawned live via engine_spawn (optionally only one classname) "
                        "— the map's own entities are untouched. Iterate on level dressing: spawn → "
                        "look → clear → respawn."),
        "inputSchema": {
            "type": "object",
            "properties": {"classname": {"type": "string", "description": "only clear this classname"}},
        },
        "handler": t_clear,
    },
    {
        "name": "engine_save_preset",
        "description": ("Snapshot the current movement + effect cvars to a named preset (.cfg) so you "
                        "can checkpoint a 'feel' and restore or A/B it later. Pass `cvars` to snapshot "
                        "a specific subset."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "cvars": {"type": "array", "items": {"type": "string"}},
            },
            "required": ["name"],
        },
        "handler": t_save_preset,
    },
    {
        "name": "engine_load_preset",
        "description": "Apply a saved tuning preset (restore a checkpointed feel).",
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string"}},
            "required": ["name"],
        },
        "handler": t_load_preset,
    },
    {
        "name": "engine_list_presets",
        "description": "List saved tuning presets.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_list_presets,
    },
    {
        "name": "engine_effects_get",
        "description": ("Snapshot the visual-effect / look palette (audio-reactive shader envelopes, "
                        "bullet-time, FOV/third-person, shadows/marks/trails, renderer gamma/picmip) "
                        "with current values, ranges, and which need a vid_restart."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_effects_get,
    },
    {
        "name": "engine_effects_set",
        "description": ("Tune one effect cvar live (clamped to range). Renderer cvars auto vid_restart. "
                        "Set shoot=true to get a screenshot of the result for the adapt loop. Use "
                        "engine_effects_get for the palette."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "enum": sorted(EFFECT_CVARS)},
                "value": {"type": "number"},
                "clamp": {"type": "boolean", "default": True},
                "restart": {"type": "boolean", "description": "override auto vid_restart"},
                "shoot": {"type": "boolean", "default": False},
            },
            "required": ["name", "value"],
        },
        "handler": t_effects_set,
    },
    {
        "name": "engine_telemetry",
        "description": ("Read the modded qagame's JSONL playtest telemetry from this session "
                        "(requires g_playtest 1). Returns the most recent records."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "tag": {"type": "string", "description": "g_playtestTag to filter by"},
                "limit": {"type": "integer", "default": 50},
            },
        },
        "handler": t_telemetry,
    },
    {
        "name": "engine_source_constants",
        "description": ("List the source-tier movement constants in bg_pmove.c / bg_local.h (bhop boost, "
                        "air-dash, double-jump, slide, wall-jump, wall-run, JUMP_VELOCITY, etc.) with "
                        "values and file:line. Each is flagged `live` (tune now via engine_movement_set) "
                        "or rebuild-only (edit the file, then engine_rebuild). The full tunable surface."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_source_constants,
    },
    {
        "name": "engine_config_overrides",
        "description": ("Show cvars whose startup config (autoexec.cfg / strafe64.cfg) overrides the "
                        "g_main.c source default — so you see where the running game diverges from "
                        "documented defaults (live cvars can silently differ from source). File parse, "
                        "no engine needed."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_config_overrides,
    },
    {
        "name": "engine_set_source_constant",
        "description": ("Change a rebuild-only movement constant (from engine_source_constants) by "
                        "editing its line in bg_pmove.c / bg_local.h in place. Pass rebuild=true to "
                        "rebuild + redeploy so it takes effect (slow). For live cvar-backed constants "
                        "use engine_movement_set instead (this refuses them). Full source-tier tuning."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "value": {"description": "new numeric value"},
                "rebuild": {"type": "boolean", "default": False, "description": "rebuild+deploy after editing"},
            },
            "required": ["name", "value"],
        },
        "handler": t_set_source_constant,
    },
    {
        "name": "engine_rebuild",
        "description": ("SOURCE TIER: rebuild the native engine + mod (scripts/build.sh) and redeploy "
                        "the dylibs. Use after editing compile-time movement constants in bg_pmove.c / "
                        "bg_local.h that are not cvar-backed (JUMP_VELOCITY, wall-jump/slide/bhop). "
                        "Slow (minutes). Shut the engine down first, rebuild, then relaunch."),
        "inputSchema": {
            "type": "object",
            "properties": {"clean": {"type": "boolean", "default": False}},
        },
        "handler": t_rebuild,
    },
    {
        "name": "engine_generate_map",
        "description": ("Author a level: generate a procedural course with strafegen, deploy it into "
                        "the running engine, and load it live — design + play in one call. kind: "
                        "course (linear run), arena (deathmatch bowl), surf (banked surf line), "
                        "killbox (vertical melee arena). Same seed reproduces the same map. "
                        "shoot=true returns a screenshot of the loaded map."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "seed": {"description": "course seed (int); omit for a random one"},
                "kind": {"type": "string", "enum": ["course", "arena", "surf", "killbox"],
                         "default": "course"},
                "difficulty": {"type": "integer", "enum": [0, 1, 2]},
                "length": {"type": "integer", "description": "length multiplier (1 ≈ 6 sections)"},
                "load": {"type": "boolean", "default": True, "description": "load it into the engine now"},
                "devmap": {"type": "boolean", "default": True, "description": "load with cheats"},
                "shoot": {"type": "boolean", "default": False},
            },
        },
        "handler": t_generate_map,
    },
    {
        "name": "engine_reload",
        "description": ("Re-init the renderer (vid_restart) and optionally sound (snd_restart) so "
                        "freshly-deployed textures/shaders/models/sounds are picked up live without "
                        "relaunching — the asset-iteration step."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "visual": {"type": "boolean", "default": True},
                "sound": {"type": "boolean", "default": False},
            },
        },
        "handler": t_reload,
    },
    {
        "name": "engine_playtest_report",
        "description": ("Judge a map: run a headless bot playtest with telemetry and return a fitness "
                        "report — completion %, flow, airborne %, avg max speed, best bhop chain, "
                        "moveset usage, death causes, and a verdict flagging metrics outside their "
                        "target bands. The design→playtest→iterate loop for procedural courses. Spins "
                        "its own dedicated server (doesn't disturb the current session). Needs a map "
                        "with an .aas navmesh so bots can navigate."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "map": {"type": "string", "description": "map to test; omit for the default sandbox"},
                "bots": {"type": "integer", "default": 5},
                "seconds": {"type": "number", "default": 30},
                "skill": {"type": "integer", "default": 4, "description": "bot skill 1-5"},
            },
        },
        "handler": t_playtest_report,
    },
    {
        "name": "engine_selftest",
        "description": ("Health-check the whole API end-to-end: spins its own client engine and "
                        "exercises cvars, movement, effects, state, spawn/clear, presets, framing, "
                        "screenshot, and demo — returns pass/fail per capability. Run after a rebuild "
                        "or to confirm the engine + tooling are working. Opens a window briefly."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_selftest,
    },
    {
        "name": "engine_processes",
        "description": ("List this repo's running engine processes, classified as 'persistent' "
                        "(engine_open), 'session' (engine_launch), or 'other'. Visibility into what's "
                        "currently running across sessions."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_processes,
    },
    {
        "name": "engine_kill_orphans",
        "description": ("Clean up orphaned session-scoped engine windows (left over when their launcher "
                        "exited). Never touches persistent (engine_open) engines, foreign engines, or "
                        "the current live session — the safe 'close leftovers' tool."),
        "inputSchema": {"type": "object", "properties": {}},
        "handler": t_kill_orphans,
    },
]

_BY_NAME = {t["name"]: t for t in TOOLS}


# --- JSON-RPC 2.0 over stdio (newline-delimited) ----------------------------

def _send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def _result(id_, result):
    _send({"jsonrpc": "2.0", "id": id_, "result": result})


def _error(id_, code, message):
    _send({"jsonrpc": "2.0", "id": id_, "error": {"code": code, "message": message}})


def _handle(req):
    method = req.get("method")
    id_ = req.get("id")
    params = req.get("params") or {}

    if method == "initialize":
        _result(id_, {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
            "instructions": INSTRUCTIONS,
        })
        return
    if method in ("notifications/initialized", "initialized"):
        return  # notification, no reply
    if method == "ping":
        _result(id_, {})
        return
    if method == "tools/list":
        _result(id_, {"tools": [{k: t[k] for k in ("name", "description", "inputSchema")}
                                for t in TOOLS]})
        return
    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        tool = _BY_NAME.get(name)
        if not tool:
            _error(id_, -32602, f"unknown tool: {name}")
            return
        try:
            out = tool["handler"](**args)
            if isinstance(out, dict) and "_mcp_content" in out:
                _result(id_, {"content": out["_mcp_content"]})
            else:
                text = out if isinstance(out, str) else json.dumps(out, indent=2, default=str)
                _result(id_, {"content": [{"type": "text", "text": text}]})
        except Exception as e:
            msg = f"{type(e).__name__}: {e}"
            if not isinstance(e, EngineError):
                msg += "\n" + traceback.format_exc()
            _result(id_, {"content": [{"type": "text", "text": msg}], "isError": True})
        return

    if id_ is not None:
        _error(id_, -32601, f"method not found: {method}")


@atexit.register
def _shutdown_engine():
    global _session
    if _session is not None:
        try:
            _session.stop()
        except Exception:
            pass
        _session = None


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue
        try:
            _handle(req)
        except Exception:
            # never let one bad request kill the server loop
            sys.stderr.write(traceback.format_exc())
            sys.stderr.flush()


if __name__ == "__main__":
    main()
