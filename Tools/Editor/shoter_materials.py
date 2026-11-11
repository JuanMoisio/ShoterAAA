"""
shoter_materials.py -- step 2: builds the project's materials and material instances in /Game/Shoter/Materials.

Materials (graphs are rebuilt from scratch on every run; parameter names are a contract with the C++ code):
  M_Shoter_Surface      opaque lit        BaseColor, EmissiveColor | Metallic, Roughness, EmissiveStrength
  M_Shoter_Glow         unlit additive    Color | Intensity
  M_Shoter_Character    opaque lit        BaseColor, TeamColor, EmissiveColor | Metallic, Roughness, RimStrength, EmissiveStrength
  M_Shoter_ImpactDecal  deferred decal    soft round burn mark
  M_Shoter_ScorchDecal  deferred decal    big irregular scorch mark
  M_Shoter_Floor        opaque lit        world-aligned panels + emissive grid lines
  M_Shoter_Wall         opaque lit        world-aligned panels + horizontal emissive strip at a world height
Instances: MI_Floor*, MI_Wall*, MI_Metal_*, MI_Neon_*  (see INSTANCES).
"""
import math

import unreal

import shoter_common as C

MATERIAL_DIR = "/Game/Shoter/Materials"
TAU = 2.0 * math.pi

CYAN = C.NEON["cyan"]
MAGENTA = C.NEON["magenta"]
BLUE = C.NEON["blue"]
RED = C.NEON["red"]
ORANGE = C.NEON["orange"]


# =========================================================================== graph builder

