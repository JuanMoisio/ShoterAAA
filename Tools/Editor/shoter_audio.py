"""
shoter_audio.py -- step 1: import every RawAssets/Audio/*.wav into /Game/Shoter/Audio.

  * Asset name = file name without extension (SFX_PulseRifle_Fire.wav -> SFX_PulseRifle_Fire).
  * Existing assets are reused unless force_reimport is True (then they are replaced).
  * AMB_* and MUS_* are set to loop; per-sound volume multipliers are applied (VOLUME_OVERRIDES).
  * Properties are checked every run, so editing the tables below and re-running is enough.
"""
import os

import unreal

import shoter_common as C

DEST_PATH = "/Game/Shoter/Audio"
FALLBACK_SOURCE_DIR = "D:/ShoterAAA/RawAssets/Audio"

LOOPING_PREFIXES = ("AMB_", "MUS_")

# Everything that is not listed here plays at 1.0.
VOLUME_OVERRIDES = {
    "SFX_WeaponEquip": 1.8,
    "SFX_Impact_Surface": 1.8,
    "SFX_DryFire": 1.8,
    "SFX_UI_Click": 1.8,
    "SFX_Denied": 0.5,
    "AMB_Arena_Loop": 0.5,
}


def _source_dir():
    candidates = [C.project_dir() + "RawAssets/Audio", FALLBACK_SOURCE_DIR]
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return None


def _make_task(source_file, asset_name):
    task = unreal.AssetImportTask()
    C.set_prop(task, "Filename", source_file)
    C.set_prop(task, "DestinationPath", DEST_PATH)
    C.set_prop(task, "DestinationName", asset_name)
    C.set_prop(task, "bAutomated", True)
    C.set_prop(task, "bReplaceExisting", True)
    C.set_prop(task, "bReplaceExistingSettings", True)
    C.set_prop(task, "bSave", True)
    return task


def _imported_paths(task):
    paths = C.get_prop(task, "ImportedObjectPaths", None)
    try:
        return list(paths) if paths else []
    except Exception:
        return []


def _configure_sound(asset_name):
    """Applies looping/volume to one SoundWave and saves it when something changed.  Returns 'changed'/'ok'/'missing'."""
    path = "{}/{}".format(DEST_PATH, asset_name)
    sound = C.load_asset(path)
    if sound is None:
        C.error("Sound asset not found after import: {}".format(path))
        return "missing"

    want_loop = asset_name.startswith(LOOPING_PREFIXES)
    want_volume = float(VOLUME_OVERRIDES.get(asset_name, 1.0))

    changed = False
    current_loop = C.get_prop(sound, "bLooping", None)
    if current_loop is None or bool(current_loop) != want_loop:
        changed |= C.set_prop(sound, "bLooping", want_loop)

    current_volume = C.get_prop(sound, "Volume", None)
    if current_volume is None or abs(float(current_volume) - want_volume) > 1e-4:
        changed |= C.set_prop(sound, "Volume", want_volume)

    if changed:
        if not C.save_asset(sound, path):
            C.warn("Could not save {}".format(path))
        C.log("  configured {} (looping={}, volume={})".format(asset_name, want_loop, want_volume))
        return "changed"
    return "ok"


def run(force_reimport=False):
    """Imports and configures all sounds.  Returns a dict with the counters used by the final summary."""
    result = {"found": 0, "imported": 0, "reused": 0, "import_failed": 0, "configured": 0, "missing": 0, "problems": []}

    source_dir = _source_dir()
    if source_dir is None:
        message = "audio source folder not found (looked in <project>/RawAssets/Audio and {})".format(FALLBACK_SOURCE_DIR)
        C.error(message)
        result["problems"].append(message)
        return result

    wav_files = sorted(f for f in os.listdir(source_dir) if f.lower().endswith(".wav"))
    result["found"] = len(wav_files)
    C.log("Audio: {} wav files in {} -> {} (force_reimport={})".format(len(wav_files), source_dir, DEST_PATH, force_reimport))
    if not wav_files:
        C.warn("No .wav files to import")
        result["problems"].append("no .wav files found in " + source_dir)
        return result

    C.make_directory(DEST_PATH)

    # ---- import
    tasks = []
    task_names = []
    for file_name in wav_files:
        asset_name = os.path.splitext(file_name)[0]
        asset_path = "{}/{}".format(DEST_PATH, asset_name)
        if C.asset_exists(asset_path) and not force_reimport:
            result["reused"] += 1
            continue
        tasks.append(_make_task("{}/{}".format(source_dir, file_name), asset_name))
        task_names.append(asset_name)

    if tasks:
        C.log("Audio: importing {} file(s) ...".format(len(tasks)))
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        batch = 10
        for start in range(0, len(tasks), batch):
            chunk = tasks[start:start + batch]
            names = task_names[start:start + batch]
            try:
                tools.import_asset_tasks(chunk)
            except Exception:
                C.log_exception("import_asset_tasks (batch starting at {})".format(start))
            for task, asset_name in zip(chunk, names):
                if _imported_paths(task):
                    result["imported"] += 1
                elif C.asset_exists("{}/{}".format(DEST_PATH, asset_name)):
                    # The task did not report a path (older/newer import pipeline) but the asset is there.
                    result["imported"] += 1
                    C.warn("Import of {} reported no object path; the asset exists, assuming it worked".format(asset_name))
                else:
                    result["import_failed"] += 1
                    result["problems"].append("import failed: {}".format(asset_name))
                    C.error("Import failed: {}".format(asset_name))
    else:
        C.log("Audio: nothing to import (all assets exist; set FORCE_REIMPORT_AUDIO = True to replace them)")

    # ---- configure (runs for reused assets as well)
    for file_name in wav_files:
        asset_name = os.path.splitext(file_name)[0]
        state = _configure_sound(asset_name)
        if state == "changed":
            result["configured"] += 1
        elif state == "missing":
            result["missing"] += 1
            result["problems"].append("sound asset missing after import: {}".format(asset_name))

    C.log(
        "Audio done: {found} found, {imported} imported, {reused} reused, {import_failed} import failures, "
        "{configured} re-configured, {missing} missing".format(**result)
    )
    return result
