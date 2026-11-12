# ShoterAAA editor content scripts

Python scripts for the Unreal Editor (UE 5.8, PythonScriptPlugin + EditorScriptingUtils) that generate **all binary content**
of the game from code: sounds, materials and the two maps. Nobody has to build anything by hand in the editor.

| File | Purpose |
|---|---|
| `shoter_build_content.py` | Entry point. Runs the steps below in order, prints a summary, raises at the end if a step failed (non-zero exit code). |
| `shoter_audio.py` | Step 1: imports `RawAssets/Audio/*.wav` into `/Game/Shoter/Audio` and sets looping / volume. |
| `shoter_materials.py` | Step 2: builds the materials and material instances in `/Game/Shoter/Materials`. |
| `shoter_maps.py` | Step 3: builds `/Game/Shoter/Maps/Arena_Neon` and `/Game/Shoter/Maps/MainMenu`. |
| `shoter_common.py` | Shared helpers (logging, tolerant `set_prop`, asset helpers, class loading). |
| `run_build_content.ps1` | Finds `UnrealEditor-Cmd.exe`, runs the whole build headless, tees the log, returns the editor's exit code. |

Everything is idempotent: existing sounds are reused (unless `FORCE_REIMPORT_AUDIO`), materials are rebuilt in place (same asset,
graph recreated), maps are rebuilt from scratch every time. A failing step is logged and the next one still runs. Every log line
starts with `[ShoterBuild]`.

## Before you run it

1. **Compile the C++ project first.** The maps spawn `AShoterPlayerStart`, `AShoterZombieSpawnPoint`, `AShoterBuyStation`, the weapon
   classes and the game modes `ShoterGameMode_TDM` / `ShoterMenuGameMode`. They are loaded with
   `unreal.load_class(None, "/Script/ShoterAAA.<Name>")`. If a class is missing the script says so, skips those actors and fails the
   step (geometry, lights and materials are still produced; just re-run after compiling). `run_build_content.ps1 -Build` compiles
   the `ShoterAAAEditor` target for you.
2. UE 5.8 must be installed (the runner looks at the Epic launcher manifest, then `D:\EPICAGAMES\Epic Games\UE_5.8`, then
   `C:\Program Files\Epic Games\UE_5.8`; or pass `-EngineRoot`, or set `UE_ROOT`).
3. Close the editor if it has the project open and unsaved changes in `/Game/Shoter` (the script saves assets and maps).

## Run it

If PowerShell refuses to run the script (default execution policy), start it as
`powershell -NoProfile -ExecutionPolicy Bypass -File D:\ShoterAAA\Tools\Editor\run_build_content.ps1 [options]`.

```powershell
# everything (audio, materials, maps)
D:\ShoterAAA\Tools\Editor\run_build_content.ps1

# compile first, force re-importing the sounds
D:\ShoterAAA\Tools\Editor\run_build_content.ps1 -Build -ForceReimportAudio

# only some steps
D:\ShoterAAA\Tools\Editor\run_build_content.ps1 -Steps materials,maps
```

The runner executes exactly this (UE path auto-detected):

```
UnrealEditor-Cmd.exe "D:\ShoterAAA\ShoterAAA.uproject" -run=pythonscript -script="D:\ShoterAAA\Tools\Editor\shoter_build_content.py" -unattended -nop4 -nosplash -NoSound
```

Output goes to the console and to `D:\ShoterAAA\Saved\Logs\ShoterBuild.log` (UTF-8). The editor's own log is
`D:\ShoterAAA\Saved\Logs\ShoterAAA.log`. The exit code is the editor's. If the console shows nothing, add
`-ExtraArgs '-stdout','-FullStdOutLogOutput'`.

Inside an open editor you can use *Tools > Execute Python Script...* on `shoter_build_content.py`, or the console command
`py "D:/ShoterAAA/Tools/Editor/shoter_build_content.py"`. Modules are reloaded on every run, so edits take effect immediately.

### Switches

* `FORCE_REIMPORT_AUDIO` at the top of `shoter_build_content.py` (default `False`), or `-ForceReimportAudio`, or env `SHOTER_FORCE_REIMPORT_AUDIO=1`.
* `-Steps audio,materials,maps` or env `SHOTER_STEPS`.
* Tuning constants at the top of `shoter_maps.py`: `ADD_SKYLINE`, `ADD_VOLUMETRIC_CLOUD`, `MOON_INTENSITY`, `SKY_LIGHT_INTENSITY`,
  `FOG_DENSITY`, `BLOOM_INTENSITY`, `VIGNETTE_INTENSITY`, `EXPOSURE_BRIGHTNESS_RANGE`, `LOCAL_LIGHT_SCALE` (global dimmer for all
  point/rect lights), `PVP_COOLDOWN`.