class Graph(object):
    """Thin convenience layer over unreal.MaterialEditingLibrary: creates, positions and wires expression nodes."""

    LEVEL_STEP = 250
    ROW_STEP = 120

    def __init__(self, material):
        self.material = material
        self.mel = unreal.MaterialEditingLibrary
        self._levels = {}      # id(expression) -> depth from the parameters (for node positions)
        self._rows = {}        # depth -> number of nodes already placed at that depth
        self._keep = []        # keeps the Python wrappers alive so id() stays unique
        self._consts = {}      # float -> shared Constant node
        self.link_failures = 0
        self.node_count = 0

    # ---- low level -----------------------------------------------------------------------------

    def _new(self, class_name, inputs=(), **props):
        cls = getattr(unreal, class_name, None)
        if cls is None:
            raise RuntimeError("unreal.{} does not exist in this engine version".format(class_name))
        level = 0
        for source in inputs:
            level = max(level, self._levels.get(id(source), 0) + 1)
        row = self._rows.get(level, 0)
        self._rows[level] = row + 1
        x = -3200 + level * self.LEVEL_STEP
        y = -400 + row * self.ROW_STEP
        expression = self.mel.create_material_expression(self.material, cls, x, y)
        if expression is None:
            raise RuntimeError("create_material_expression returned None for {}".format(class_name))
        self._keep.append(expression)
        self._levels[id(expression)] = level
        self.node_count += 1
        for key, value in props.items():
            C.set_prop(expression, key, value)
        return expression

    def link(self, source, destination, input_name="", output_name="", fallback_names=()):
        """
        Connects source's output to destination's input (empty names = first input/output).
        fallback_names are tried when the first attempt reports failure (some nodes name their only input "Input").
        """
        ok = self.mel.connect_material_expressions(source, output_name, destination, input_name)
        if ok is False:
            for alternative in fallback_names:
                ok = self.mel.connect_material_expressions(source, output_name, destination, alternative)
                if ok is not False:
                    break
        if ok is False:
            self.link_failures += 1
            C.warn("connect_material_expressions failed: {} -> {}.{}".format(
                type(source).__name__, type(destination).__name__, input_name or "<first input>"))
        return ok

    def output(self, expression, property_name, output_name=""):
        """Connects an expression to a material output pin, e.g. output(expr, 'BASE_COLOR')."""
        prop = getattr(unreal.MaterialProperty, "MP_" + property_name)
        ok = self.mel.connect_material_property(expression, output_name, prop)
        if ok is False:
            self.link_failures += 1
            C.warn("connect_material_property failed for MP_{}".format(property_name))
        return ok

    # ---- leaves --------------------------------------------------------------------------------

    def _checked_parameter(self, class_name, name, default):
        expression = self._new(class_name, ParameterName=name, DefaultValue=default, Group="Shoter")
        if str(C.get_prop(expression, "ParameterName", "")) != name:
            raise RuntimeError("could not set ParameterName '{}' on {} (parameter names are a contract with the C++ code)".format(name, class_name))
        return expression

    def scalar(self, name, default):
        return self._checked_parameter("MaterialExpressionScalarParameter", name, float(default))

    def vector(self, name, rgb, alpha=1.0):
        return self._checked_parameter("MaterialExpressionVectorParameter", name, C.lin(rgb[0], rgb[1], rgb[2], alpha))

    def const(self, value):
        key = float(value)
        if key not in self._consts:
            self._consts[key] = self._new("MaterialExpressionConstant", R=key)
        return self._consts[key]

    def const2(self, x, y):
        return self._new("MaterialExpressionConstant2Vector", R=float(x), G=float(y))

    def texcoord(self, index=0):
        return self._new("MaterialExpressionTextureCoordinate", CoordinateIndex=int(index))

    def world_position(self):
        return self._new("MaterialExpressionWorldPosition")

    def vertex_normal(self):
        return self._new("MaterialExpressionVertexNormalWS")

    # ---- operators (numbers are promoted to Constant nodes) ------------------------------------

    def val(self, x):
        if isinstance(x, (int, float)):
            return self.const(x)
        return x

    def _binary(self, class_name, a, b, a_name="A", b_name="B"):
        a, b = self.val(a), self.val(b)
        node = self._new(class_name, inputs=(a, b))
        self.link(a, node, a_name)
        self.link(b, node, b_name)
        return node

    def _unary(self, class_name, a, **props):
        a = self.val(a)
        node = self._new(class_name, inputs=(a,), **props)
        self.link(a, node, "", fallback_names=("Input", "A"))
        return node

    def add(self, a, b):
        return self._binary("MaterialExpressionAdd", a, b)

    def sub(self, a, b):
        return self._binary("MaterialExpressionSubtract", a, b)

    def mul(self, a, b):
        return self._binary("MaterialExpressionMultiply", a, b)

    def div(self, a, b):
        return self._binary("MaterialExpressionDivide", a, b)

    def vmax(self, a, b):
        return self._binary("MaterialExpressionMax", a, b)

    def vmin(self, a, b):
        return self._binary("MaterialExpressionMin", a, b)

    def dot(self, a, b):
        return self._binary("MaterialExpressionDotProduct", a, b)

    def lerp(self, a, b, alpha):
        a, b, alpha = self.val(a), self.val(b), self.val(alpha)
        node = self._new("MaterialExpressionLinearInterpolate", inputs=(a, b, alpha))
        self.link(a, node, "A")
        self.link(b, node, "B")
        self.link(alpha, node, "Alpha")
        return node

    def append(self, a, b):
        return self._binary("MaterialExpressionAppendVector", a, b)

    def absf(self, a):
        return self._unary("MaterialExpressionAbs", a)

    def frac(self, a):
        return self._unary("MaterialExpressionFrac", a)

    def floor(self, a):
        return self._unary("MaterialExpressionFloor", a)

    def sat(self, a):
        return self._unary("MaterialExpressionSaturate", a)

    def one_minus(self, a):
        return self._unary("MaterialExpressionOneMinus", a)

    def sine(self, a):
        """sin(a * 2*pi) with the node's default period of 1."""
        return self._unary("MaterialExpressionSine", a)

    def mask(self, a, r=False, g=False, b=False, alpha=False):
        return self._unary("MaterialExpressionComponentMask", a, R=bool(r), G=bool(g), B=bool(b), A=bool(alpha))

    def fresnel(self, exponent, base_reflect=0.0):
        return self._new("MaterialExpressionFresnel", Exponent=float(exponent), BaseReflectFraction=float(base_reflect))

    def atan2(self, y, x):
        y, x = self.val(y), self.val(x)
        node = self._new("MaterialExpressionArctangent2", inputs=(y, x))
        self.link(y, node, "Y")
        self.link(x, node, "X")
        return node

    def sphere_mask(self, a, b, radius, hardness):
        """radius/hardness are expression nodes (hardness 0..1: 0 = soft ramp, 1 = hard edge)."""
        node = self._new("MaterialExpressionSphereMask", inputs=(a, b, radius, hardness),
                         AttenuationRadius=0.5, HardnessPercent=0.0)
        self.link(a, node, "A")
        self.link(b, node, "B")
        self.link(radius, node, "Radius")
        self.link(hardness, node, "Hardness")
        return node


