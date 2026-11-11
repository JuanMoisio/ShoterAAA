"""
shoter_common.py -- helpers shared by the ShoterAAA content build scripts.

Runs inside the Unreal Editor's embedded Python (UE 5.8).  Nothing in here depends on
the rest of the build scripts, so every other module may import it.

Contents
  * logging with the "[ShoterBuild]" prefix
  * small constructors for unreal structs that avoid constructor argument-order traps
    (unreal.Rotator takes (roll, pitch, yaw) and unreal.Color takes (b, g, r, a))
  * set_prop / get_prop: tolerant property access that tries the snake_case Python name,
    the raw C++ name and finally a fuzzy match against dir(obj)
  * asset helpers that prefer the editor subsystems and fall back to EditorAssetLibrary
  * load_cpp_class for native classes of this project
"""
import re
import traceback

import unreal

TAG = "[ShoterBuild]"

# Neon palette (linear RGB), shared by the material and map scripts.
NEON = {
    "cyan": (0.0, 0.85, 1.0),
    "magenta": (1.0, 0.05, 0.75),
    "blue": (0.08, 0.22, 1.0),
    "red": (1.0, 0.04, 0.03),
    "orange": (1.0, 0.33, 0.02),
}


# --------------------------------------------------------------------------- logging

def log(message):
    unreal.log("{} {}".format(TAG, message))


def warn(message):
    unreal.log_warning("{} {}".format(TAG, message))


def error(message):
    unreal.log_error("{} {}".format(TAG, message))


def log_exception(context):
    """Logs the current exception (call from inside an except block), one log line per traceback line."""
    error("{} failed:".format(context))
    for line in traceback.format_exc().rstrip().splitlines():
        error("    " + line)


# --------------------------------------------------------------------------- struct helpers

def vec(x=0.0, y=0.0, z=0.0):
    return unreal.Vector(float(x), float(y), float(z))


def rot(pitch=0.0, yaw=0.0, roll=0.0):
    """unreal.Rotator from degrees.  Built by attribute so the (roll, pitch, yaw) ctor order cannot bite."""
    r = unreal.Rotator()
    try:
        r.pitch = float(pitch)
        r.yaw = float(yaw)
        r.roll = float(roll)
    except Exception:
        r = unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))
    return r


def lin(r, g, b, a=1.0):
    return unreal.LinearColor(float(r), float(g), float(b), float(a))


def color8(r, g, b, a=255):
    """unreal.Color (8 bit) built by attribute: the ctor order is (b, g, r, a)."""
    c = unreal.Color()
    try:
        c.r = int(r)
        c.g = int(g)
        c.b = int(b)
        c.a = int(a)
    except Exception:
        c = unreal.Color(b=int(b), g=int(g), r=int(r), a=int(a))
    return c


# --------------------------------------------------------------------------- property access

def to_snake(name):
    """CamelCase -> snake_case the way Unreal's Python bindings do it ("UseInPvP" -> "use_in_pv_p")."""
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", s)
    return s.lower()


def _norm(name):
    return name.replace("_", "").lower()


def _strip_bool_prefix(cpp_name):
    if len(cpp_name) > 1 and cpp_name[0] == "b" and cpp_name[1].isupper():
        return cpp_name[1:]
    return None


def python_names(cpp_name):
    """Candidate Python names for a C++ property name, most likely first."""
    names = []

    def add(candidate):
        if candidate and candidate not in names:
            names.append(candidate)

    stripped = _strip_bool_prefix(cpp_name)
    if stripped:
        add(to_snake(stripped))
    add(to_snake(cpp_name))
    add(cpp_name)
    if stripped:
        add(stripped)
    return names


_warned = set()


def warn_once(key, message):
    """Logs a warning only the first time `key` is seen in this editor session."""
    if key not in _warned:
        _warned.add(key)
        warn(message)


_warn_once = warn_once