## Step 1 - audio (`/Game/Shoter/Audio`)

Every `*.wav` in `RawAssets/Audio` becomes an asset with the file name as asset name (`SFX_PulseRifle_Fire.wav` -> `SFX_PulseRifle_Fire`),
imported with `unreal.AssetImportTask` (automated, replace existing, save). Afterwards, on every run:

* `looping = True` for `AMB_*` and `MUS_*`.
* volume 1.8: `SFX_WeaponEquip`, `SFX_Impact_Surface`, `SFX_DryFire`, `SFX_UI_Click`; 0.5: `SFX_Denied`, `AMB_Arena_Loop`; everything else 1.0.

Edit `VOLUME_OVERRIDES` in `shoter_audio.py` to change them.

## Step 2 - materials (`/Game/Shoter/Materials`)

Parameter names are a contract with the C++ code (`UShoterAssets`, `AShoterCharacter`, `FShoterFX`); `shoter_materials.py` verifies
after compiling that they exist and fails the step otherwise.

| Material | Setup | Parameters |
|---|---|---|
| `M_Shoter_Surface` | opaque, default lit | `BaseColor` (0.5 gray), `EmissiveColor` (black) / `Metallic` 0.5, `Roughness` 0.4, `EmissiveStrength` 0. Emissive = EmissiveColor x EmissiveStrength |
| `M_Shoter_Glow` | unlit, additive, two sided | `Color` (white) / `Intensity` 20. Emissive = Color x Intensity |
| `M_Shoter_Character` | opaque, default lit | `BaseColor`, `TeamColor`, `EmissiveColor` / `Metallic` 0.8, `Roughness` 0.35, `RimStrength` 4, `EmissiveStrength` 0. Emissive = Fresnel(exp 4) x TeamColor x RimStrength + EmissiveColor x EmissiveStrength |
| `M_Shoter_ImpactDecal` | deferred decal, translucent | `BurnColor`, `EmissiveColor`, `EmissiveStrength`, `OpacityScale`, `Roughness`, `Radius`, `Hardness`. Soft round burn mark (SphereMask on the decal UVs) with a small orange glow in the centre |
| `M_Shoter_ScorchDecal` | deferred decal, translucent | same parameters; bigger, darker, softer, irregular outline (two angular harmonics) |
| `M_Shoter_Floor` | opaque, default lit | `BaseColor`, `LineColor` / `LineWidth` 0.01, `TileSize` 200, `EmissiveStrength` 3, `Metallic` 0.85, `Roughness` 0.3, `RoughnessVariation` 0.15. World aligned panels (WorldPosition.xy / TileSize), dark grooves, emissive grid lines, per-panel roughness/tint variation |
| `M_Shoter_Wall` | opaque, default lit | `BaseColor`, `StripColor` / `StripHeight` 300, `StripWidth` 12, `TileSize` 200, `EmissiveStrength` 8, `Metallic` 0.8, `Roughness` 0.4. World aligned panels (axis aware: works on X facing, Y facing and top faces) and a horizontal emissive strip at world Z = `StripHeight` |

`M_Shoter_Surface`, `M_Shoter_Glow` and `M_Shoter_Character` also have "used with skeletal mesh / instanced static mesh / morph targets"
set, so they work on skeletal meshes in packaged builds.

Instances (`MaterialInstanceConstant`): `MI_Floor` (cyan lines), `MI_Floor_Blue`, `MI_Floor_Red` (team floor plates), `MI_Wall` (neutral, cyan
strip), `MI_Wall_Blue`, `MI_Wall_Red`, `MI_Metal_Dark`, `MI_Metal_Light`, and `MI_Neon_Cyan/Magenta/Blue/Red/Orange` (from
`M_Shoter_Surface`, `EmissiveStrength` 10: opaque, solid light strips that bloom).

## Step 3 - maps (`/Game/Shoter/Maps`)

Both maps are made by the same function from `/Engine/BasicShapes/Cube` static mesh actors (scaled), lights, atmosphere and the
C++ gameplay actors. Actors are in World Outliner folders (`Geometry/...`, `Neon/...`, `Lighting/...`, `Gameplay/...`,
`Atmosphere`, `PostProcess`, `Navigation`) and have readable, unique labels.

`Arena_Neon` (GameMode override `ShoterGameMode_TDM`) and `MainMenu` (override `ShoterMenuGameMode`, no NavMeshBoundsVolume).