# =========================================================================== asset helpers

def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _get_or_create_material(name):
    """Returns (material, created).  An existing material keeps its asset but loses all expression nodes."""
    path = "{}/{}".format(MATERIAL_DIR, name)
    if C.asset_exists(path):
        material = C.load_asset(path)
        if material is None or not isinstance(material, unreal.Material):
            raise RuntimeError("{} exists but is not a Material".format(path))
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False
    material = _tools().create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("could not create {}".format(path))
    return material, True


def _verify_parameters(material, name, scalars, vectors):
    """Checks that the compiled material exposes the parameter names C++ relies on."""
    mel = unreal.MaterialEditingLibrary
    try:
        found_scalars = set(str(n) for n in mel.get_scalar_parameter_names(material))
        found_vectors = set(str(n) for n in mel.get_vector_parameter_names(material))
    except Exception as exc:
        C.warn("  cannot list parameters of {} ({}); skipping contract check".format(name, exc))
        return True
    missing = ["scalar " + s for s in scalars if s not in found_scalars] + ["vector " + v for v in vectors if v not in found_vectors]
    if missing:
        C.error("  {} is missing contract parameters: {}".format(name, ", ".join(missing)))
        return False
    return True


def _finish(material, name, graph, scalars=(), vectors=()):
    """Recompiles, checks the parameter contract and saves.  Returns True when everything looked fine."""
    mel = unreal.MaterialEditingLibrary
    try:
        mel.recompile_material(material)
    except Exception:
        C.log_exception("recompile_material({})".format(name))
        return False
    ok = graph.link_failures == 0
    ok &= _verify_parameters(material, name, scalars, vectors)
    try:
        stats = mel.get_statistics(material)
        C.log("  {}: {} nodes, {} pixel shader instructions".format(
            name, graph.node_count, C.get_prop(stats, "NumPixelShaderInstructions", "?")))
    except Exception:
        C.log("  {}: {} nodes".format(name, graph.node_count))
    if not C.save_asset(material, "{}/{}".format(MATERIAL_DIR, name)):
        C.warn("  could not save {}".format(name))
        ok = False
    return ok


def _setup(material, domain="MD_SURFACE", blend="BLEND_OPAQUE", shading="MSM_DEFAULT_LIT", two_sided=False, usage=(), extra=None):
    props = {
        "MaterialDomain": getattr(unreal.MaterialDomain, domain),
        "BlendMode": getattr(unreal.BlendMode, blend),
        "ShadingModel": getattr(unreal.MaterialShadingModel, shading),
        "TwoSided": bool(two_sided),
    }
    for flag in usage:
        props[flag] = True
    if extra:
        props.update(extra)
    C.set_props(material, props)


# =========================================================================== shared pattern maths

def _panel_pattern(g, uv):
    """
    uv: float2 expression in panel units (1.0 = one panel).
    Returns (edge_distance, hash): distance to the nearest panel seam (0 on the seam, 0.5 in the middle)
    and a per-panel random number 0..1.
    """
    centered = g.sub(g.frac(uv), 0.5)
    folded = g.absf(centered)
    edge = g.sub(0.5, g.vmax(g.mask(folded, r=True), g.mask(folded, g=True)))
    cell = g.floor(uv)
    noise = g.sine(g.dot(cell, g.const2(12.9898, 78.233)))
    panel_hash = g.frac(g.mul(noise, 43758.5453))
    return edge, panel_hash


def _seam_line(g, edge, half_line):
    """1 on the seam (emissive grid line), 0 beyond half_line panel units from it, with a short soft edge."""
    soft = g.mul(half_line, 0.6)
    return g.sat(g.div(g.sub(half_line, edge), soft))


def _seam_groove(g, edge, half_line):
    """Darkening factor 0.45..1: dark groove on the seam fading out over three half-line widths."""
    groove_width = g.mul(half_line, 3.0)
    groove = g.sat(g.div(g.sub(groove_width, edge), groove_width))
    return g.sub(1.0, g.mul(groove, 0.55))


# =========================================================================== the materials

