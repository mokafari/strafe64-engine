#!/usr/bin/env python3
"""STRAFE 64 engine control API — let an LLM hook directly into the engine.

This is the low-level Python library; `engine_mcp.py` wraps it as an MCP server
so Claude (or any LLM) can drive a running engine as a set of tools.

It speaks the engine's *native* dev plumbing, so it needs no new C code:

  * **command channel (in)** — `com_pipefile`. The engine creates a FIFO in its
    home dir and executes whatever console text is written to it, live, every
    frame (`Com_ReadFromPipe`). We open that FIFO and write commands to it.
  * **reply channel (out)** — `com_logfile 2`. Every `Com_Printf` is flushed to
    `<home>/<game>/qconsole.log`. To read a value back we bracket a query with
    `echo` sentinels and scrape the text the engine prints between them.

So the two-tier control surface is:

  LIVE  (no rebuild)   set/get any cvar, run any console command, change maps,
                       take screenshots, read the mod's JSONL telemetry — all
                       against an already-running instance.
  SOURCE (rebuild)     the compile-time movement constants in bg_pmove.c that
                       aren't cvar-backed: edit + `rebuild()` + redeploy.

A subset of the movement model *is* already cvar-backed (g_main.c mirrors these
cvars into the bg_pmove globals every frame, on both game and cgame), so those
tune live — see MOVEMENT_CVARS.

Example
-------
    from engine_api import EngineSession
    with EngineSession(mode="dedicated", map="strafe64_1337") as eng:
        print(eng.get_cvar("pm_strafeAccelerate"))   # -> "70"
        eng.set_cvar("pm_airControlAmount", 220)
        print(eng.console("cvarlist pm_"))            # any console command
"""

from __future__ import annotations

import errno
import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

# --- paths (mirror scripts/run.sh) ------------------------------------------

ROOT = Path(__file__).resolve().parents[2]            # strafe64-engine/
ENGINE_DIR = ROOT / "engine" / "build" / "Release"
APP_BIN = ENGINE_DIR / "ioquake3.app" / "Contents" / "MacOS" / "ioquake3"
DED_BIN = ENGINE_DIR / "ioq3ded"
BUILD_BASEQ3 = ENGINE_DIR / "baseq3"                  # built dylibs land here
STRAFEGEN = ROOT / "tools" / "strafegen"
BUILD_SH = ROOT / "scripts" / "build.sh"

GAME = "baseoa"
DYLIBS = ("qagame", "cgame", "ui")
DEFAULT_OA = os.environ.get("OA", str(ROOT / "assets" / "openarena"))

# Where the LLM boots when no map is given: a known-good playtest sandbox.
# dojo_arena is a purpose-built bot-dojo arena — enclosed (camera framing/clamp
# behave), ships a working .aas (bots navigate), uses the STRAFE shaders, and is
# open enough to audition models, test effects, drive the player, and film bots.
DEFAULT_MAP = os.environ.get("S64_DEFAULT_MAP", "dojo_arena")

# Persistent engines live under LIVE_BASE, one named subdir each, so multiple
# sessions can each open + keep their own engine running and reattach by name.
LIVE_BASE = os.environ.get("S64_LIVE_BASE", str(Path.home() / ".strafe64-engine"))
STATE_FILE = "engine_api.session.json"


def live_home(name: str = "default") -> str:
    """Home dir for a named persistent engine."""
    # keep names filesystem-safe
    safe = re.sub(r"[^A-Za-z0-9._-]", "_", name) or "default"
    return str(Path(LIVE_BASE) / safe)


# back-compat alias for the original single-instance home
LIVE_HOME = live_home("default")


def _pid_alive(pid: int | None) -> bool:
    if not pid:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def free_port(start: int = 27960, span: int = 400, exclude=()) -> int:
    """Find a free UDP port (the engine binds net_port) so instances don't clash.

    `exclude` skips ports already claimed by other live instances — without this,
    two engines opened back-to-back would both probe-pick the same lowest port.
    """
    exclude = set(exclude or ())
    for p in range(start, start + span):
        if p in exclude:
            continue
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.bind(("0.0.0.0", p))
            return p
        except OSError:
            continue
        finally:
            s.close()
    raise EngineError(f"no free UDP port in {start}..{start+span}")

# Color-code stripper for engine console text (^1..^9, ^0).
_COLOR = re.compile(r"\^[0-9]")
# Cvar_Print:  "name" is:"value^7" ...  — the tty console strips color codes,
# so we just take the quoted value after is: .
_CVAR_PRINT = re.compile(r'"[^"]*"\s+is:"(.*?)"')


def strip_colors(s: str) -> str:
    return _COLOR.sub("", s)


# --- the live-tunable movement model ----------------------------------------
# These cvars are mirrored into the bg_pmove globals each frame (see g_main.c
# cvarTable + cg_predict.c), so setting them changes movement feel immediately
# with no rebuild. range/default are advisory metadata for the tune tools.

MOVEMENT_CVARS = {
    "pm_strafeAccelerate": dict(default=70.0,  range=(0, 300),
                                doc="A/D-only air-strafe acceleration"),
    "pm_airControlAmount": dict(default=150.0, range=(0, 400),
                                doc="W-only velocity-redirect strength"),
    "pm_airaccelerate":    dict(default=1.0,   range=(0, 20),
                                doc="base air acceleration"),
    "pm_airStopAccelerate": dict(default=3.0,  range=(0, 20),
                                 doc="counter-strafe stopping accel in air"),
    "pm_wishSpeedClamp":   dict(default=30.0,  range=(0, 320),
                                doc="air wish-speed cap — the skill ceiling"),
    "g_gravity":           dict(default=1000.0, range=(0, 4000),
                                doc="world gravity (latched; needs map reload)"),
    "g_knockback":         dict(default=1000.0, range=(0, 4000),
                                doc="weapon knockback scale"),
    "g_speed":             dict(default=320.0, range=(0, 1000),
                                doc="ground move speed"),
    "timescale":           dict(default=1.0,   range=(0.05, 4),
                                doc="global time multiplier (slow-mo/fast)"),
}


# --- the visual-effect / look palette ---------------------------------------
# Cvars an LLM auditions to dial in the look. `restart` flags a renderer cvar
# that only takes effect after a vid_restart (set_effect handles that).

EFFECT_CVARS = {
    # audio-reactive shader envelopes — normally driven by the music band
    # analyser, but you can pin them to preview how the world warps in time.
    "au_bass":  dict(default=0.0, range=(0, 1), restart=False, doc="bass-band shader envelope"),
    "au_mid":   dict(default=0.0, range=(0, 1), restart=False, doc="mid-band shader envelope"),
    "au_high":  dict(default=0.0, range=(0, 1), restart=False, doc="high-band shader envelope"),
    "au_level": dict(default=0.0, range=(0, 1), restart=False, doc="overall level shader envelope"),
    # bullet-time core (the sword/slow-mo direction)
    "g_timeBind":    dict(default=1.0, range=(0, 1),  restart=False, doc="enable time-bind slow-mo"),
    "g_timeBindMin": dict(default=0.05, range=(0.01, 1), restart=False, doc="near-freeze floor when still"),
    # view / camera
    "cg_fov":              dict(default=90.0, range=(60, 160), restart=False, doc="field of view"),
    "cg_thirdPerson":      dict(default=0.0,  range=(0, 1),    restart=False, doc="third-person view"),
    "cg_thirdPersonRange": dict(default=80.0, range=(0, 400),  restart=False, doc="third-person camera distance"),
    "cg_thirdPersonAngle": dict(default=0.0,  range=(0, 360),  restart=False, doc="third-person orbit angle"),
    "cg_drawGun":          dict(default=1.0,  range=(0, 1),    restart=False, doc="draw the first-person weapon"),
    # particles / decals / trails
    "cg_shadows":      dict(default=1.0,   range=(0, 3),    restart=False, doc="0 none,1 blob,2 stencil,3 projection"),
    "cg_marks":        dict(default=1.0,   range=(0, 1),    restart=False, doc="wall/floor impact decals"),
    "cg_brassTime":    dict(default=2500.0, range=(0, 5000), restart=False, doc="ejected-brass lifetime ms"),
    "cg_railTrailTime": dict(default=400.0, range=(0, 5000), restart=False, doc="rail trail lifetime ms"),
    # renderer look (need a vid_restart to apply)
    "r_gamma":         dict(default=1.0, range=(0.5, 3),  restart=True,  doc="display gamma"),
    "r_picmip":        dict(default=0.0, range=(0, 6),    restart=True,  doc="texture downscale (lo-fi look)"),
    "r_overBrightBits": dict(default=0.0, range=(0, 2),   restart=True,  doc="overbright lighting bits"),
    "r_ext_texture_filter_anisotropic": dict(default=0.0, range=(0, 16), restart=True, doc="aniso filtering"),
}


class EngineError(RuntimeError):
    pass


def deploy_dylibs(oa: str = DEFAULT_OA, cfgs: bool = True, force: bool = False) -> list[str]:
    """Copy the freshly-built modded dylibs into <oa>/baseoa and re-sign them.

    Mirrors run.sh: the three dylibs share networked headers, so they must be
    deployed together. Apple Silicon SIGKILLs an invalidly-signed dylib on
    dlopen, hence the ad-hoc re-sign.

    All-or-nothing guard: if any deployed dylib is NEWER than our build output, a
    concurrent build deployed it — don't regress it (or mix a stale set with a
    fresh one). Skip the deploy and use what's there. `force=True` overrides.
    """
    import sys
    dest = Path(oa) / GAME
    dest.mkdir(parents=True, exist_ok=True)
    srcs = {n: BUILD_BASEQ3 / f"{n}.dylib" for n in DYLIBS
            if (BUILD_BASEQ3 / f"{n}.dylib").exists()}
    if not force:
        for name, src in srcs.items():
            dst = dest / f"{name}.dylib"
            if dst.exists() and dst.stat().st_mtime > src.stat().st_mtime + 1:
                sys.stderr.write(
                    f"deploy_dylibs: deployed {name}.dylib is newer than the build output "
                    f"(concurrent build?) — keeping the deployed set, skipping deploy "
                    f"(force=True to override).\n")
                done = []
                if cfgs:
                    for cfg in ("strafe64.cfg", "psx.cfg"):
                        s = STRAFEGEN / cfg
                        if s.exists():
                            shutil.copy2(s, dest / cfg)
                return done
    done = []
    for name, src in srcs.items():
        dst = dest / f"{name}.dylib"
        shutil.copy2(src, dst)
        subprocess.run(["codesign", "-f", "-s", "-", str(dst)],
                       capture_output=True)
        done.append(name)
    if cfgs:
        for cfg in ("strafe64.cfg", "psx.cfg"):
            src = STRAFEGEN / cfg
            if src.exists():
                shutil.copy2(src, dest / cfg)
    return done