```
   Y+  catwalk A (deck Z 330, Rail station)                       N lane  Y 1150..2000
 2000 +--------------------------------------------------------------------------------+
      | pocket |  lane wall Y=+1100 (4 segments, gaps 400/600/400, pillars at the gap) | pocket|
      |  BLUE  |   yard cover      PLATFORM 1200x1200x200 + 2 ramps       yard cover   |  RED  |
      |  base  |   axis pillar (-1700,0)  [Plasma]            axis pillar (1700,0)     |  base |
      |        |  lane wall Y=-1100                                                    |       |
      | pocket |  S lane Y -2000..-1150, catwalk B (deck Z 330, Ripper station)        | pocket|
-2000 +--------------------------------------------------------------------------------+
     X -3000                                  0                                      X +3000
```

* **Shell**: floor slab, perimeter walls 800 cm tall (strip at 300 cm comes from `M_Shoter_Wall`), blue/red tinted ends.
* **Bases** at X = -2600 (blue) / +2600 (red): 750 x 2100 alcove with side walls, team floor plate, emblem and console, 4 cover blocks
  (the two mouth blocks are MID, 200 cm), a TALL slab (500 wide, 320 high) on the long axis just outside the alcove (X = -2190 / +2190:
  hides the spawners from the platform axis, both sides stay open), a rect light, 6 `ShoterPlayerStart` (TeamId 0/1, PvP only).
* **Centre**: platform 1200 x 1200 x 200 with 2 ramps (18.4 degrees), 120 cm parapet with gaps, four tall L-shaped corner walls (260 cm
  above the deck, 160 cm legs), two tall slabs north / south of the Plasma Launcher station (it is entered from the ramps side), 6 pillars
  200 x 200 x 800, 6 zombie-mode starts on top (Z 300).
* **Yards** (blue; red is the 180 degree rotation): a bunker at X -1750..-1250, Y 300..700 (500 x 400 room, 30 cm walls, 300 high, roof,
  doorways 200 wide on the west and south sides, window slit Z 110..180 facing the centre, dim interior light); in the other half two
  staggered tall slabs (X -1880 and X -1450, perpendicular to the long axis); two LOW crates and two MID crates (Y +-830) that screen
  the lane wall gaps.
* **Lanes** at Y = +-1500: broken walls (360 cm) with 400 / 600 cm gaps; per lane (see `LANE_PIECES`) tall crates and 230 cm baffle walls
  attached to the lane wall or the outer wall, staggered left / right so no one sees down more than about a third of the lane, plus a few
  LOW crates. Two catwalks (deck top at 330 cm, reached with the two step crates of 120 / 230 cm at one end of each deck, or from the arena
  side with two 230 cm crates at the parapet gaps - the double jump alone, about 310 cm at best, does not reach 330): 120 cm parapet along
  the arena edge with two 250 cm gaps, a roofed middle bay with the weapon station behind a tall shield, two dividers on the back wall,
  partial end walls, and a 180 cm passage along the inner side.
* **Gameplay**: 8 neutral starts (PvP only), 12 `ShoterZombieSpawnPoint` in decorative vent alcoves facing inward (2 with `MinWave` 3),
  7 `ShoterBuyStation`: Pulse Rifle and Scatter (lane walls, middle side), two ammo stations, Rail (catwalk A), Ripper (catwalk B),
  Plasma Launcher (centre). `PvPCooldown` per station is set from `PVP_COOLDOWN`, `CostOverride` is -1.
* **Navigation**: one `NavMeshBoundsVolume` scaled to 6400 x 4400 x 1200 cm (arena only).
* **Lighting**: bluish dim moon (`DirectionalLight` 2 lux, atmosphere sun light, shadows), `SkyAtmosphere`, real-time-capture `SkyLight`,
  `ExponentialHeightFog` (volumetric, bluish), 37 unshadowed point lights and 8 rect lights next to the neon, unbound
  `PostProcessVolume` (bloom 1.2, auto exposure clamped to brightness 0.5..2 = EV100 2..4 when the project uses the extended luminance
  range, slight vignette, no motion blur). All lights are Movable (the project disables static lighting, Lumen is on).
* **Skyline**: 20 dark towers with glowing bands outside the walls (`ADD_SKYLINE`).
* **Layout check** (log line `layout check passed`): starts/spawns must keep 120 cm of clearance, and a 25 cm grid with the nav agent
  radius (42 cm) must connect every start, zombie spawn and ground/platform station (ramps join the two layers).

### The menu camera

`AShoterMenuPlayerController` orbits the origin at r = 22 m, h = 6.5 m. The ring is inside the arena except for the north/south wall
sections (the walls span |Y| 20..22 m), where the camera is inside the wall volume: single sided walls are not rendered from inside, so
the arena stays visible. Nothing taller than 6 m stands on the ring (pillars are at least 4 m away from it); the two axis pillars at
X = +-17 m pass in front of the platform for about two seconds per revolution.