def build_surface():
    material, _ = _get_or_create_material("M_Shoter_Surface")
    _setup(material, usage=("bUsedWithSkeletalMesh", "bUsedWithInstancedStaticMeshes", "bUsedWithMorphTargets"))
    g = Graph(material)
    base = g.vector("BaseColor", (0.5, 0.5, 0.5))
    metallic = g.scalar("Metallic", 0.5)
    roughness = g.scalar("Roughness", 0.4)
    emissive_color = g.vector("EmissiveColor", (0.0, 0.0, 0.0))
    emissive_strength = g.scalar("EmissiveStrength", 0.0)
    g.output(base, "BASE_COLOR")
    g.output(metallic, "METALLIC")
    g.output(roughness, "ROUGHNESS")
    g.output(g.mul(emissive_color, emissive_strength), "EMISSIVE_COLOR")
    return _finish(material, "M_Shoter_Surface", g,
                   ("Metallic", "Roughness", "EmissiveStrength"), ("BaseColor", "EmissiveColor"))


def build_glow():
    material, _ = _get_or_create_material("M_Shoter_Glow")
    _setup(material, blend="BLEND_ADDITIVE", shading="MSM_UNLIT", two_sided=True,
           usage=("bUsedWithSkeletalMesh", "bUsedWithInstancedStaticMeshes"))
    g = Graph(material)
    color = g.vector("Color", (1.0, 1.0, 1.0))
    intensity = g.scalar("Intensity", 20.0)
    g.output(g.mul(color, intensity), "EMISSIVE_COLOR")
    return _finish(material, "M_Shoter_Glow", g, ("Intensity",), ("Color",))


def build_character():
    material, _ = _get_or_create_material("M_Shoter_Character")
    _setup(material, usage=("bUsedWithSkeletalMesh", "bUsedWithInstancedStaticMeshes", "bUsedWithMorphTargets"))
    g = Graph(material)
    base = g.vector("BaseColor", (0.07, 0.08, 0.09))
    team = g.vector("TeamColor", (0.10, 0.40, 1.0))
    emissive_color = g.vector("EmissiveColor", (0.0, 0.0, 0.0))
    metallic = g.scalar("Metallic", 0.8)
    roughness = g.scalar("Roughness", 0.35)
    rim_strength = g.scalar("RimStrength", 4.0)
    emissive_strength = g.scalar("EmissiveStrength", 0.0)

    rim = g.mul(g.mul(g.fresnel(4.0), team), rim_strength)
    emissive = g.add(rim, g.mul(emissive_color, emissive_strength))
    g.output(base, "BASE_COLOR")
    g.output(metallic, "METALLIC")
    g.output(roughness, "ROUGHNESS")
    g.output(emissive, "EMISSIVE_COLOR")
    return _finish(material, "M_Shoter_Character", g,
                   ("Metallic", "Roughness", "RimStrength", "EmissiveStrength"), ("BaseColor", "TeamColor", "EmissiveColor"))


def _build_decal(name, radius, hardness, burn, roughness, ember, ember_strength, opacity_scale, wobble):
    material, _ = _get_or_create_material(name)
    _setup(material, domain="MD_DEFERRED_DECAL", blend="BLEND_TRANSLUCENT", shading="MSM_DEFAULT_LIT",
           extra={"DecalBlendMode": unreal.DecalBlendMode.DBM_TRANSLUCENT})
    g = Graph(material)

    burn_color = g.vector("BurnColor", burn)
    roughness_param = g.scalar("Roughness", roughness)
    ember_color = g.vector("EmissiveColor", ember)
    ember_param = g.scalar("EmissiveStrength", ember_strength)
    opacity_param = g.scalar("OpacityScale", opacity_scale)
    radius_param = g.scalar("Radius", radius)
    hardness_param = g.scalar("Hardness", hardness)

    # Decal UVs run 0..1 across the projection box; a soft disc centred on 0.5 is the mark.
    uv = g.texcoord(0)
    disc = g.sphere_mask(uv, g.const2(0.5, 0.5), radius_param, hardness_param)

    if wobble:
        # Irregular outline: modulate the falloff with two angular harmonics.
        offset = g.sub(uv, 0.5)
        angle = g.atan2(g.mask(offset, g=True), g.mask(offset, r=True))
        harmonic_a = g.sine(g.mul(angle, 5.0 / TAU))
        harmonic_b = g.sine(g.add(g.mul(angle, 9.0 / TAU), 0.31))
        factor = g.add(g.add(1.0, g.mul(harmonic_a, 0.30)), g.mul(harmonic_b, 0.15))
        disc = g.sat(g.mul(disc, factor))

    opacity = g.sat(g.mul(disc, opacity_param))
    core = g.mul(disc, disc)
    core = g.mul(core, core) if wobble else g.mul(core, disc)
    emissive = g.mul(g.mul(ember_color, core), ember_param)

    g.output(burn_color, "BASE_COLOR")
    g.output(roughness_param, "ROUGHNESS")
    g.output(emissive, "EMISSIVE_COLOR")
    g.output(opacity, "OPACITY")
    return _finish(material, name, g)