class EngineSession:
    """A running engine instance you can drive command-by-command.

    mode:
      "client"     windowed ioquake3.app (needs a display) — for visual work
      "dedicated"  headless ioq3ded — for fast cvar/telemetry iteration
    """

    def __init__(self, mode: str = "dedicated", *, map: str | None = None,
                 oa: str = DEFAULT_OA, home: str | None = None,
                 port: int | None = None, fullscreen: bool = False,
                 deploy: bool = True, extra: list[str] | None = None,
                 cheats: bool | None = None, detached: bool = False,
                 name: str | None = None, bots=0, bot_skill: int = 4,
                 width: int | None = None, height: int | None = None,
                 pipename: str = "s64api.pipe", logname: str = "engine_console.log"):
        if mode not in ("client", "dedicated"):
            raise ValueError(f"mode must be client|dedicated, got {mode!r}")
        self.mode = mode
        # boot into the default playtest sandbox unless a map (or "none"/"") is
        # explicitly given — so the LLM always lands somewhere useful.
        if map is None:
            map = DEFAULT_MAP
        elif str(map).lower() in ("none", "", "menu"):
            map = None
        self.map = map
        self.oa = oa
        self.port = port            # None → allocated free at start()
        self.name = name
        self.fullscreen = fullscreen
        # client window resolution. Default low so several windows stay light
        # and screenshots are small/fast; pass 0/0 to use the desktop resolution.
        self.width = 640 if width is None else width
        self.height = 360 if height is None else height
        self.deploy = deploy
        self.extra = list(extra or [])
        # cheats default on for client (so audition cmds — setviewpos, give,
        # noclip — work); off for dedicated. Achieved via `devmap` on load.
        self.cheats = (mode == "client") if cheats is None else cheats
        # detached: run in its own session so the engine keeps running after this
        # process exits; reattach later via EngineSession.attach().
        self.detached = detached
        # bots: an int count (default roster) or a list of bot names. Added AFTER
        # the map loads — the game registers `addbot` only once a map is up, so
        # +addbot at launch is dropped as an unknown command.
        self.bots = bots
        self.bot_skill = bot_skill
        self.pipename = pipename
        self.logname = logname
        self._pid: int | None = None
        self._attached = False

        self.home = Path(home) if home else Path(tempfile.mkdtemp(prefix="s64api_"))
        self._owns_home = home is None
        self._gamedir = self.home / GAME
        self._gamedir.mkdir(parents=True, exist_ok=True)
        self.pipe_path = self._gamedir / pipename
        # The reply channel: the engine's console goes to stderr via CON_Print
        # (fputs to stderr, which is unbuffered), so our own captured copy is a
        # real-time, complete transcript — more reliable than qconsole.log,
        # which the dedicated server doesn't always open. We tail this file.
        self.log_path = self._gamedir / logname

        self.proc: subprocess.Popen | None = None
        self._pipe_fd: int | None = None
        self._log_fp = None
        self._cap = None

    # -- lifecycle -----------------------------------------------------------

    def start(self, timeout: float = 30.0) -> "EngineSession":
        if self.proc:
            raise EngineError("session already started")
        binary = DED_BIN if self.mode == "dedicated" else APP_BIN
        if not Path(binary).exists():
            raise EngineError(f"engine not built: {binary} missing — run scripts/build.sh")
        if not (Path(self.oa) / GAME).exists():
            raise EngineError(f"assets not found at {self.oa} (set OA=...)")
        if self.deploy:
            deploy_dylibs(self.oa)
        if self.port is None:
            claimed = {e["port"] for e in self.list_live() if e.get("alive") and e.get("port")}
            self.port = free_port(exclude=claimed)

        args = [
            str(binary),
            "+set", "com_basegame", GAME,
            "+set", "fs_basepath", self.oa,
            "+set", "fs_homepath", str(self.home),
            "+set", "sv_pure", "0",
            "+set", "vm_game", "0",
            "+set", "vm_cgame", "0",
            "+set", "vm_ui", "0",
            "+set", "com_pipefile", self.pipename,
            "+set", "net_port", str(self.port),
        ]
        if self.mode == "dedicated":
            args += ["+set", "dedicated", "1", "+set", "sv_maxclients", "16"]
        else:
            args += ["+set", "r_fullscreen", "1" if self.fullscreen else "0"]
            # r_mode -1 = custom resolution from r_customwidth/height; 0/0 falls
            # back to the engine default (desktop) via r_mode -2.
            if self.width and self.height:
                args += ["+set", "r_mode", "-1",
                         "+set", "r_customwidth", str(self.width),
                         "+set", "r_customheight", str(self.height)]
        args += self.extra
        if self.map:
            args += ["+devmap" if self.cheats else "+map", self.map]

        # capture the engine's console (stdout+stderr) to our reply-channel file
        self._cap = open(self.log_path, "wb")
        # detached → new session so the engine outlives this process (the child
        # keeps its own dup of the log fd, so the transcript keeps growing).
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE,
                                     stdout=self._cap, stderr=subprocess.STDOUT,
                                     start_new_session=self.detached)
        self._pid = self.proc.pid
        self._log_fp = open(self.log_path, "rb")
        self._await_ready(timeout)
        self._write_state()
        return self

    # -- persistence (open once, reattach across processes) ------------------

    @property
    def state_path(self) -> Path:
        return self._gamedir / STATE_FILE

    def _write_state(self):
        self.state_path.write_text(json.dumps({
            "name": self.name, "pid": self._pid, "mode": self.mode, "map": self.map,
            "home": str(self.home), "oa": self.oa, "port": self.port,
            "pipe": str(self.pipe_path), "log": str(self.log_path),
            "detached": self.detached,
        }, indent=2))

    @staticmethod
    def list_live(base: str = LIVE_BASE) -> list[dict]:
        """List every persistent engine instance under `base` (alive flag set)."""
        out = []
        b = Path(base)
        if not b.exists():
            return out
        for sp in sorted(b.glob(f"*/{GAME}/{STATE_FILE}")):
            try:
                st = json.loads(sp.read_text())
            except (OSError, json.JSONDecodeError):
                continue
            st["alive"] = _pid_alive(st.get("pid"))
            st.setdefault("name", sp.parent.parent.name)
            out.append(st)
        return out

    @staticmethod
    def peek(home: str = LIVE_HOME) -> dict | None:
        """Read a persistent engine's state without attaching (no fds opened)."""
        sp = Path(home) / GAME / STATE_FILE
        if not sp.exists():
            return None
        try:
            st = json.loads(sp.read_text())
        except (OSError, json.JSONDecodeError):
            return None
        st["alive"] = _pid_alive(st.get("pid"))
        return st

    @classmethod
    def attach(cls, home: str = LIVE_HOME) -> "EngineSession":
        """Reconnect to an engine started earlier (e.g. by a prior MCP process).

        Reads the state file under `home`, reopens the command FIFO + the console
        log, and returns a session that drives the still-running engine.
        """
        sp = Path(home) / GAME / STATE_FILE
        if not sp.exists():
            raise EngineError(f"no persistent engine state at {sp}")
        st = json.loads(sp.read_text())
        if not _pid_alive(st.get("pid")):
            raise EngineError(f"persistent engine (pid {st.get('pid')}) is not running")
        self = cls(mode=st["mode"], map=st.get("map"), oa=st.get("oa", DEFAULT_OA),
                   home=st["home"], port=st.get("port"), name=st.get("name"),
                   deploy=False, detached=st.get("detached", True))
        self._pid = st["pid"]
        self._attached = True
        if not self.pipe_path.exists():
            raise EngineError(f"command FIFO missing at {self.pipe_path}")
        self._pipe_fd = os.open(str(self.pipe_path), os.O_WRONLY)
        self._log_fp = open(self.log_path, "rb")
        self._log_fp.seek(0, os.SEEK_END)
        return self

    def _await_ready(self, timeout: float):
        """Wait for the FIFO to appear (engine reached Com_Init), then settle."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise EngineError(f"engine exited during startup (code {self.proc.returncode}) "
                                  f"— see {self.log_path}")
            if self.pipe_path.exists():
                break
            time.sleep(0.1)
        else:
            raise EngineError(f"engine did not create com_pipefile within {timeout}s "
                              f"— see {self.log_path}")
        # open the FIFO for writing (engine holds the read end open since startup)
        self._pipe_fd = os.open(str(self.pipe_path), os.O_WRONLY)
        # let the first map load settle so commands aren't dropped pre-spawn
        if self.map:
            self.wait_for(r"AAS initialized|Map Loading|spawn|ClientBegin",
                          timeout=timeout, soft=True)
            self._spawn_bots()

    _BOT_ROSTER = ("gargoyle", "merman", "kyonshi", "beret", "major", "sarge",
                   "grism", "angelyss", "arachna", "ayumi")

    def add_bot(self, name: str, skill: int | None = None):
        """Add one bot (after a map is loaded — `addbot` isn't valid before)."""
        self.command(f"addbot {name} {skill if skill is not None else self.bot_skill}")

    def _spawn_bots(self):
        if not self.bots:
            return
        names = ([self._BOT_ROSTER[i % len(self._BOT_ROSTER)] for i in range(int(self.bots))]
                 if isinstance(self.bots, int) else list(self.bots))
        time.sleep(0.5)
        for n in names:
            self.add_bot(n)
            time.sleep(0.2)

    def stop(self, timeout: float = 8.0):
        """Release this session. A detached/attached engine keeps running (use
        close() to actually quit it); a normal session is quit + killed."""
        if self.detached or self._attached:
            self._cleanup(rm_home=False)
            return
        if not self.proc:
            return
        try:
            self.command("quit")
        except Exception:
            pass
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        finally:
            self._cleanup()

    def close(self, timeout: float = 8.0):
        """Quit the engine for good (works for persistent/detached too) and
        remove its state file."""
        try:
            self.command("quit")
        except Exception:
            pass
        deadline = time.time() + timeout
        while time.time() < deadline and _pid_alive(self._pid):
            time.sleep(0.2)
        if _pid_alive(self._pid):
            try:
                os.kill(self._pid, 15)
            except OSError:
                pass
        try:
            self.state_path.unlink()
        except OSError:
            pass
        self._cleanup(rm_home=not (self.detached or self._attached))

    def _cleanup(self, rm_home: bool | None = None):
        for closer in (self._pipe_fd, self._log_fp, self._cap):
            try:
                if isinstance(closer, int):
                    os.close(closer)
                elif closer:
                    closer.close()
            except Exception:
                pass
        self._pipe_fd = self._log_fp = self._cap = None
        if rm_home is None:
            rm_home = self._owns_home
        if rm_home and self.home.exists():
            shutil.rmtree(self.home, ignore_errors=True)

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()

    @property
    def alive(self) -> bool:
        if self.proc is not None:
            return self.proc.poll() is None
        return _pid_alive(self._pid)

    # -- raw IO --------------------------------------------------------------

    def command(self, cmd: str):
        """Fire a console command into the running engine (no reply captured)."""
        if self._pipe_fd is None:
            raise EngineError("session not started")
        if not self.alive:
            raise EngineError("engine has exited")
        os.write(self._pipe_fd, (cmd.rstrip("\n") + "\n").encode())

    def _read_new(self) -> str:
        if not self._log_fp:
            return ""
        return self._log_fp.read().decode("latin-1", "replace")

    def wait_for(self, pattern: str, timeout: float = 10.0, soft: bool = False) -> str:
        """Block until `pattern` (regex, '|'-alternated ok) appears in the log."""
        rx = re.compile(pattern)
        buf, deadline = "", time.time() + timeout
        while time.time() < deadline:
            chunk = self._read_new()
            if chunk:
                buf += chunk
                if rx.search(strip_colors(buf)):
                    return buf
            else:
                time.sleep(0.03)
        if soft:
            return buf
        raise EngineError(f"timed out waiting for {pattern!r}")

    def console(self, cmd: str, timeout: float = 6.0) -> str:
        """Run a command and return whatever the engine printed in response.

        Brackets the command with unique `echo` sentinels and scrapes the text
        the engine flushes to qconsole.log between them.
        """
        if self._pipe_fd is None:
            raise EngineError("session not started")
        nonce = os.urandom(4).hex()
        begin, end = f"@@S64B_{nonce}@@", f"@@S64E_{nonce}@@"
        # drain anything already pending so our window is clean
        self._read_new()
        os.write(self._pipe_fd,
                 f"echo {begin}\n{cmd}\necho {end}\n".encode())
        buf, deadline = "", time.time() + timeout
        while time.time() < deadline:
            buf += self._read_new()
            clean = strip_colors(buf)
            if end in clean:
                body = clean.split(begin, 1)[-1].split(end, 1)[0]
                # drop the echoed sentinel lines themselves
                return "\n".join(ln for ln in body.splitlines()
                                 if begin not in ln and end not in ln).strip("\n")
            if not self.alive:
                raise EngineError("engine exited")
            time.sleep(0.03)
        raise EngineError(f"no reply for command within {timeout}s: {cmd!r}")

    # -- cvars ---------------------------------------------------------------

    def get_cvar(self, name: str, timeout: float = 6.0) -> str | None:
        out = self.console(f"print {name}", timeout=timeout)
        m = _CVAR_PRINT.search(out)
        return m.group(1) if m else None

    def set_cvar(self, name: str, value, confirm: bool = True):
        self.command(f'set {name} "{value}"')
        if confirm:
            got = self.get_cvar(name)
            return got
        return None

    def cvarlist(self, prefix: str = "") -> str:
        return self.console(f"cvarlist {prefix}".strip())

    # -- movement model ------------------------------------------------------

    def get_movement(self) -> dict:
        """Snapshot the live-tunable movement cvars."""
        out = {}
        for name, meta in MOVEMENT_CVARS.items():
            val = self.get_cvar(name)
            out[name] = dict(value=val, **meta)
        return out

    def set_movement(self, name: str, value, clamp: bool = True):
        if name not in MOVEMENT_CVARS:
            raise EngineError(f"{name} is not a live movement cvar; "
                              f"known: {sorted(MOVEMENT_CVARS)}")
        lo, hi = MOVEMENT_CVARS[name]["range"]
        v = float(value)
        if clamp:
            v = max(lo, min(hi, v))
        return self.set_cvar(name, v)

    # -- map / view ----------------------------------------------------------

    def change_map(self, mapname: str, devmap: bool | None = None, wait: bool = True):
        if devmap is None:
            devmap = self.cheats
        self.map = mapname
        self.command(f"{'devmap' if devmap else 'map'} {mapname}")
        if wait:
            self.wait_for(r"AAS initialized|Map Loading|spawn|ClientBegin",
                          timeout=30, soft=True)

    # -- audition / camera (client mode, cheats) -----------------------------

    def setviewpos(self, x, y, z, yaw: float = 0.0, pitch: float | None = None):
        """Teleport the view to a world position + facing (cheat).

        pitch needs the extended `setviewpos x y z yaw pitch` (qagame rebuild);
        omit it for plain yaw-only placement.
        """
        cmd = f"setviewpos {int(x)} {int(y)} {int(z)} {yaw:g}"
        if pitch is not None:
            cmd += f" {pitch:g}"
        self.command(cmd)

    def look_at(self, eye, target, settle: float = 0.0):
        """Place the camera at `eye` (x,y,z) aimed at world point `target`.

        Computes the yaw + pitch from the eye→target vector — frame any subject
        by position instead of guessing angles.
        """
        import math
        dx, dy, dz = (target[0] - eye[0], target[1] - eye[1], target[2] - eye[2])
        yaw = math.degrees(math.atan2(dy, dx))
        dist = math.hypot(dx, dy)
        pitch = -math.degrees(math.atan2(dz, dist)) if dist or dz else 0.0
        self.setviewpos(eye[0], eye[1], eye[2], yaw, pitch)
        if settle:
            time.sleep(settle)
        return {"eye": list(eye), "yaw": round(yaw, 1), "pitch": round(pitch, 1)}

    def noclip(self):
        """Toggle noclip (cheat) — free-fly the camera to compose a shot."""
        self.command("noclip")

    def dolly(self, path, frames: int | None = None, interval: float = 0.25,
              look: bool = True, out: str | None = None, title: str | None = None,
              fps: int | None = None, clean: bool = True) -> dict:
        """Move a free camera along a path of waypoints, capturing a moving clip.

        path: list of (x,y,z) eye points, or (x,y,z, tx,ty,tz) eye+look-at points.
        Frames are interpolated across the waypoints; with `look`+look-at points
        the camera tracks the target through the move. Returns a collage.
        """
        pts = [tuple(p) for p in path]
        if len(pts) < 2:
            raise EngineError("dolly needs at least 2 waypoints")
        frames = frames or max(len(pts), 8)
        if clean:
            self.cinematic(True)
            time.sleep(0.2)

        def lerp(a, b, t):
            return [a[i] + (b[i] - a[i]) * t for i in range(len(a))]

        def hook(i):
            u = i / (frames - 1) if frames > 1 else 0.0
            seg = u * (len(pts) - 1)
            k = min(int(seg), len(pts) - 2)
            wp = lerp(pts[k], pts[k + 1], seg - k)
            if look and len(wp) >= 6:
                self.look_at(wp[0:3], wp[3:6])
            else:
                self.setviewpos(wp[0], wp[1], wp[2], wp[3] if len(wp) > 3 else 0)
            time.sleep(0.05)
        try:
            paths = self.capture_frames(frames, interval, hook=hook)
        finally:
            if clean:
                self.cinematic(False)
        sheet = self.collage(paths, out=out, title=title,
                             labels=[f"{i}" for i in range(len(paths))])
        res = {"collage": sheet, "frames": paths}
        if fps:
            res["video"] = self.make_video(paths, fps=fps)
        return res

    # -- world state + auto-composed shots (point & shoot) -------------------

    def state(self, timeout: float = 4.0) -> dict:
        """Read the live world state: every connected client's origin/angles/
        health/bot flag, plus the current camera ('self'). Lets shots be framed
        by name/position instead of guessing coordinates. Needs the rebuilt
        qagame + a local client (client mode / listen server)."""
        self._read_new()
        self.command("apistate")
        buf, deadline = "", time.time() + timeout
        while time.time() < deadline:
            buf += self._read_new()
            clean = strip_colors(buf)
            if "@@APISTATE_END@@" in clean:
                body = clean.split("@@APISTATE_BEGIN@@", 1)[-1].split("@@APISTATE_END@@", 1)[0]
                clients, me = [], None
                for line in body.splitlines():
                    line = line.strip()
                    if not line.startswith("{"):
                        continue
                    try:
                        o = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if "self" in o:
                        me = o
                    elif "num" in o:
                        clients.append(o)
                return {"clients": clients, "self": me}
            if not self.alive:
                raise EngineError("engine exited")
            time.sleep(0.03)
        raise EngineError("no apistate reply (needs the rebuilt qagame + client mode)")

    def measure(self, subject="self", seconds: float = 4.0, hz: int = 10,
                field: str = "speed") -> dict:
        """Sample a subject's `field` (speed, health, air, wj, dj) over `seconds`
        → metrics. Quantifies a run (bhop chain, slide-jump, surf line) or moveset
        usage so movement can be judged by the numbers, not by eye. subject:
        'self' (the player you're driving), 'follow' (the bot you're spectating),
        or a client name/number."""
        interval = 1.0 / max(1, hz)
        samples, t0 = [], time.time()
        while time.time() - t0 < seconds:
            st = self.state()
            me = st.get("self") or {}
            want = (me.get("self") if subject in ("self", "me")
                    else me.get("following") if subject in ("follow", "followed")
                    else None)
            v = None
            for c in st["clients"]:
                if (want is not None and c["num"] == want) or \
                   str(c["num"]) == str(subject) or c.get("name", "").lower() == str(subject).lower():
                    v = c.get(field)
                    break
            if isinstance(v, (int, float)):
                samples.append((round(time.time() - t0, 2), v))
            time.sleep(interval)
        vals = [v for _, v in samples]
        if not vals:
            return {"subject": subject, "field": field, "samples": 0,
                    "note": "no data — drive the player (play_mode) or follow a bot first"}
        mx = max(vals)
        peak_at = next(t for t, v in samples if v == mx)
        return {"subject": subject, "field": field, "samples": len(vals),
                "max": round(mx), "avg": round(sum(vals) / len(vals), 1),
                "final": round(vals[-1]), "peak_at": peak_at,
                # back-compat aliases for the common speed case
                "max_speed": round(mx), "avg_speed": round(sum(vals) / len(vals)),
                "final_speed": round(vals[-1]),
                "series": [[t, round(v)] for t, v in samples[:80]]}

    def _barchart(self, title, labels, values, out=None, unit="", w=620, h=320):
        from PIL import Image, ImageDraw
        img = Image.new("RGB", (w, h), (14, 14, 18))
        d = ImageDraw.Draw(img)
        d.text((10, 8), title, fill=(220, 220, 235))
        n = max(1, len(values))
        mx = max(values + [1])
        pad, top, bot = 50, 40, 40
        bw = (w - 2 * pad) / n
        for i, (lab, v) in enumerate(zip(labels, values)):
            x0 = pad + i * bw + bw * 0.15
            x1 = pad + (i + 1) * bw - bw * 0.15
            bh = (h - top - bot) * (v / mx)
            y1 = h - bot
            y0 = y1 - bh
            d.rectangle([x0, y0, x1, y1], fill=(90, 200, 255))
            d.text(((x0 + x1) / 2 - 12, y0 - 14), f"{round(v)}{unit}", fill=(220, 230, 245))
            d.text(((x0 + x1) / 2 - 18, y1 + 6), str(lab), fill=(180, 180, 195))
        out = out or str(self._clips_dir() / "compare.jpg")
        img.save(out, quality=92)
        return out

    def compare(self, cvar: str, values: list, mode: str = "visual",
                subject=None, actions=None, seconds: float = 2.5, hz: int = 8,
                dist: float = 240, trials: int = 3,
                out: str | None = None, title: str | None = None) -> dict:
        """Sweep a cvar across `values` and return a comparison so the LLM can
        decide in one call.

        mode='visual': frame `subject` (or use the current view), set each value,
        screenshot → one side-by-side collage labelled by value.
        mode='movement': for each value, respawn + run `actions` (default: run
        forward) while measuring speed → a bar chart of max speed + a metrics
        table. Quantitatively compares how a cvar changes the movement.
        """
        import threading
        values = list(values)
        if mode == "movement":
            rows = []
            # default exercises the real speed-building tech (air-strafe + bhop),
            # so air-strafe params actually move the numbers — not a plain run.
            acts = actions or [{"strafe": seconds, "side": 1}]
            for v in values:
                self.set_cvar(cvar, v, confirm=False)
                # scripted air-strafe is noisy run-to-run (geometry, timing) — run
                # several trials and take the MEDIAN so one fluke doesn't mislead.
                peaks = []
                for _ in range(max(1, trials)):
                    self.play_mode()
                    time.sleep(0.4)
                    th = threading.Thread(target=self.run_actions, args=(acts,))
                    th.start()
                    m = self.measure(subject="self", seconds=seconds, hz=hz)
                    th.join(timeout=2)
                    if m.get("max_speed"):
                        peaks.append(m["max_speed"])
                peaks.sort()
                med = peaks[len(peaks) // 2] if peaks else 0
                rows.append({"value": v, "max_speed": med, "trials": peaks,
                             "spread": [min(peaks), max(peaks)] if peaks else None})
            chart = self._barchart(title or f"{cvar} → median peak speed ({trials} trials)",
                                   [r["value"] for r in rows],
                                   [r.get("max_speed") or 0 for r in rows],
                                   out=out, unit="u/s")
            return {"chart": chart, "cvar": cvar, "mode": "movement", "results": rows}
        # visual
        self.camera_mode()
        if subject not in (None, "self", "current", "view", "camera"):
            try:
                self.frame(subject, dist=dist)
            except EngineError:
                pass   # nothing to frame → just sweep on the current view
        self.cinematic(True); time.sleep(0.2)
        shots, applied = [], []
        for v in values:
            got = self.set_effect(cvar, v) if cvar in EFFECT_CVARS else self.set_cvar(cvar, v)
            applied.append(got)
            time.sleep(0.4)
            p = self.screenshot()
            if p:
                shots.append(p)
        self.cinematic(False)
        sheet = self.collage(shots, out=out, title=title or f"{cvar} sweep",
                             labels=[f"{cvar}={v}" for v in values[:len(shots)]])
        return {"collage": sheet, "cvar": cvar, "mode": "visual",
                "values": values, "frames": shots}

    def subjects(self, include_spectators: bool = False) -> list[dict]:
        """The framable subjects right now (players/bots), with positions."""
        st = self.state()
        return [c for c in st["clients"]
                if include_spectators or not c.get("spectator")]

    def map_bounds(self) -> dict | None:
        """The current map's true world extent (from the .bsp MODELS lump, model
        0) — so a course can be framed by its real geometry, not by where players
        happen to be. Works for loose-bsp maps (generated/deployed); returns None
        for pk3-only maps."""
        import struct
        name = self.map or self.get_cvar("mapname")
        if not name:
            return None
        for base in (self._gamedir, Path(self.oa) / GAME):
            bsp = base / "maps" / f"{name}.bsp"
            if bsp.exists():
                try:
                    with open(bsp, "rb") as f:
                        head = f.read(8 + 17 * 8)
                        if head[:4] != b"IBSP":
                            return None
                        off, _ = struct.unpack_from("<ii", head, 8 + 7 * 8)  # lump 7 = models
                        f.seek(off)
                        mins = list(struct.unpack("<3f", f.read(12)))
                        maxs = list(struct.unpack("<3f", f.read(12)))
                    return {"mins": [round(v) for v in mins], "maxs": [round(v) for v in maxs],
                            "center": [round((mins[i] + maxs[i]) / 2) for i in range(3)],
                            "size": [round(maxs[i] - mins[i]) for i in range(3)]}
                except (OSError, struct.error):
                    return None
        return None

    def overview(self) -> dict:
        """One-call orientation: what map, who's present (positions/speed/health),
        and the current camera — so the LLM gets situated without a flurry of
        separate queries."""
        out = {"map": self.map, "mode": self.mode, "name": self.name,
               "alive": self.alive}
        try:
            st = self.state()
            out["clients"] = st["clients"]
            out["camera"] = st.get("self")
        except EngineError as e:
            out["state_error"] = str(e)
        return out

    def _resolve_target(self, subject, st: dict | None = None):
        """Resolve a subject → a world point. subject may be a client name or
        number, a literal [x,y,z], None/'action' (centroid of the players), or
        'self' (the camera)."""
        st = st or self.state()
        live = [c for c in st["clients"] if not c.get("spectator")]
        if isinstance(subject, (list, tuple)) and len(subject) == 3:
            return [float(v) for v in subject], None
        if subject in (None, "action", "auto", "all"):
            pts = [c["origin"] for c in live] or [c["origin"] for c in st["clients"]]
            if not pts:
                raise EngineError("no players to frame")
            n = len(pts)
            return [sum(p[i] for p in pts) / n for i in range(3)], pts
        key = str(subject).lower()
        for c in st["clients"]:
            if str(c["num"]) == key or c.get("name", "").lower() == key:
                return list(c["origin"]), [c["origin"]]
        raise EngineError(f"no subject '{subject}' — have "
                          f"{[c.get('name') for c in st['clients']]}")

    def _room_box(self, st: dict):
        """Approximate the playable volume from where the players are (they're
        always inside it) — used to keep the camera from ending up outside the
        geometry (which renders as fullbright void)."""
        # only live players bound the room — the free camera (a spectator) flies
        # around and would balloon the box, defeating the clamp.
        pts = [c["origin"] for c in st["clients"] if not c.get("spectator")] \
            or [c["origin"] for c in st["clients"]] or [[0, 0, 0]]
        lo = [min(p[i] for p in pts) for i in range(3)]
        hi = [max(p[i] for p in pts) for i in range(3)]
        return lo, hi

    def _clamp_to_room(self, eye, st: dict):
        lo, hi = self._room_box(st)
        # horizontal: stay within the player spread + a margin so we don't poke
        # through walls; vertical: from just above the lowest player up to a
        # modest ceiling estimate above the highest.
        mx = max(220.0, (hi[0] - lo[0]) * 0.5)
        my = max(220.0, (hi[1] - lo[1]) * 0.5)
        return [
            min(max(eye[0], lo[0] - mx), hi[0] + mx),
            min(max(eye[1], lo[1] - my), hi[1] + my),
            min(max(eye[2], lo[2] + 16), hi[2] + 280),
        ]

    def camera_mode(self):
        """Put the view into a free-floating spectator camera (no model, no
        gravity) with the HUD/notify hidden — the basis for clean composed
        shots. Idempotent-ish."""
        self.command("team spectator")
        self.cinematic(True)
        time.sleep(0.35)

    def frame(self, subject=None, dist: float = 240, height: float = 110,
              yaw: float = 35, settle: float = 0.4) -> dict:
        """Auto-compose: place the free camera to frame `subject` and aim at it.

        `yaw` is the orbit bearing around the subject (deg); dist/height set the
        standoff. The LLM names *what* to see; the coordinates + look angles are
        computed here. Returns where it placed the camera.
        """
        import math
        st = self.state()
        target, pts = self._resolve_target(subject, st)
        # auto-pull-back so a spread-out group still fits
        if pts and len(pts) > 1:
            spread = max(math.dist(target[:2], p[:2]) for p in pts)
            dist = max(dist, spread * 1.6 + 160)
            height = max(height, spread * 0.6)
        rad = math.radians(yaw)
        eye = [target[0] - dist * math.cos(rad),
               target[1] - dist * math.sin(rad),
               target[2] + height]
        eye = self._clamp_to_room(eye, st)   # keep the camera inside the geometry
        info = self.look_at(eye, target)
        if settle:
            time.sleep(settle)
        return {"subject": subject, "target": [round(v) for v in target],
                "dist": round(dist), "height": round(height), **info}

    def establishing_shot(self, height: float = 700, margin: float = 1.4,
                          radius: float | None = None, settle: float = 0.4) -> dict:
        """A wide shot that fits the players in frame — auto-placed above and back
        from their bounding box. For an empty course (no bots) pass `radius` to
        pull the camera back far enough to take in the whole level, since there
        are no players to size the shot from."""
        import math
        st = self.state()
        pts = [c["origin"] for c in st["clients"] if not c.get("spectator")] or \
              [c["origin"] for c in st["clients"]]
        if not pts:
            raise EngineError("no players to establish on")
        cx = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
        # NOTE: bsp model-0 bounds (map_bounds()) include the sky/void dome and
        # are far larger than the playable area, so framing them puts the camera
        # uselessly far away. We frame from the players' spread, or an explicit
        # `radius` for an empty course — not the raw map bounds.
        span = (radius if radius is not None
                else max((max(p[i] for p in pts) - min(p[i] for p in pts))
                         for i in range(2)) or 256)
        back = span * margin + 256
        eye = [cx[0] - back, cx[1], cx[2] + max(height, span * 0.8)]
        if radius is None:
            eye = self._clamp_to_room(eye, st)
        info = self.look_at(eye, cx)
        if settle:
            time.sleep(settle)
        return {"target": [round(v) for v in cx], "span": round(span),
                "source": "radius" if radius else "players", **info}

    def orbit_shot(self, subject=None, angles=None, dist: float = 260,
                   height: float = 120, freeze: bool = True, out: str | None = None,
                   title: str | None = None, clean: bool = True) -> dict:
        """Turntable around a subject by NAME/position — the camera positions are
        computed from the subject's live origin, so no coordinates to guess."""
        import math
        angles = angles or [0, 45, 90, 135, 180, 225, 270, 315]
        st = self.state()
        target, _ = self._resolve_target(subject, st)
        if clean:
            self.cinematic(True); time.sleep(0.2)
        saved_ts = None
        if freeze:
            saved_ts = self.get_cvar("timescale")
            self.set_cvar("timescale", 0.02, confirm=False); time.sleep(0.2)
        try:
            def hook(i):
                rad = math.radians(angles[i])
                eye = [target[0] - dist * math.cos(rad),
                       target[1] - dist * math.sin(rad), target[2] + height]
                self.look_at(self._clamp_to_room(eye, st), target); time.sleep(0.12)
            paths = self.capture_frames(len(angles), interval=0.12, hook=hook)
        finally:
            if freeze and saved_ts is not None:
                self.set_cvar("timescale", saved_ts, confirm=False)
            if clean:
                self.cinematic(False)
        sheet = self.collage(paths, out=out, cols=max(1, len(angles) // 2),
                             title=title or f"orbit: {subject}",
                             labels=[f"{int(a)}°" for a in angles[:len(paths)]])
        return {"collage": sheet, "frames": paths, "target": [round(v) for v in target]}

    def give(self, what: str = "all"):
        """give all | give weapon <n> | give health … (cheat)."""
        self.command(f"give {what}")

    def third_person(self, on: bool = True, range: float = 80, angle: float = 0):
        self.set_cvar("cg_thirdPerson", 1 if on else 0, confirm=False)
        if on:
            self.set_cvar("cg_thirdPersonRange", range, confirm=False)
            self.set_cvar("cg_thirdPersonAngle", angle, confirm=False)

    def audition_model(self, model: str, headmodel: str | None = None,
                       weapon: int | None = None, settle: float = 3.8):
        """Swap to a player model and frame it in third person to inspect it.

        `model` is a Q3 model path like 'sarge' or 'sarge/red'. Forces the model,
        respawns so it loads, and switches to third person. Optionally gives +
        selects a weapon so its model shows on the player.
        """
        if not self.cheats:
            raise EngineError("audition needs cheats (launch client mode / devmap)")
        self.set_cvar("cg_forceModel", 1, confirm=False)
        self.set_cvar("model", model, confirm=False)
        if headmodel:
            self.set_cvar("headmodel", headmodel, confirm=False)
        # auto-respawn: default g_forceRespawn 0 leaves you dead on the
        # scoreboard until +attack — force it so the new model loads cleanly.
        self.set_cvar("g_forceRespawn", 1, confirm=False)
        # the bullet-time core near-freezes game time when the player is still
        # (g_timeBindMin), which stretches the respawn timer to tens of real
        # seconds while dead. Disable it so audition runs at normal speed.
        self.set_cvar("g_timeBind", 0, confirm=False)
        self.third_person(True)
        self.command("kill")          # respawn so the new model loads
        time.sleep(min(settle, 2.5))
        # belt-and-braces: pulse fire to clear any lingering respawn prompt
        self.command("+attack")
        time.sleep(0.1)
        self.command("-attack")
        self.give("all")
        if weapon is not None:
            self.command(f"weapon {int(weapon)}")
        time.sleep(max(0.0, settle - 2.6))
        return {"model": self.get_cvar("model"), "headmodel": self.get_cvar("headmodel")}

    # -- effects / look ------------------------------------------------------

    def get_effects(self) -> dict:
        out = {}
        for name, meta in EFFECT_CVARS.items():
            out[name] = dict(value=self.get_cvar(name), **meta)
        return out

    def set_effect(self, name: str, value, clamp: bool = True, restart: bool | None = None):
        if name not in EFFECT_CVARS:
            raise EngineError(f"{name} is not a known effect cvar; "
                              f"known: {sorted(EFFECT_CVARS)}")
        lo, hi = EFFECT_CVARS[name]["range"]
        v = float(value)
        if clamp:
            v = max(lo, min(hi, v))
        self.command(f'set {name} "{v}"')
        if restart is None:
            restart = EFFECT_CVARS[name]["restart"]
        if restart:
            self.vid_restart()
        return self.get_cvar(name)

    # -- tuning presets (checkpoint / restore a feel) ------------------------

    def _presets_dir(self) -> Path:
        d = Path(self.oa) / GAME / "presets"
        d.mkdir(parents=True, exist_ok=True)
        return d

    def save_preset(self, name: str, cvars: list | None = None) -> dict:
        """Snapshot the current movement + effect cvars to a named .cfg so a
        tuning 'feel' can be restored or A/B'd later. Persists under the base
        game dir, so any instance/session can load it."""
        names = cvars or (list(MOVEMENT_CVARS) + list(EFFECT_CVARS))
        lines = [f"// strafe64 tuning preset: {name}"]
        saved = {}
        for cv in names:
            v = self.get_cvar(cv)
            if v is not None:
                lines.append(f'set {cv} "{v}"')
                saved[cv] = v
        path = self._presets_dir() / f"{name}.cfg"
        path.write_text("\n".join(lines) + "\n")
        return {"preset": name, "path": str(path), "cvars": saved}

    def load_preset(self, name: str) -> dict:
        """Apply a saved preset (exec its .cfg)."""
        self.command(f"exec presets/{name}.cfg")
        time.sleep(0.3)
        return {"loaded": name}

    def list_presets(self) -> list[str]:
        d = Path(self.oa) / GAME / "presets"
        return sorted(p.stem for p in d.glob("*.cfg")) if d.exists() else []

    def vid_restart(self):
        """Reinitialise the renderer so latched r_* cvars take effect."""
        self.command("vid_restart")
        self.wait_for(r"RE_RegisterFont|Com_TouchMemory|CL_InitCGame|----",
                      timeout=30, soft=True)

    def screenshot_bytes(self, jpeg: bool = True) -> tuple[str | None, bytes | None]:
        """Take a screenshot and return (path, raw bytes) for inspection."""
        path = self.screenshot(jpeg=jpeg)
        if path and Path(path).exists():
            return path, Path(path).read_bytes()
        return path, None

    # -- spectator / follow (filming bots) -----------------------------------

    def spectate(self, target=None, third_person: bool = True, range: float = 130,
                 angle: float = 25, settle: float = 0.8):
        """Become a spectator and follow a bot/player to film their gameplay.

        target: client name or number to follow; None cycles to the next one.
        third person + an orbit angle gives a chase-cam over the bot.
        """
        self.command("team spectator")
        time.sleep(0.4)
        if target is None:
            self.command("follownext")
        else:
            self.command(f"follow {target}")
        time.sleep(settle)
        if third_person:
            self.third_person(True, range=range, angle=angle)
        return {"following": target if target is not None else "next"}

    def follow_next(self):
        self.command("follownext")

    def follow_prev(self):
        self.command("followprev")

    def orbit(self, angle: float, range: float | None = None):
        """Rotate the third-person chase camera around the subject (cheat)."""
        self.set_cvar("cg_thirdPersonAngle", angle % 360, confirm=False)
        if range is not None:
            self.set_cvar("cg_thirdPersonRange", range, confirm=False)

    # -- multi-frame capture + collage ---------------------------------------

    def _clips_dir(self) -> Path:
        d = self._gamedir / "clips"
        d.mkdir(parents=True, exist_ok=True)
        return d

    # -- player interaction (drive the character) ----------------------------

    INPUT_BUTTONS = ("forward", "back", "moveleft", "moveright", "moveup",
                     "movedown", "attack", "speed", "left", "right",
                     "lookup", "lookdown")

    def play_mode(self, weapon: int | None = None, first_person: bool = True):
        """Leave spectator and become a live, controllable player at normal time
        speed (so interaction isn't slowed by the bullet-time core)."""
        self.set_cvar("g_forceRespawn", 1, confirm=False)
        self.set_cvar("g_timeBind", 0, confirm=False)
        self.command("team free")
        time.sleep(0.7)
        self.tap("attack", 0.1)        # respawn if waiting on the fire-to-spawn prompt
        time.sleep(0.6)
        self.give("all")
        if weapon is not None:
            self.command(f"weapon {int(weapon)}")
        if first_person:
            self.third_person(False)
        self.cinematic(False)   # restore the HUD for interactive play

    def hold(self, button: str):
        self.command("+" + button)

    def release(self, button: str):
        self.command("-" + button)

    def tap(self, button: str, secs: float = 0.2):
        self.hold(button)
        time.sleep(max(0.0, secs))
        self.release(button)

    def release_all(self):
        for b in self.INPUT_BUTTONS:
            self.release(b)

    def move(self, forward: int = 0, side: int = 0, secs: float = 0.6, jump: bool = False):
        """Walk/run the player: forward/back (±1), strafe right/left (±1), for
        `secs` seconds; optionally jump during the move."""
        held = []
        if forward > 0: held.append("forward")
        elif forward < 0: held.append("back")
        if side > 0: held.append("moveright")
        elif side < 0: held.append("moveleft")
        for b in held:
            self.hold(b)
        if jump:
            self.hold("moveup")
        try:
            time.sleep(max(0.0, secs))
        finally:
            for b in held:
                self.release(b)
            if jump:
                self.release("moveup")

    def jump(self, times: int = 1):
        for _ in range(max(1, times)):
            self.tap("moveup", 0.08)
            time.sleep(0.25)

    def strafe_jump(self, secs: float = 3.0, side: int = 1, jump: bool = True):
        """Build speed the way the game intends — air-strafing: hold forward + a
        strafe direction + a matching continuous turn (the CPM speed-gain curve),
        while *pulsing* jump for bhop rehops (held jump doesn't rehop and just
        runs at the ground cap — pulsing reaches ~411 vs 320). This exercises
        pm_strafeAccelerate / pm_airControlAmount / pm_wishSpeedClamp / bhop boost,
        which plain forward-running cannot — so measure/compare can actually tune
        the core movement tech."""
        sd = "moveright" if side >= 0 else "moveleft"
        tn = "right" if side >= 0 else "left"
        held = ["forward", sd, tn]
        for b in held:
            self.hold(b)
        try:
            t0 = time.time()
            while time.time() - t0 < secs:
                if jump:
                    self.command("+moveup"); time.sleep(0.05)
                    self.command("-moveup"); time.sleep(0.08)
                else:
                    time.sleep(0.1)
        finally:
            self.release("moveup")
            for b in held:
                self.release(b)

    def attack(self, secs: float = 0.3):
        """Fire / swing for `secs` (a sword swing also surges time-bind)."""
        self.tap("attack", secs)

    def use_item(self, item: str | None = None):
        self.command(f"use {item}" if item else "+button2")
        if not item:
            time.sleep(0.1); self.release("button2")

    def spawn(self, classname: str, **keys):
        """Spawn any entity into the live world (cheat) — ~96u in front of the
        view unless an `origin="x y z"` key is given. Dress/edit a running map:
        items (item_health, weapon_rocketlauncher, item_armor_body), props
        (misc_model with model="..."), jump pads (trigger_push / target_position),
        lights, etc. keys become entity spawn keys."""
        parts = [f"spawnent {classname}"]
        for k, v in keys.items():
            parts.append(f'{k} "{v}"')
        self.command(" ".join(parts))
        time.sleep(0.2)
        return {"classname": classname, "keys": keys}

    def clear_spawns(self, classname: str | None = None) -> dict:
        """Remove entities spawned live via spawn() (optionally just one
        classname), leaving the map's own entities intact — iterate on dressing."""
        self.command(f"clearents {classname}" if classname else "clearents")
        out = self.wait_for(r"cleared \d+ spawned", timeout=3, soft=True)
        m = re.search(r"cleared (\d+)", strip_colors(out))
        return {"cleared": int(m.group(1)) if m else None}

    def turn(self, yaw: float = 0, pitch: float = 0, speed: float = 140.0):
        """Turn the live player by relative degrees (pulses the look buttons).
        +yaw turns left, +pitch looks up. Approximate (button-timed)."""
        if yaw:
            self.tap("left" if yaw > 0 else "right", abs(yaw) / speed)
        if pitch:
            self.tap("lookup" if pitch > 0 else "lookdown", abs(pitch) / speed)

    def run_actions(self, actions) -> list:
        """Execute a sequence of interaction steps (the engine_input format):
        {"move":[fwd,side],"secs":s,"jump":bool} | {"turn":[yaw,pitch]} |
        {"attack":secs} | {"jump":n} | {"use":item} | {"wait":secs}."""
        done = []
        for a in (actions or []):
            if "move" in a:
                mv = a["move"]
                self.move(forward=mv[0], side=(mv[1] if len(mv) > 1 else 0),
                          secs=float(a.get("secs", 0.6)), jump=bool(a.get("jump")))
                done.append(f"move{mv}")
            elif "turn" in a:
                tn = a["turn"]
                self.turn(yaw=tn[0], pitch=(tn[1] if len(tn) > 1 else 0))
                done.append(f"turn{tn}")
            elif "attack" in a:
                self.attack(float(a["attack"])); done.append("attack")
            elif "jump" in a:
                self.jump(int(a["jump"])); done.append("jump")
            elif "strafe" in a:
                self.strafe_jump(secs=float(a["strafe"]), side=int(a.get("side", 1)))
                done.append("strafe")
            elif "use" in a:
                self.use_item(a["use"]); done.append(f"use {a['use']}")
            elif "wait" in a:
                time.sleep(float(a["wait"])); done.append(f"wait {a['wait']}")
        return done

    def cinematic(self, on: bool = True):
        """Hide the 2D HUD / spectator text AND the console notify lines (the
        'Wrote screenshots…' echoes) for clean footage frames."""
        self.set_cvar("cg_draw2D", 0 if on else 1, confirm=False)
        self.set_cvar("con_notifytime", -1 if on else 3, confirm=False)

    def capture_frames(self, count: int = 9, interval: float = 0.4,
                       hook=None) -> list[str]:
        """Snap `count` screenshots spaced by `interval` seconds.

        `hook(i)` (optional) runs before each frame — use it to change the
        camera angle, follow target, or effect per frame.
        """
        paths = []
        for i in range(int(count)):
            if hook:
                hook(i)
            p = self.screenshot(jpeg=True)
            if p:
                paths.append(p)
            if i < count - 1:
                time.sleep(interval)
        return paths

    def collage(self, paths: list[str], out: str | None = None, cols: int | None = None,
                thumb_w: int = 480, gap: int = 6, labels: list[str] | None = None,
                bg=(12, 12, 16), title: str | None = None) -> str:
        """Bake frames into a single contact-sheet image (grid)."""
        from PIL import Image, ImageDraw
        import math
        if not paths:
            raise EngineError("no frames to collage")
        imgs = [Image.open(p).convert("RGB") for p in paths]
        n = len(imgs)
        cols = cols or max(1, round(math.sqrt(n)))
        rows = math.ceil(n / cols)
        # uniform thumbnails preserving aspect ratio (use the first frame's)
        ar = imgs[0].height / imgs[0].width
        tw, th = thumb_w, int(thumb_w * ar)
        thumbs = [im.resize((tw, th), Image.LANCZOS) for im in imgs]
        top = 28 if title else 0
        W = cols * tw + (cols + 1) * gap
        H = top + rows * th + (rows + 1) * gap
        sheet = Image.new("RGB", (W, H), bg)
        draw = ImageDraw.Draw(sheet)
        if title:
            draw.text((gap, 8), title, fill=(220, 220, 230))
        for i, t in enumerate(thumbs):
            r, c = divmod(i, cols)
            x = gap + c * (tw + gap)
            y = top + gap + r * (th + gap)
            sheet.paste(t, (x, y))
            if labels and i < len(labels) and labels[i]:
                draw.rectangle([x, y, x + 7 * len(labels[i]) + 8, y + 16], fill=(0, 0, 0))
                draw.text((x + 4, y + 3), labels[i], fill=(120, 230, 255))
        out = out or str(self._clips_dir() / "collage.jpg")
        sheet.save(out, quality=90)
        return out

    def make_video(self, paths: list[str], out: str | None = None, fps: int = 10) -> str | None:
        """Stitch frames into an mp4 via ffmpeg (if available)."""
        if not paths:
            return None
        ff = shutil.which("ffmpeg")
        if not ff:
            return None
        listfile = self._clips_dir() / "frames.txt"
        listfile.write_text("".join(
            f"file '{p}'\nduration {1.0/fps:.4f}\n" for p in paths) +
            f"file '{paths[-1]}'\n")
        out = out or str(self._clips_dir() / "clip.mp4")
        subprocess.run([ff, "-y", "-f", "concat", "-safe", "0", "-i", str(listfile),
                        "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2", "-pix_fmt", "yuv420p",
                        str(out)], capture_output=True)
        return out if Path(out).exists() else None

    # -- demos: record / replay real gameplay --------------------------------

    def _demos_dir(self) -> Path:
        d = self._gamedir / "demos"
        d.mkdir(parents=True, exist_ok=True)
        return d

    def record_demo(self, name: str):
        """Start recording a real gameplay demo (client mode). Smooth full-motion
        replay — a shareable artifact and the path to real video."""
        self.command(f"record {name}")
        self.wait_for(r"recording|started", timeout=4, soft=True)
        return {"recording": name}

    def stop_demo(self) -> dict:
        """Stop recording; return the demo file written."""
        before = set(self._demos_dir().glob("*.dm_*"))
        self.command("stoprecord")
        deadline = time.time() + 4
        while time.time() < deadline:
            new = set(self._demos_dir().glob("*.dm_*")) - before
            if new:
                p = sorted(new, key=lambda f: f.stat().st_mtime)[-1]
                return {"demo": str(p), "bytes": p.stat().st_size}
            time.sleep(0.1)
        latest = max(self._demos_dir().glob("*.dm_*"),
                     key=lambda f: f.stat().st_mtime, default=None)
        return {"demo": str(latest) if latest else None,
                "bytes": latest.stat().st_size if latest else 0}

    def clip_demo(self, seconds: float = 6.0, name: str = "clip") -> dict:
        """Record `seconds` of live gameplay into a demo (whatever's happening —
        bots playing, or you driving). Returns the demo file."""
        self.record_demo(name)
        time.sleep(seconds)
        return self.stop_demo()

    def play_demo(self, name: str):
        """Play back a recorded demo."""
        self.command(f"demo {name}")
        self.wait_for(r"playing|Demo|spawn", timeout=10, soft=True)
        return {"playing": name}

    def render_demo(self, name: str, fps: int = 25, timeout: float = 60) -> dict:
        """Render a recorded demo to a real video via the engine's framebuffer
        capture (smooth full motion), then convert to mp4. The proper 'capture a
        clip' path. Plays the demo back, captures with `video`, and detects the
        end by watching the AVI stop growing."""
        videos = self._gamedir / "videos"
        videos.mkdir(parents=True, exist_ok=True)
        avi = videos / f"{name}.avi"
        if avi.exists():
            avi.unlink()
        self.set_cvar("cl_aviFrameRate", fps, confirm=False)
        self.play_demo(name)
        time.sleep(0.4)
        self.command(f"video {name}")        # only valid during demo playback
        # wait for the avi to appear then stop growing (demo finished)
        last, stable, deadline = -1, 0, time.time() + timeout
        while time.time() < deadline and not avi.exists():
            time.sleep(0.1)
        while time.time() < deadline:
            sz = avi.stat().st_size if avi.exists() else 0
            stable = stable + 1 if (sz == last and sz > 0) else 0
            last = sz
            if stable >= 8:                  # ~0.8s no growth
                break
            time.sleep(0.1)
        self.command("stopvideo")
        time.sleep(0.4)
        out = avi
        ff = shutil.which("ffmpeg")
        if avi.exists() and ff:
            mp4 = videos / f"{name}.mp4"
            subprocess.run([ff, "-y", "-i", str(avi), "-pix_fmt", "yuv420p", str(mp4)],
                           capture_output=True)
            if mp4.exists():
                out = mp4
        return {"video": str(out) if out.exists() else None, "fps": fps,
                "bytes": out.stat().st_size if out.exists() else 0}

    def clip_video(self, seconds: float = 6.0, name: str = "clip",
                   clean: bool = True) -> dict:
        """Capture a real-time mp4 of the current view: grabs frames as fast as
        possible for `seconds`, measures the achieved rate, and stitches at that
        fps so playback is real-time. Reliable (vs. demo-AVI, which is sparse on
        listen servers). Spectate a bot or drive the player first to choose what
        it films."""
        if clean:
            self.cinematic(True); time.sleep(0.2)
        paths, t0 = [], time.time()
        try:
            while time.time() - t0 < seconds:
                p = self.screenshot(jpeg=True)
                if p:
                    paths.append(p)
        finally:
            if clean:
                self.cinematic(False)
        elapsed = max(0.1, time.time() - t0)
        fps = max(1, round(len(paths) / elapsed))
        out = self.make_video(paths, out=str(self._clips_dir() / f"{name}.mp4"), fps=fps)
        return {"video": out, "frames": len(paths), "fps": fps,
                "seconds": round(elapsed, 1),
                "bytes": Path(out).stat().st_size if out and Path(out).exists() else 0}

    def cinematic_clip(self, subject=None, seconds: float = 5.0, slowmo: float = 0.3,
                       orbit_deg: float = 140, dist: float = 260, height: float = 130,
                       name: str = "cinematic") -> dict:
        """A bullet-time hero shot: ramp time into slow-mo and orbit the camera
        around a subject while filming — the Matrix-rotate that fits the sword /
        time-bind game. Returns an mp4. subject = a bot/player name, [x,y,z], or
        'action' (the players' centroid)."""
        import math
        st = self.state()
        target, _ = self._resolve_target(subject, st)
        self.camera_mode()
        saved_ts = self.get_cvar("timescale")
        self.set_cvar("timescale", max(0.05, min(1.0, slowmo)), confirm=False)
        self.cinematic(True)
        time.sleep(0.2)
        paths, t0 = [], time.time()
        try:
            while time.time() - t0 < seconds:
                u = (time.time() - t0) / max(0.1, seconds)
                ang = math.radians(orbit_deg * u)
                eye = [target[0] - dist * math.cos(ang),
                       target[1] - dist * math.sin(ang), target[2] + height]
                self.look_at(self._clamp_to_room(eye, st), target)
                p = self.screenshot()
                if p:
                    paths.append(p)
        finally:
            self.set_cvar("timescale", saved_ts or 1, confirm=False)
            self.cinematic(False)
        elapsed = max(0.1, time.time() - t0)
        fps = max(1, round(len(paths) / elapsed))
        out = self.make_video(paths, out=str(self._clips_dir() / f"{name}.mp4"), fps=fps)
        return {"video": out, "frames": len(paths), "fps": fps, "slowmo": slowmo,
                "orbit_deg": orbit_deg, "target": [round(v) for v in target],
                "bytes": Path(out).stat().st_size if out and Path(out).exists() else 0}

    def clip_collage(self, frames: int = 9, interval: float = 0.4, cols: int = 3,
                     out: str | None = None, title: str | None = None,
                     fps: int | None = None, clean: bool = True) -> dict:
        """Film the current view over time and bake the frames into a collage."""
        if clean:
            self.cinematic(True)
            time.sleep(0.2)   # let cg_draw2D 0 take effect before the first frame
        try:
            paths = self.capture_frames(frames, interval)
        finally:
            if clean:
                self.cinematic(False)
        sheet = self.collage(paths, out=out, cols=cols, title=title,
                             labels=[f"{i*interval:.1f}s" for i in range(len(paths))])
        res = {"collage": sheet, "frames": paths}
        if fps:
            res["video"] = self.make_video(paths, fps=fps)
        return res

    def angles_collage(self, angles: list[float] | None = None, range: float = 130,
                       freeze: bool = True, cols: int | None = None,
                       out: str | None = None, title: str | None = None,
                       clean: bool = True) -> dict:
        """Capture the current third-person subject from several orbit angles.

        With `freeze`, time is nearly paused so every angle shows the same
        moment — a turntable of one instant (great for models or a bot mid-move).
        """
        angles = angles or [0, 45, 90, 135, 180, 225, 270, 315]
        self.set_cvar("cg_thirdPerson", 1, confirm=False)
        if clean:
            self.cinematic(True)
            time.sleep(0.2)
        saved_ts = None
        if freeze:
            saved_ts = self.get_cvar("timescale")
            self.set_cvar("timescale", 0.02, confirm=False)
            time.sleep(0.2)
        try:
            def hook(i):
                self.orbit(angles[i], range=range)
                time.sleep(0.15)
            paths = self.capture_frames(len(angles), interval=0.15, hook=hook)
        finally:
            if freeze and saved_ts is not None:
                self.set_cvar("timescale", saved_ts, confirm=False)
            if clean:
                self.cinematic(False)
        sheet = self.collage(paths, out=out, cols=cols or max(1, len(angles) // 2),
                             title=title, labels=[f"{int(a)}°" for a in angles[:len(paths)]])
        return {"collage": sheet, "frames": paths, "angles": angles}

    # -- event-timed capture (shoot/clip around what we observe) -------------

    def wait_event(self, pattern: str, timeout: float = 30.0) -> str | None:
        """Watch the live console for `pattern` (regex) and return the matching
        line when it appears, or None on timeout. Engine events print here:
        kills ('killed'/'fragged'), 'entered the game', race finish, telemetry
        markers, your own `echo` beacons, etc."""
        rx = re.compile(pattern)
        buf, deadline = "", time.time() + timeout
        while time.time() < deadline:
            buf += self._read_new()
            clean = strip_colors(buf)
            m = rx.search(clean)
            if m:
                ls = clean.rfind("\n", 0, m.start()) + 1
                le = clean.find("\n", m.end())
                return clean[ls:le if le >= 0 else None].strip() or m.group(0)
            if not self.alive:
                raise EngineError("engine exited")
            time.sleep(0.05)
        return None

    def capture_on_event(self, pattern: str, frames: int = 9, interval: float = 0.3,
                         timeout: float = 30.0, cols: int = 3, out: str | None = None,
                         title: str | None = None, fps: int | None = None,
                         clean: bool = True) -> dict:
        """Wait until `pattern` is observed, then film the aftermath into a
        collage — times the shot to the moment instead of guessing."""
        line = self.wait_event(pattern, timeout)
        res = self.clip_collage(frames=frames, interval=interval, cols=cols,
                                out=out, title=title or f"on: {pattern}", fps=fps,
                                clean=clean)
        res["event"] = line
        res["fired"] = line is not None
        return res

    def record_window(self, pattern: str, pre: int = 4, post: int = 5,
                      interval: float = 0.3, timeout: float = 30.0,
                      out: str | None = None, title: str | None = None,
                      fps: int | None = None, clean: bool = True) -> dict:
        """Roll a pre-trigger frame buffer while watching for `pattern`; when it
        fires, keep the last `pre` frames + `post` more — a clip that brackets
        the moment (before *and* after), not just the aftermath."""
        from collections import deque
        rx = re.compile(pattern)
        ring = deque(maxlen=max(1, pre))
        buf, deadline, fired = "", time.time() + timeout, False
        if clean:
            self.cinematic(True); time.sleep(0.2)
        try:
            while time.time() < deadline:
                p = self.screenshot()
                if p:
                    ring.append(p)
                buf += self._read_new()
                if rx.search(strip_colors(buf)):
                    fired = True
                    break
                if not self.alive:
                    raise EngineError("engine exited")
                time.sleep(interval)
            frames = list(ring)
            n_pre = len(frames)
            if fired:
                for _ in range(max(0, post)):
                    time.sleep(interval)
                    p = self.screenshot()
                    if p:
                        frames.append(p)
        finally:
            if clean:
                self.cinematic(False)
        labels = ([f"-{n_pre-i}" for i in range(n_pre)] +
                  [f"+{i+1}" for i in range(len(frames) - n_pre)])
        sheet = self.collage(frames, out=out, title=title or f"window: {pattern}",
                             labels=labels)
        res = {"collage": sheet, "frames": frames, "fired": fired,
               "pre": n_pre, "post": len(frames) - n_pre}
        if fps:
            res["video"] = self.make_video(frames, fps=fps)
        return res

    # -- state-condition timing (observe a measured event) -------------------

    _OPS = {">": lambda a, b: a > b, "<": lambda a, b: a < b,
            ">=": lambda a, b: a >= b, "<=": lambda a, b: a <= b,
            "==": lambda a, b: a == b, "!=": lambda a, b: a != b}

    def _match_state(self, spec: dict, st: dict):
        subj = spec.get("subject", "any")
        field = spec.get("field", "speed")
        op = self._OPS.get(spec.get("op", ">"))
        val = float(spec.get("value", 0))
        cands = st["clients"]
        me = st.get("self") or {}
        if subj in ("self", "me"):
            cands = [c for c in cands if c["num"] == me.get("self")]
        elif subj in ("follow", "followed"):
            cands = [c for c in cands if c["num"] == me.get("following")]
        elif subj not in (None, "any", "all"):
            cands = [c for c in cands if str(c["num"]) == str(subj)
                     or c.get("name", "").lower() == str(subj).lower()]
        for c in cands:
            v = c.get(field)
            if isinstance(v, (int, float)) and op and op(v, val):
                return c
        return None

    def wait_state(self, spec: dict, timeout: float = 20.0, hz: int = 10):
        """Poll the world until a measured condition holds, e.g.
        {"subject":"Gargoyle","field":"speed","op":">","value":400} or
        {"subject":"any","field":"health","op":"<","value":30}. Returns the
        matching client (or None on timeout)."""
        interval = 1.0 / max(1, hz)
        t0 = time.time()
        while time.time() - t0 < timeout:
            m = self._match_state(spec, self.state())
            if m:
                return m
            if not self.alive:
                raise EngineError("engine exited")
            time.sleep(interval)
        return None

    def capture_on_state(self, spec: dict, frames: int = 6, interval: float = 0.3,
                         timeout: float = 20.0, out: str | None = None,
                         title: str | None = None, fps: int | None = None) -> dict:
        """Wait until a measured condition holds, then auto-frame the matching
        subject and film it — time a shot to *what the game is doing*, not a log
        line. Complements capture_on_event (console regex)."""
        m = self.wait_state(spec, timeout)
        self.camera_mode()
        if m:
            try:
                self.frame(m["num"])
            except EngineError:
                pass
        res = self.clip_collage(frames=frames, interval=interval,
                                title=title or f"on {spec.get('subject')} "
                                f"{spec.get('field')}{spec.get('op')}{spec.get('value')}",
                                out=out, fps=fps)
        res["matched"] = m
        res["fired"] = m is not None
        return res

    # -- authoring: generate + load content live -----------------------------

    def reload(self, visual: bool = True, sound: bool = False):
        """Re-init the renderer (and optionally sound) so freshly-deployed
        textures/shaders/models are picked up without relaunching."""
        if visual:
            self.vid_restart()
        if sound:
            self.command("snd_restart")

    def generate_map(self, seed=None, kind: str = "course", difficulty=None,
                     length=None, load: bool = True, devmap: bool = True,
                     extra_flags=None, timeout: float = 180) -> dict:
        """Generate a course with strafegen.py, deploy it into THIS instance's
        game dir, and (by default) load it live — author + play in one call.

        kind: course | arena | surf | killbox.  Loose .bsp/.aas are dropped in
        maps/ (found at open time even mid-session); the .pk3 (full shaders) is
        copied too for the next fresh launch.
        """
        import sys
        out_dir = STRAFEGEN / "generated"
        out_dir.mkdir(parents=True, exist_ok=True)
        args = [sys.executable, str(STRAFEGEN / "strafegen.py")]
        if seed is not None:
            args.append(str(seed))
        flag = {"course": None, "arena": "--arena", "surf": "--surf",
                "killbox": "--killbox"}.get(kind)
        if kind not in (None, "course") and flag is None:
            raise EngineError(f"unknown map kind: {kind}")
        if flag:
            args.append(flag)
        if difficulty is not None:
            args += ["--difficulty", str(difficulty)]
        if length is not None:
            args += ["--length", str(length)]
        args += ["--pk3", "--out", str(out_dir)] + list(extra_flags or [])

        before = {p: p.stat().st_mtime for p in out_dir.glob("*.bsp")}
        proc = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        if proc.returncode != 0:
            raise EngineError(f"strafegen failed: {(proc.stderr or proc.stdout)[-600:]}")
        bsps = sorted(out_dir.glob("*.bsp"), key=lambda p: p.stat().st_mtime)
        fresh = [p for p in bsps if before.get(p) != p.stat().st_mtime]
        if not (fresh or bsps):
            raise EngineError("strafegen produced no .bsp")
        bsp = (fresh or bsps)[-1]
        name = bsp.stem

        maps_dir = self._gamedir / "maps"
        maps_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(bsp, maps_dir / bsp.name)
        aas = bsp.with_suffix(".aas")
        if aas.exists():
            shutil.copy2(aas, maps_dir / aas.name)
        pk3 = bsp.with_suffix(".pk3")
        deployed_pk3 = None
        if pk3.exists():
            shutil.copy2(pk3, self._gamedir / pk3.name)
            deployed_pk3 = str(self._gamedir / pk3.name)
        res = {"name": name, "kind": kind, "bsp": str(maps_dir / bsp.name),
               "pk3": deployed_pk3, "loaded": False}
        if load and self._pipe_fd is not None:
            self.change_map(name, devmap=devmap)
            res["loaded"] = True
        return res

    def screenshot(self, jpeg: bool = True) -> str | None:
        """Trigger a screenshot (client mode only) and return the file path."""
        if self.mode != "client":
            raise EngineError("screenshots require mode='client' (needs a display)")
        ssdir = self._gamedir / "screenshots"
        before = set(ssdir.glob("*")) if ssdir.exists() else set()
        self.command("screenshotJPEG" if jpeg else "screenshot")
        deadline = time.time() + 5
        while time.time() < deadline:
            now = set(ssdir.glob("*")) if ssdir.exists() else set()
            new = now - before
            if new:
                return str(sorted(new, key=lambda p: p.stat().st_mtime)[-1])
            time.sleep(0.1)
        return None

    # -- telemetry -----------------------------------------------------------

    def telemetry(self, tag: str | None = None, limit: int = 50) -> list[dict]:
        """Read the modded qagame's JSONL playtest telemetry from this session."""
        pdir = self._gamedir / "playtest"
        if not pdir.exists():
            return []
        files = sorted(pdir.glob(f"{tag}.jsonl" if tag else "*.jsonl"),
                       key=lambda p: p.stat().st_mtime)
        recs = []
        for f in files:
            for line in f.read_text(errors="replace").splitlines():
                line = line.strip()
                if line:
                    try:
                        recs.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass
        return recs[-limit:]


# --- source-tier helpers (rebuild for compile-time constants) ---------------

def rebuild(clean: bool = False, deploy: bool = True, oa: str = DEFAULT_OA,
            timeout: float = 1200) -> dict:
    """Rebuild the engine + mod via scripts/build.sh, then redeploy the dylibs.

    Use after editing compile-time movement constants in bg_pmove.c / bg_local.h
    that are not cvar-backed (JUMP_VELOCITY, wall-jump/slide/bhop tuning, ...).
    """
    if not BUILD_SH.exists():
        raise EngineError(f"build script missing: {BUILD_SH}")
    cmd = [str(BUILD_SH)] + (["clean"] if clean else [])
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    result = dict(returncode=proc.returncode,
                  stdout=proc.stdout[-4000:], stderr=proc.stderr[-4000:])
    if proc.returncode == 0 and deploy:
        result["deployed"] = deploy_dylibs(oa)
    return result


_SRC_FILES = [ROOT / "engine" / "code" / "game" / "bg_pmove.c",
              ROOT / "engine" / "code" / "game" / "bg_local.h"]
_SRC_FLOAT = re.compile(r"^\s*(?:static\s+)?float\s+(pm_\w+)\s*=\s*(-?[\d.]+)f?\s*;(?:\s*//\s*(.*))?")
_SRC_DEFINE = re.compile(r"^\s*#define\s+(\w+)\s+(-?[\d.]+)f?\b(?:.*//\s*(.*))?")
_SRC_DEF_KW = re.compile(r"JUMP|WALL|SLIDE|BHOP|DOUBLE|AIR|DASH|GRAPPLE|STEP|"
                         r"CROUCH|WALK|OVERCLIP|GRAVITY|FRICTION|ACCEL", re.I)


def config_overrides(oa: str = DEFAULT_OA) -> list[dict]:
    """Report cvars whose live config (autoexec.cfg / strafe64.cfg, exec'd at
    startup) overrides their g_main.c source default — so you can see at a glance
    where the running game diverges from the documented defaults (e.g. the
    g_timeBindMin 0.05→0.2 override). Pure file parse, no engine needed."""
    gmain = ROOT / "engine" / "code" / "game" / "g_main.c"
    defaults = {}
    if gmain.exists():
        for m in re.finditer(r'\{\s*&\w+,\s*"([^"]+)",\s*"([^"]*)"', gmain.read_text(errors="replace")):
            defaults[m.group(1)] = m.group(2)
    out = []
    cfg_re = re.compile(r'^\s*seta?\s+(\w+)\s+"?([^"\s/]+)"?')
    for cfg in ("autoexec.cfg", "strafe64.cfg"):
        p = Path(oa) / GAME / cfg
        if not p.exists():
            continue
        for line in p.read_text(errors="replace").splitlines():
            m = cfg_re.match(line)
            if not m:
                continue
            name, val = m.group(1), m.group(2)
            if name in defaults and val != defaults[name]:
                out.append({"cvar": name, "source_default": defaults[name],
                            "config_value": val, "from": cfg})
    return out


def source_constants() -> list[dict]:
    """Enumerate the source-tier movement constants in bg_pmove.c / bg_local.h —
    the compile-time tunables (most NOT cvar-backed). Each entry says whether it's
    `live` (tune now via engine_movement_set / a cvar) or rebuild-only (edit the
    file at file:line, then engine_rebuild). Completes the two-tier control view:
    engine_movement_get shows the live cvars; this shows everything in source."""
    live = set(MOVEMENT_CVARS)
    out = []
    for f in _SRC_FILES:
        if not f.exists():
            continue
        for i, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            m = _SRC_FLOAT.match(line)
            if m:
                out.append({"name": m.group(1), "value": m.group(2), "kind": "float",
                            "file": f.name, "line": i, "live": m.group(1) in live,
                            "doc": (m.group(3) or "").strip()})
                continue
            m = _SRC_DEFINE.match(line)
            if m and _SRC_DEF_KW.search(m.group(1)):
                out.append({"name": m.group(1), "value": m.group(2), "kind": "define",
                            "file": f.name, "line": i, "live": False,
                            "doc": (m.group(3) or "").strip()})
    return out


def set_source_constant(name: str, value, rebuild: bool = False) -> dict:
    """Edit a source-tier movement constant in place (bg_pmove.c / bg_local.h) at
    its exact line, preserving the suffix/comment. With rebuild=True, also rebuild
    + redeploy so it takes effect. Completes source-tier control: see them with
    source_constants(), change them here. (For `live` cvar-backed constants this
    refuses — the cvar overrides the source value at runtime; use set_cvar /
    set_movement instead.)"""
    consts = {c["name"]: c for c in source_constants()}
    if name not in consts:
        raise EngineError(f"no source constant '{name}' — see source_constants()")
    c = consts[name]
    if c["live"]:
        raise EngineError(f"{name} is a live cvar — editing source has no runtime effect "
                          f"(the cvar overrides it); use set_movement/set_cvar instead")
    f = next(p for p in _SRC_FILES if p.name == c["file"])
    lines = f.read_text().splitlines(keepends=True)
    idx = c["line"] - 1
    old = lines[idx]
    vstr = str(value)
    if c["kind"] == "float":
        # preserve the original token's decimal precision so edits/reverts stay
        # byte-clean (e.g. setting 1.10's value to 1.1 still writes "1.10",
        # 75.0 → 90 writes "90.0") — Python's str() would otherwise drop zeros.
        old_dec = len(c["value"].split(".")[1]) if "." in str(c["value"]) else 0
        try:
            fv = float(value)
            nat = repr(fv)
            nat_dec = len(nat.split(".")[1]) if "." in nat else 0
            vstr = f"{fv:.{max(old_dec, nat_dec, 1)}f}"
        except (TypeError, ValueError):
            pass
        pat = re.compile(r"(float\s+" + re.escape(name) + r"\s*=\s*)-?[\d.]+(f?\s*;)")
    else:
        pat = re.compile(r"(#define\s+" + re.escape(name) + r"\s+)-?[\d.]+(f?)")
    if not pat.search(old):
        raise EngineError(f"could not locate {name} at {c['file']}:{c['line']} (source changed?)")
    new = pat.sub(lambda m: f"{m.group(1)}{vstr}{m.group(2)}", old, count=1)
    lines[idx] = new
    f.write_text("".join(lines))
    res = {"name": name, "old": c["value"], "new": str(value),
           "file": c["file"], "line": c["line"], "rebuilt": False}
    if rebuild:
        rb = globals()["rebuild"]()
        res["rebuilt"] = rb.get("returncode") == 0
        res["rebuild_stderr"] = rb.get("stderr", "")[-300:]
    return res


def list_processes() -> list[dict]:
    """List this repo's running engine processes, classified by how they were
    launched: 'persistent' (engine_open, under LIVE_BASE — intentional),
    'session' (engine_launch, an s64api_ tempdir home — an orphan once its
    launcher exits), or 'other'. Foreign engines (different binary) are ignored."""
    try:
        out = subprocess.run(["ps", "-axwwo", "pid=,command="],
                             capture_output=True, text=True).stdout
    except Exception:
        return []
    procs = []
    for line in out.splitlines():
        line = line.strip()
        if str(ENGINE_DIR) not in line:           # only this repo's engine
            continue
        if "ioquake3" not in line and "ioq3ded" not in line:
            continue
        try:
            pid = int(line.split(None, 1)[0])
        except ValueError:
            continue
        m = re.search(r"fs_homepath (\S+)", line)
        home = m.group(1) if m else "?"
        if home.startswith(str(Path(LIVE_BASE))):
            kind = "persistent"
        elif "s64api_" in home:
            kind = "session"
        else:
            kind = "other"
        procs.append({"pid": pid, "kind": kind, "home": home,
                      "mode": "dedicated" if "ioq3ded" in line else "client"})
    return procs


def kill_orphans(keep_pid: int | None = None) -> dict:
    """Kill orphaned session-scoped engines (s64api_ tempdir homes) — windows
    left over when their launching process exited. Never touches persistent
    (engine_open) engines, foreign engines, or `keep_pid` (the live session)."""
    killed, kept = [], []
    for p in list_processes():
        if p["kind"] != "session" or p["pid"] == keep_pid:
            kept.append(p)
            continue
        try:
            os.kill(p["pid"], 15)
            killed.append(p["pid"])
        except OSError:
            pass
    return {"killed": killed, "remaining": [p for p in kept if p["kind"] != "session"
                                            or p["pid"] == keep_pid]}


def _mean(xs):
    xs = [x for x in xs if isinstance(x, (int, float))]
    return sum(xs) / len(xs) if xs else 0.0


def _aggregate_playtest(recs, mapname):
    from collections import Counter
    n = len(recs)
    if not n:
        return {"map": mapname, "bot_runs": 0,
                "note": "no telemetry — bots never spawned/navigated "
                        "(the map needs an .aas navmesh; bspc fails on some seeds)"}
    evs = Counter(r.get("ev") for r in recs)
    deaths = [r for r in recs if r.get("ev") == "death"]
    fin = evs.get("finish", 0)
    completion = 100.0 * fin / n
    causes = Counter(r.get("mod", "?") for r in deaths)
    void_share = 100.0 * causes.get("MOD_FALLING", 0) / len(deaths) if deaths else 0.0
    flow = _mean([r.get("flowpct") for r in recs])
    stuck = _mean([r.get("stuckms") for r in recs])
    fins = [r.get("racems") for r in recs if r.get("ev") == "finish" and r.get("racems", -1) > 0]

    # NOTE: bots ignore the race start/finish triggers, so they never emit a
    # 'finish' event — completion% is ~0 for bot playtests and is NOT a fitness
    # signal here. Judge bots on the movement-quality metrics they DO exercise:
    # flow (sustained momentum), stuck (nav/friction trouble), and moveset use.
    flags = []
    if flow < 35:
        flags.append(f"low flow {flow:.0f}% (choppy / blocked movement)")
    if stuck > 700:
        flags.append(f"high stuck {stuck:.0f}ms (nav or friction trouble)")
    if deaths and void_share > 85:
        flags.append(f"void-dominated deaths {void_share:.0f}%")
    return {
        "map": mapname, "bot_runs": n,
        "finishes": fin, "deaths": len(deaths), "timeouts": evs.get("timeout", 0),
        "flow_pct": round(flow, 1),
        "air_pct": round(_mean([r.get("airpct") for r in recs]), 1),
        "wallrun_pct": round(_mean([r.get("wallrunpct") for r in recs]), 1),
        "avg_max_speed": round(_mean([r.get("maxspd") for r in recs])),
        "best_bhop_chain": max([r.get("maxbhop", 0) for r in recs], default=0),
        "moveset_used": {
            "wallrun_pct": round(100.0 * sum(1 for r in recs if r.get("wallrunpct", 0) > 0) / n),
            "walljump_pct": round(100.0 * sum(1 for r in recs if r.get("wj", 0) > 0) / n),
            "doublejump_pct": round(100.0 * sum(1 for r in recs if r.get("dj", 0) > 0) / n)},
        "deaths_by_cause": dict(causes),
        "void_share_pct": round(void_share, 1),
        "avg_stuck_ms": round(stuck),
        "completion_pct": round(completion, 1),
        "completion_note": "bots ignore race triggers — completion% is not a "
                           "fitness signal for bot runs; judge flow / speed / stuck",
        "verdict": "good bot movement" if not flags else "; ".join(flags),
    }


def playtest_report(map: str | None = None, bots: int = 5, seconds: float = 30,
                    skill: int = 4, oa: str = DEFAULT_OA) -> dict:
    """Run a headless bot playtest and return a fitness report — design → judge.

    Spins a dedicated server with g_playtest telemetry + bots on `map` (defaults
    to the sandbox), lets them play `seconds`, and aggregates completion / flow /
    speed / moveset usage / death causes with band flags. The design→playtest→
    iterate loop for procedural courses.
    """
    tag = "report"
    eng = EngineSession(mode="dedicated", map=map, oa=oa, bots=bots, bot_skill=skill,
                        extra=["+set", "bot_enable", "1",
                               "+set", "g_playtest", "1", "+set", "g_playtestTag", tag,
                               "+set", "g_voidRise", "1",
                               "+set", "timelimit", "0", "+set", "fraglimit", "0"])
    eng.start(timeout=90)
    mapname = eng.map
    try:
        time.sleep(seconds)
        recs = eng.telemetry(tag=tag, limit=1_000_000)
    finally:
        eng.stop()
    return _aggregate_playtest(recs, mapname)


def selftest(oa: str = DEFAULT_OA, keep_open: bool = False) -> dict:
    """End-to-end health check: spin a client engine and exercise the whole API
    surface (cvars, movement, state, spawn/clear, presets, framing, screenshot,
    demo), returning pass/fail per capability. Run after a rebuild to confirm
    everything still works."""
    results = {}

    def check(name, fn):
        try:
            ok, detail = fn()
            results[name] = {"ok": bool(ok), "detail": detail}
        except Exception as e:
            results[name] = {"ok": False, "detail": f"{type(e).__name__}: {e}"}

    eng = EngineSession(mode="client", oa=oa, bots=3)
    try:
        eng.start(timeout=90)
        time.sleep(5)  # let bots spawn + move

        check("launch", lambda: (eng.alive, f"pid {eng._pid} map {eng.map}"))
        check("get_cvar", lambda: ((lambda v: (v is not None, v))(eng.get_cvar("pm_strafeAccelerate"))))
        check("set_cvar", lambda: (eng.set_cvar("pm_airControlAmount", 175) == "175", "roundtrip 175"))
        check("movement", lambda: ((lambda m: (len(m) > 0, f"{len(m)} cvars"))(eng.get_movement())))
        check("effects", lambda: ((lambda m: (len(m) > 0, f"{len(m)} cvars"))(eng.get_effects())))

        def chk_state():
            st = eng.state()
            c0 = st["clients"][0] if st["clients"] else {}
            has_move = all(k in c0 for k in ("speed", "air", "wj", "dj"))
            return (len(st["clients"]) >= 1 and has_move,
                    f"{len(st['clients'])} clients, movement fields={has_move}")
        check("state", chk_state)

        def chk_spawn():
            eng.spawn("item_quad")
            cleared = eng.clear_spawns().get("cleared")
            return (cleared == 1, f"spawned 1, cleared {cleared}")
        check("spawn+clear", chk_spawn)

        def chk_presets():
            eng.set_cvar("g_knockback", 1234)
            eng.save_preset("_selftest")
            eng.set_cvar("g_knockback", 1000)
            eng.load_preset("_selftest")
            return (eng.get_cvar("g_knockback") == "1234", "save/load roundtrip")
        check("presets", chk_presets)

        def chk_frame():
            eng.camera_mode()
            info = eng.frame("action")
            return (bool(info.get("target")), f"framed {info.get('target')}")
        check("frame", chk_frame)

        def chk_shot():
            p, data = eng.screenshot_bytes()
            return (bool(data) and len(data) > 1000, f"{len(data) if data else 0} bytes")
        check("screenshot", chk_shot)

        def chk_demo():
            r = eng.clip_demo(seconds=2.0, name="_selftest")
            return (bool(r.get("demo")), f"{r.get('bytes')} bytes")
        check("demo", chk_demo)
    finally:
        if not keep_open:
            eng.stop()

    passed = sum(1 for r in results.values() if r["ok"])
    return {"ok": passed == len(results), "passed": passed, "total": len(results),
            "checks": results}


if __name__ == "__main__":
    # tiny smoke CLI: launch dedicated, print the movement model, quit.
    import pprint
    with EngineSession(mode="dedicated", map="strafe64_1337") as eng:
        pprint.pprint(eng.get_movement())