def set_prop(obj, cpp_name, value, quiet=False):
    """
    Sets an editor property given its C++ name.  Tries the snake_case name(s), the raw C++ name and a
    fuzzy match against dir(obj).  Returns True on success; logs one warning per (class, property) on failure.
    """
    if obj is None:
        if not quiet:
            warn("set_prop({}): object is None".format(cpp_name))
        return False

    last_error = None
    for name in python_names(cpp_name):
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception as exc:  # unknown property, read-only or wrong type
            last_error = exc

    stripped = _strip_bool_prefix(cpp_name)
    targets = {_norm(cpp_name)}
    if stripped:
        targets.add(_norm(stripped))
    try:
        attributes = dir(obj)
    except Exception:
        attributes = []
    for attr in attributes:
        if attr.startswith("_") or _norm(attr) not in targets:
            continue
        try:
            obj.set_editor_property(attr, value)
            return True
        except Exception as exc:
            last_error = exc

    if not quiet:
        class_name = type(obj).__name__
        _warn_once(
            (class_name, cpp_name),
            "Could not set property '{}' on {} (tried {}): {}".format(
                cpp_name, class_name, ", ".join(python_names(cpp_name)), last_error
            ),
        )
    return False


def get_prop(obj, cpp_name, default=None):
    """Reads an editor property given its C++ name (same lookup strategy as set_prop)."""
    if obj is None:
        return default
    for name in python_names(cpp_name):
        try:
            return obj.get_editor_property(name)
        except Exception:
            pass
    stripped = _strip_bool_prefix(cpp_name)
    targets = {_norm(cpp_name)}
    if stripped:
        targets.add(_norm(stripped))
    try:
        attributes = dir(obj)
    except Exception:
        attributes = []
    for attr in attributes:
        if attr.startswith("_") or _norm(attr) not in targets:
            continue
        try:
            return obj.get_editor_property(attr)
        except Exception:
            pass
    return default


def set_props(obj, values, quiet=False):
    """Applies a {cpp_name: value} dict with set_prop; returns the number of properties that failed."""
    failed = 0
    for key, value in values.items():
        if not set_prop(obj, key, value, quiet=quiet):
            failed += 1
    return failed


# --------------------------------------------------------------------------- subsystems and assets

def get_subsystem(class_name):
    """unreal.get_editor_subsystem(unreal.<class_name>) or None when the class/subsystem is unavailable."""
    cls = getattr(unreal, class_name, None)
    if cls is None:
        return None
    try:
        return unreal.get_editor_subsystem(cls)
    except Exception:
        return None


def _asset_subsystem():
    return get_subsystem("EditorAssetSubsystem")


def asset_exists(path):
    subsystem = _asset_subsystem()
    try:
        if subsystem is not None:
            return bool(subsystem.does_asset_exist(path))
        return bool(unreal.EditorAssetLibrary.does_asset_exist(path))
    except Exception:
        return False


def load_asset(path):
    """Loads an asset by package path or object path; returns None when it does not exist."""
    subsystem = _asset_subsystem()
    try:
        if subsystem is not None:
            return subsystem.load_asset(path)
    except Exception:
        pass
    try:
        return unreal.load_asset(path)
    except Exception:
        return None


def save_asset(asset, path=None):
    """Saves a loaded asset (always, not only when dirty).  Returns True on success."""
    subsystem = _asset_subsystem()
    try:
        if subsystem is not None:
            return bool(subsystem.save_loaded_asset(asset, False))
        return bool(unreal.EditorAssetLibrary.save_loaded_asset(asset, False))
    except Exception:
        pass
    if path:
        try:
            if subsystem is not None:
                return bool(subsystem.save_asset(path, False))
            return bool(unreal.EditorAssetLibrary.save_asset(path, False))
        except Exception:
            log_exception("save_asset({})".format(path))
    return False


def make_directory(path):
    subsystem = _asset_subsystem()
    try:
        if subsystem is not None:
            return bool(subsystem.make_directory(path))
        return bool(unreal.EditorAssetLibrary.make_directory(path))
    except Exception:
        return False