def build_impact_decal():
    return _build_decal("M_Shoter_ImpactDecal", radius=0.42, hardness=0.30, burn=(0.018, 0.014, 0.012), roughness=0.85,
                        ember=(1.0, 0.32, 0.05), ember_strength=2.5, opacity_scale=0.95, wobble=False)


def build_scorch_decal():
    return _build_decal("M_Shoter_ScorchDecal", radius=0.50, hardness=0.0, burn=(0.006, 0.005, 0.005), roughness=0.95,
                        ember=(0.9, 0.20, 0.03), ember_strength=1.2, opacity_scale=0.92, wobble=True)


def build_floor():
    material, _ = _get_or_create_material("M_Shoter_Floor")
    _setup(material)
    g = Graph(material)
    base = g.vector("BaseColor", (0.035, 0.040, 0.050))
    line_color = g.vector("LineColor", CYAN)
    line_width = g.scalar("LineWidth", 0.01)
    tile_size = g.scalar("TileSize", 200.0)
    emissive_strength = g.scalar("EmissiveStrength", 3.0)
    metallic = g.scalar("Metallic", 0.85)
    roughness = g.scalar("Roughness", 0.30)
    roughness_variation = g.scalar("RoughnessVariation", 0.15)

    # World aligned panel coordinates: WorldPosition.xy / TileSize, so scaled cubes never stretch the pattern.
    xy = g.mask(g.world_position(), r=True, g=True)
    uv = g.div(xy, tile_size)
    half_line = g.vmax(g.mul(line_width, 0.5), 0.0005)
    edge, panel_hash = _panel_pattern(g, uv)
    line_mask = _seam_line(g, edge, half_line)
    groove_factor = _seam_groove(g, edge, half_line)

    tint = g.lerp(0.80, 1.25, panel_hash)
    base_out = g.mul(base, g.mul(tint, groove_factor))
    roughness_out = g.sat(g.add(roughness, g.mul(g.sub(panel_hash, 0.5), roughness_variation)))
    emissive = g.mul(g.mul(line_color, line_mask), emissive_strength)

    g.output(base_out, "BASE_COLOR")
    g.output(metallic, "METALLIC")
    g.output(roughness_out, "ROUGHNESS")
    g.output(emissive, "EMISSIVE_COLOR")
    return _finish(material, "M_Shoter_Floor", g,
                   ("LineWidth", "TileSize", "EmissiveStrength", "Metallic", "Roughness"), ("BaseColor", "LineColor"))


