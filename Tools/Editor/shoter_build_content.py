"""
shoter_build_content.py -- entry point of the ShoterAAA content build (Unreal Engine 5.8, Python editor scripting).

Generates every binary asset the game needs from code:
  1. audio      shoter_audio.py      RawAssets/Audio/*.wav -> /Game/Shoter/Audio
  2. materials  shoter_materials.py  /Game/Shoter/Materials (materials + instances)
  3. maps       shoter_maps.py       /Game/Shoter/Maps/Arena_Neon and /Game/Shoter/Maps/MainMenu

Run it headless with run_build_content.ps1, or directly:
  UnrealEditor-Cmd.exe "D:\\ShoterAAA\\ShoterAAA.uproject" -run=pythonscript -script="D:\\ShoterAAA\\Tools\\Editor\\shoter_build_content.py" -unattended -nop4 -nosplash -NoSound
or inside an open editor: Tools > Execute Python Script... (or `py "D:/ShoterAAA/Tools/Editor/shoter_build_content.py"` in the console).

The build is idempotent: existing sounds/materials are reused or rebuilt in place, maps are rebuilt from scratch.
A failing step is logged and the next step still runs; the script raises at the very end if anything failed so the
editor process exits with a non-zero code.

Environment overrides (used by run_build_content.ps1):
  SHOTER_STEPS=audio,materials,maps    run only these steps
  SHOTER_FORCE_REIMPORT_AUDIO=1        same as FORCE_REIMPORT_AUDIO = True
"""
import importlib
import os
import sys
import time

import unreal

# --------------------------------------------------------------------------- configuration

# True: re-import sounds that already exist in /Game/Shoter/Audio (replaces them).  False: keep existing assets.
FORCE_REIMPORT_AUDIO = False

ALL_STEPS = ("audio", "materials", "maps")
FALLBACK_SCRIPT_DIR = "D:/ShoterAAA/Tools/Editor"


# --------------------------------------------------------------------------- bootstrap

def _script_dir():
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except NameError:                       # __file__ is not defined in every execution mode
        pass
    for arg in sys.argv:
        if str(arg).lower().endswith("shoter_build_content.py"):
            return os.path.dirname(os.path.abspath(arg))
    return FALLBACK_SCRIPT_DIR


def _load_modules(folder):
    """Puts this folder on sys.path and (re)imports the step modules so edits are picked up without restarting the editor."""
    if folder not in sys.path:
        sys.path.insert(0, folder)
    loaded = {}
    for name in ("shoter_common", "shoter_audio", "shoter_materials", "shoter_maps"):
        if name in sys.modules:
            loaded[name] = importlib.reload(sys.modules[name])
        else:
            loaded[name] = importlib.import_module(name)
    return loaded


def _wait_for_asset_registry(C):
    """Make sure existing assets are discoverable before we test for them (matters for the headless commandlet)."""
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
    except Exception:
        return
    for method, args in (("search_all_assets", (True,)), ("wait_for_completion", ())):
        try:
            getattr(registry, method)(*args)
        except Exception as exc:
            C.log("asset registry {}() not available: {}".format(method, exc))


def _selected_steps():
    wanted = os.environ.get("SHOTER_STEPS", "").replace(";", ",").replace(" ", "")
    if not wanted:
        return list(ALL_STEPS)
    steps = [s for s in wanted.split(",") if s]
    unknown = [s for s in steps if s not in ALL_STEPS]
    if unknown:
        unreal.log_warning("[ShoterBuild] ignoring unknown step(s) in SHOTER_STEPS: {}".format(", ".join(unknown)))
    return [s for s in ALL_STEPS if s in steps]


# --------------------------------------------------------------------------- summary text

def _describe(step, result):
    """Summary lines (what was created) for one finished step."""
    if not isinstance(result, dict):
        return [str(result)]
    if step == "audio":
        return ["{found} wav files: {imported} imported, {reused} reused, {configured} re-configured".format(**result)]
    if step == "materials":
        return [
            "{} materials: {}".format(len(result["materials_ok"]), ", ".join(result["materials_ok"])),
            "{} instances: {}".format(len(result["instances_ok"]), ", ".join(result["instances_ok"])),
        ]
    if step == "maps":
        lines = []
        for name in ("Arena_Neon", "MainMenu"):
            info = result.get(name)
            if not info:
                lines.append("{}: FAILED".format(name))
                continue
            lights = info.get("point_lights", 0) + info.get("rect_lights", 0) + info.get("directional_lights", 0) + info.get("sky_lights", 0)
            lines.append("{}: {} mesh actors, {} lights, {} player starts, {} zombie spawns ({} late), {} buy stations, {} nav volume(s), saved={}".format(
                name, info.get("static_meshes", 0), lights, info.get("player_starts", 0), info.get("zombie_spawns", 0),
                info.get("zombie_spawns_late", 0), info.get("buy_stations", 0), info.get("nav_volumes", 0), info.get("saved")))
        return lines
    return [str(result)]


# --------------------------------------------------------------------------- main

def main():
    started = time.time()
    folder = _script_dir()
    modules = _load_modules(folder)
    C = modules["shoter_common"]

    force_reimport = FORCE_REIMPORT_AUDIO or os.environ.get("SHOTER_FORCE_REIMPORT_AUDIO", "") not in ("", "0", "false", "False")
    steps = _selected_steps()
    try:
        engine_version = unreal.SystemLibrary.get_engine_version()
    except Exception:
        engine_version = "unknown"
    C.log("ShoterAAA content build started (engine {}, scripts in {})".format(engine_version, folder))
    C.log("Steps: {} | force audio reimport: {}".format(", ".join(steps), force_reimport))
    _wait_for_asset_registry(C)
    C.probe_environment()

    runners = {
        "audio": lambda: modules["shoter_audio"].run(force_reimport),
        "materials": lambda: modules["shoter_materials"].run(),
        "maps": lambda: modules["shoter_maps"].run(),
    }
    outcome = {}
    for step in steps:
        ok, result = C.run_step(step, runners[step])
        problems = list(result.get("problems", [])) if (ok and isinstance(result, dict)) else []
        for problem in problems:
            C.error("[{}] {}".format(step, problem))
        outcome[step] = (ok and not problems, result)

    C.log("================ SUMMARY ================")
    failed = []
    for step in steps:
        ok, result = outcome[step]
        C.log("{:<10} {}".format(step, "OK" if ok else "FAILED (see the errors above)"))
        if result is not None:
            for line in _describe(step, result):
                C.log("           " + line)
        if not ok:
            failed.append(step)
    C.log("Finished in {:.1f} s; {} step(s) failed".format(time.time() - started, len(failed)))
    if failed:
        raise RuntimeError("[ShoterBuild] failed step(s): {}".format(", ".join(failed)))


main()