def delete_asset(path):
    subsystem = _asset_subsystem()
    try:
        if subsystem is not None:
            return bool(subsystem.delete_asset(path))
        return bool(unreal.EditorAssetLibrary.delete_asset(path))
    except Exception:
        return False


def project_dir():
    """Absolute project directory with forward slashes and a trailing slash."""
    try:
        value = unreal.SystemLibrary.get_project_directory()
        if value:
            return value if value.endswith("/") else value + "/"
    except Exception:
        pass
    return "D:/ShoterAAA/"


def content_dir():
    try:
        value = unreal.SystemLibrary.get_project_content_directory()
        if value:
            return value if value.endswith("/") else value + "/"
    except Exception:
        pass
    return project_dir() + "Content/"


def load_cpp_class(name, module="ShoterAAA", quiet=False):
    """
    Loads a native class of this project by its name without the A/U prefix, e.g. "ShoterPlayerStart".
    Returns None (and warns once) when the module has not been compiled/loaded.
    """
    path = "/Script/{}.{}".format(module, name)
    try:
        cls = unreal.load_class(None, path)
        if cls is not None:
            return cls
    except Exception:
        pass
    cls = getattr(unreal, name, None)
    if cls is not None:
        return cls
    if not quiet:
        warn_once(("class", name), "Native class {} not found - compile the C++ project before running the build.".format(path))
    return None


# --------------------------------------------------------------------------- environment probe

PROBE_UNREAL_NAMES = (
    "EditorActorSubsystem", "EditorAssetSubsystem", "LevelEditorSubsystem", "UnrealEditorSubsystem", "EditorLevelLibrary",
    "EditorLoadingAndSavingUtils", "MaterialEditingLibrary", "AssetToolsHelpers", "AssetImportTask", "StaticMeshActor",
    "PointLight", "RectLight", "DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "VolumetricCloud",
    "PostProcessVolume", "NavMeshBoundsVolume",
)
PROBE_SUBSYSTEMS = ("EditorActorSubsystem", "EditorAssetSubsystem", "LevelEditorSubsystem", "UnrealEditorSubsystem")
PROBE_NATIVE_CLASSES = (
    "ShoterPlayerStart", "ShoterZombieSpawnPoint", "ShoterBuyStation", "ShoterGameMode_TDM", "ShoterMenuGameMode",
    "ShoterWeapon_PulseRifle", "ShoterWeapon_Ripper", "ShoterWeapon_Scatter", "ShoterWeapon_Rail", "ShoterWeapon_PlasmaLauncher",
)


def probe_environment():
    """
    Logs which editor APIs and which native classes of the game module are available in this editor session.
    This is the first thing to read when a step fails.  Returns a dict of the findings.
    """
    findings = {
        "missing_unreal_names": [n for n in PROBE_UNREAL_NAMES if not hasattr(unreal, n)],
        "missing_subsystems": [n for n in PROBE_SUBSYSTEMS if get_subsystem(n) is None],
        "missing_native_classes": [n for n in PROBE_NATIVE_CLASSES if load_cpp_class(n, quiet=True) is None],
    }
    log("API probe: unreal.* names missing: {} | editor subsystems unavailable: {} | native classes missing: {}".format(
        findings["missing_unreal_names"] or "none", findings["missing_subsystems"] or "none", findings["missing_native_classes"] or "none"))
    if findings["missing_native_classes"]:
        warn("Native classes of the ShoterAAA module are missing: the project is not compiled (or the module did not load). "
             "Maps will lack gameplay actors until you compile and re-run.")
    return findings


# --------------------------------------------------------------------------- step runner

def run_step(name, function, *args, **kwargs):
    """Runs one build step; returns (ok, result).  Exceptions are logged and swallowed so the next step can run."""
    log("=== Step: {} ===".format(name))
    try:
        return True, function(*args, **kwargs)
    except Exception:
        log_exception("Step '{}'".format(name))
        return False, None