def build_wall():
    material, _ = _get_or_create_material("M_Shoter_Wall")
    _setup(material)
    g = Graph(material)
    base = g.vector("BaseColor", (0.040, 0.045, 0.055))
    strip_color = g.vector("StripColor", CYAN)
    strip_height = g.scalar("StripHeight", 300.0)
    strip_width = g.scalar("StripWidth", 12.0)
    tile_size = g.scalar("TileSize", 200.0)
    emissive_strength = g.scalar("EmissiveStrength", 8.0)
    metallic = g.scalar("Metallic", 0.80)
    roughness = g.scalar("Roughness", 0.40)

    position = g.world_position()
    x = g.mask(position, r=True)
    y = g.mask(position, g=True)
    z = g.mask(position, b=True)

    # Cubes are axis aligned: use X along faces whose normal points along Y, Y along faces whose normal points
    # along X, and plain XY on top faces.  Vertical coordinate is the world height.
    normal = g.absf(g.vertex_normal())
    nx = g.mask(normal, r=True)
    ny = g.mask(normal, g=True)
    nz = g.mask(normal, b=True)
    horizontal_side = g.add(g.mul(x, ny), g.mul(y, nx))
    horizontal = g.lerp(horizontal_side, x, nz)
    vertical = g.lerp(z, y, nz)
    uv = g.div(g.append(horizontal, vertical), tile_size)

    # Panel seams: small fixed half width (3 cm / 200 cm) so they stay thin whatever TileSize is.
    half_line = g.const(0.008)
    edge, panel_hash = _panel_pattern(g, uv)
    groove_factor = _seam_groove(g, edge, half_line)
    tint = g.lerp(0.85, 1.20, panel_hash)
    base_out = g.mul(base, g.mul(tint, groove_factor))

    # Horizontal emissive strip centred on StripHeight, StripWidth cm thick, hidden on top faces.
    half_strip = g.vmax(g.mul(strip_width, 0.5), 0.01)
    distance = g.absf(g.sub(z, strip_height))
    band = g.sat(g.div(g.sub(half_strip, distance), g.mul(half_strip, 0.5)))
    band = g.mul(band, g.one_minus(nz))
    emissive = g.mul(g.mul(strip_color, band), emissive_strength)

    g.output(base_out, "BASE_COLOR")
    g.output(metallic, "METALLIC")
    g.output(roughness, "ROUGHNESS")
    g.output(emissive, "EMISSIVE_COLOR")
    return _finish(material, "M_Shoter_Wall", g,
                   ("StripHeight", "StripWidth", "EmissiveStrength", "Metallic", "Roughness"), ("BaseColor", "StripColor"))


MATERIALS = [
    ("M_Shoter_Surface", build_surface),
    ("M_Shoter_Glow", build_glow),
    ("M_Shoter_Character", build_character),
    ("M_Shoter_ImpactDecal", build_impact_decal),
    ("M_Shoter_ScorchDecal", build_scorch_decal),
    ("M_Shoter_Floor", build_floor),
    ("M_Shoter_Wall", build_wall),
]


# =========================================================================== instances

def _neon(color):
    """M_Shoter_Surface parameters for an opaque, solid light strip that blooms."""
    scale = 0.05
    return (
        {"Metallic": 0.0, "Roughness": 0.45, "EmissiveStrength": 10.0},
        {"BaseColor": (color[0] * scale, color[1] * scale, color[2] * scale), "EmissiveColor": color},
    )


def _tint(color, amount):
    base = (0.090, 0.100, 0.120)
    return tuple(base[i] * (1.0 - amount) + color[i] * amount * 0.25 for i in range(3))


def _instances():
    """(name, parent material, scalar overrides, vector overrides)"""
    floor_base = (0.120, 0.130, 0.155)
    items = [
        ("MI_Floor", "M_Shoter_Floor",
         {"TileSize": 200.0, "LineWidth": 0.020, "EmissiveStrength": 3.0, "Metallic": 0.60, "Roughness": 0.35},
         {"BaseColor": floor_base, "LineColor": CYAN}),
        ("MI_Floor_Blue", "M_Shoter_Floor",
         {"TileSize": 200.0, "LineWidth": 0.024, "EmissiveStrength": 4.0, "Metallic": 0.60, "Roughness": 0.35},
         {"BaseColor": (0.070, 0.090, 0.170), "LineColor": BLUE}),
        ("MI_Floor_Red", "M_Shoter_Floor",
         {"TileSize": 200.0, "LineWidth": 0.024, "EmissiveStrength": 4.0, "Metallic": 0.60, "Roughness": 0.35},
         {"BaseColor": (0.170, 0.080, 0.080), "LineColor": RED}),
        ("MI_Wall", "M_Shoter_Wall",
         {"StripHeight": 300.0, "StripWidth": 12.0, "EmissiveStrength": 8.0, "Metallic": 0.55, "Roughness": 0.40},
         {"BaseColor": (0.100, 0.110, 0.130), "StripColor": CYAN}),
        ("MI_Wall_Blue", "M_Shoter_Wall",
         {"StripHeight": 300.0, "StripWidth": 12.0, "EmissiveStrength": 9.0, "Metallic": 0.55, "Roughness": 0.40},
         {"BaseColor": _tint(BLUE, 0.35), "StripColor": BLUE}),
        ("MI_Wall_Red", "M_Shoter_Wall",
         {"StripHeight": 300.0, "StripWidth": 12.0, "EmissiveStrength": 9.0, "Metallic": 0.55, "Roughness": 0.40},
         {"BaseColor": _tint(RED, 0.35), "StripColor": RED}),
        ("MI_Metal_Dark", "M_Shoter_Surface",
         {"Metallic": 0.80, "Roughness": 0.45, "EmissiveStrength": 0.0},
         {"BaseColor": (0.075, 0.080, 0.095), "EmissiveColor": (0.0, 0.0, 0.0)}),
        ("MI_Metal_Light", "M_Shoter_Surface",
         {"Metallic": 0.80, "Roughness": 0.30, "EmissiveStrength": 0.0},
         {"BaseColor": (0.300, 0.320, 0.350), "EmissiveColor": (0.0, 0.0, 0.0)}),
    ]
    for label, color in (("Cyan", CYAN), ("Magenta", MAGENTA), ("Blue", BLUE), ("Red", RED), ("Orange", ORANGE)):
        scalars, vectors = _neon(color)
        items.append(("MI_Neon_" + label, "M_Shoter_Surface", scalars, vectors))
    return items