## Tuning the look (nothing could be previewed while writing these scripts)

* Too dark: raise `MOON_INTENSITY` / `SKY_LIGHT_INTENSITY`, lower the first number of `EXPOSURE_BRIGHTNESS_RANGE` (0.25 = one stop
  more sensitivity), or raise `LOCAL_LIGHT_SCALE`. Too bright: do the opposite.
* Neon strength is `EmissiveStrength` of the `MI_Neon_*` instances (10) and of `MI_Wall*` (8 - 9); floor lines are `MI_Floor*` (3 - 4).
* Instance colours/metallic are in `_instances()` in `shoter_materials.py`; the base materials keep the documented defaults.
* After editing a constant just re-run the build; maps are rebuilt, materials are updated in place.

## If something fails (where to look first)

The start of the log has an `API probe:` line listing missing `unreal.*` names, unavailable editor subsystems and missing native
classes. Then read the `[ShoterBuild]` warnings/errors (every property that could not be set is reported once).

The log tells you. The calls most likely to need a tweak on UE 5.8:

1. Native class loading `unreal.load_class(None, "/Script/ShoterAAA.X")` - the module must be compiled.
2. `unreal.EditorLoadingAndSavingUtils.new_blank_map(False)` / `save_map(world, path)` (fallbacks: `LevelEditorSubsystem.new_level`, `EditorLevelLibrary`).
3. `EditorActorSubsystem.spawn_actor_from_object(NavMeshBoundsVolume.static_class(), ...)` - volumes need a brush; the script measures the
   brush with `get_actor_bounds` and warns if it is empty.
4. Property names set through `shoter_common.set_prop` (it tries snake_case, the raw name and a fuzzy match on `dir()`): light components,
   `PostProcessSettings` (`bOverride_*` flags), fog (`bEnableVolumetricFog` vs `VolumetricFog`), `SkyLightComponent.bRealTimeCapture`,
   `AssetImportTask`, `SoundWave.bLooping/Volume`, project classes (`team_id`, `use_in_zombies`, `use_in_pv_p`, ...).
5. `MaterialEditingLibrary`: `create_material_expression`, `connect_material_expressions` (empty input name = first input),
   `connect_material_property`, `get_statistics`, `set_material_instance_*_parameter_value`, `update_material_instance`.

## Character look (neon trooper / corrupted android)

- `M_Shoter_Trooper` (dark armour, takes the mannequin slot textures through `BaseTex/NormalTex/MRATex`) and `M_Shoter_TeamRim` (additive fresnel overlay, `TeamColor`, `RimStrength`) are built by `shoter_materials.py`.
- Kit pieces live in `Source/ShoterAAA/Core/ShoterCharacterKit.cpp` (tables `GetTrooperKit`, `GetZombieKit`), authored in the actor frame of the reference pose.
- Screenshot dev switch: `Tools\Build\Screenshots.ps1 -Mode TDM|Zombies -Extra "-ShoterShotMirror"` takes 9 shots with third-person close-ups (self front, self back, nearest bot/zombie front and back, far shot, profile, ragdoll, far shot 2). `-ShoterKitLog` logs every bone of the mesh in the actor frame.

## Dev switches: arms and reload screenshots

Used with `Tools\Build\Screenshots.ps1 -Bots 0 -Extra "<switches>"` (all `!UE_BUILD_SHIPPING`; add `-benchmark -fps=60` so the captures do not stall the game clock).

* `-ShoterShotBurst -ShoterShotBurstFrames=N -ShoterShotBurstGapMs=N`: consecutive frames of one scene.
* `-ShoterShotNoFire`: never pull the trigger (hip views of the arms). With `-ShoterShotPitch=80` / `-80` it proves that no torso or shoulder is in view looking up/down; `-ShoterShotFireAds` gives the ADS sight picture.
* `-ShoterShotReload`: fires one round, then reloads at the first burst frame (use e.g. `-ShoterShotBurstFrames=8 -ShoterShotBurstGapMs=240`): first-person reload sequence.
* `-ShoterShotReload -ShoterShotMirror` (nearest bot) or `-ShoterShotMirror -ShoterShotMirrorSelf` (local pawn, with `-ShoterShotAzimuth=`/`-ShoterShotDistance=`): the same reload seen from the third-person camera.
* `-ShoterShotWeapon=<PulsePistol|PulseRifle|Ripper|Scatter|Rail|PlasmaLauncher>` picks the weapon.

New sounds (`Tools\Audio\generate_sfx.py`, imported by the audio step): `SFX_MagOut`, `SFX_MagIn`, `SFX_Charge`.