def _build_instance(name, parent_name, scalars, vectors):
    mel = unreal.MaterialEditingLibrary
    parent = C.load_asset("{}/{}".format(MATERIAL_DIR, parent_name))
    if parent is None:
        raise RuntimeError("parent material {} not found".format(parent_name))

    path = "{}/{}".format(MATERIAL_DIR, name)
    if C.asset_exists(path):
        instance = C.load_asset(path)
        if instance is None or not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError("{} exists but is not a MaterialInstanceConstant".format(path))
        try:
            mel.clear_all_material_instance_parameters(instance)
        except Exception:
            pass
    else:
        instance = _tools().create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if instance is None:
            raise RuntimeError("could not create {}".format(path))

    try:
        mel.set_material_instance_parent(instance, parent)
    except Exception:
        C.set_prop(instance, "Parent", parent)

    # In UE 5.8 the set_* functions return False even when the value was applied, so verify by reading it back.
    failures = 0
    for key, value in scalars.items():
        mel.set_material_instance_scalar_parameter_value(instance, key, float(value))
        if abs(float(mel.get_material_instance_scalar_parameter_value(instance, key)) - float(value)) > 1e-3:
            failures += 1
            C.warn("  {}: could not set scalar {}".format(name, key))
    for key, value in vectors.items():
        color = C.lin(value[0], value[1], value[2])
        mel.set_material_instance_vector_parameter_value(instance, key, color)
        current = mel.get_material_instance_vector_parameter_value(instance, key)
        if abs(current.r - color.r) > 1e-3 or abs(current.g - color.g) > 1e-3 or abs(current.b - color.b) > 1e-3:
            failures += 1
            C.warn("  {}: could not set vector {}".format(name, key))
    try:
        mel.update_material_instance(instance)
    except Exception:
        pass
    if not C.save_asset(instance, path):
        C.warn("  could not save {}".format(name))
        failures += 1
    return failures == 0


# =========================================================================== entry point

def run():
    """Builds all materials, then all instances.  Returns a dict used by the final summary."""
    result = {"materials_ok": [], "materials_failed": [], "instances_ok": [], "instances_failed": []}
    C.make_directory(MATERIAL_DIR)

    for name, builder in MATERIALS:
        C.log("Material {} ...".format(name))
        try:
            ok = builder()
        except Exception:
            C.log_exception("Material {}".format(name))
            ok = False
        (result["materials_ok"] if ok else result["materials_failed"]).append(name)

    for name, parent, scalars, vectors in _instances():
        C.log("Instance {} (parent {}) ...".format(name, parent))
        try:
            ok = _build_instance(name, parent, scalars, vectors)
        except Exception:
            C.log_exception("Instance {}".format(name))
            ok = False
        (result["instances_ok"] if ok else result["instances_failed"]).append(name)

    C.log("Materials done: {}/{} materials, {}/{} instances".format(
        len(result["materials_ok"]), len(MATERIALS),
        len(result["instances_ok"]), len(result["instances_ok"]) + len(result["instances_failed"])))
    result["problems"] = ["failed: " + name for name in result["materials_failed"] + result["instances_failed"]]
    return result
