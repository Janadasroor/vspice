import bpy
import math

# ============================================================
# AI-Generated 3D Inductor Coil for PCB Footprint
# Created by Qwen AI talking to Blender 5.1.0
# ============================================================

# Clear default scene
bpy.ops.wm.read_factory_settings(use_empty=True)

# --- Helper Functions ---
def create_material(name, color, metallic=0.5, roughness=0.3, alpha=1.0):
    """Create a PBR material with optional transparency"""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs['Base Color'].default_value = (*color, alpha)
        bsdf.inputs['Metallic'].default_value = metallic
        bsdf.inputs['Roughness'].default_value = roughness
        if alpha < 1.0:
            mat.blend_method = 'BLEND'
    return mat

# --- Create Materials ---
mat_copper = create_material("Copper_Wire", (0.75, 0.5, 0.2), metallic=0.9, roughness=0.3)
mat_core = create_material("Ferrite_Core", (0.15, 0.15, 0.15), metallic=0.1, roughness=0.8)
mat_body = create_material("Epoxy_Body", (0.05, 0.05, 0.05), metallic=0.0, roughness=0.9, alpha=0.1)
mat_lead = create_material("Tin_Lead", (0.8, 0.8, 0.8), metallic=0.85, roughness=0.25)
mat_silk = create_material("Silkscreen", (1.0, 1.0, 1.0), metallic=0.0, roughness=0.5)

# ============================================================
# INDUCTOR SPECIFICATIONS (Axial Lead Type)
# ============================================================
# - Wire diameter: 0.8mm
# - Coil outer diameter: 6mm
# - Coil length: 10mm
# - Number of turns: 15
# - Ferrite core diameter: 4mm
# - Lead spacing: 15mm (pitch)
# - Total length: ~20mm
# ============================================================

wire_radius = 0.4      # Wire radius (0.8mm diameter)
coil_radius = 2.6      # Coil radius (5.2mm OD, accounts for wire thickness)
coil_length = 8        # Coil body length
num_turns = 15
core_radius = 1.8      # Ferrite core radius (3.6mm diameter)
lead_spacing = 7.5     # Lead spacing (15mm pitch, half on each side)
lead_length = 6        # Lead extension beyond coil
lead_radius = 0.4      # Lead wire radius

# --- 1. Create Ferrite Core (Cylindrical) ---
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=core_radius,
    depth=coil_length + 1,
    location=(0, 0, 0)
)
core = bpy.context.active_object
core.name = "Ferrite_Core"
core.data.materials.append(mat_core)

# --- 2. Create Coil Windings using Curve ---
# Create a helical curve for the coil
curve_data = bpy.data.curves.new(name="Coil_Curve", type='CURVE')
curve_data.dimensions = '3D'
curve_data.resolution_u = 64
curve_data.bevel_depth = wire_radius
curve_data.bevel_resolution = 8

# Generate helix points
spline = curve_data.splines.new('POLY')
points_per_turn = 64
total_points = num_turns * points_per_turn + 1
spline.points.add(total_points - 1)

pitch = coil_length / num_turns
angle_step = (2 * math.pi) / points_per_turn

for i in range(total_points):
    turn = i / points_per_turn
    angle = turn * 2 * math.pi
    z = (turn - num_turns / 2) * pitch
    x = coil_radius * math.cos(angle)
    y = coil_radius * math.sin(angle)
    spline.points[i].co = (x, y, z, 1.0)

# Create curve object
coil_obj = bpy.data.objects.new("Copper_Coil", curve_data)
bpy.context.collection.objects.link(coil_obj)
coil_obj.data.materials.append(mat_copper)

# --- 3. Create Left Lead ---
bpy.ops.mesh.primitive_cylinder_add(
    vertices=16,
    radius=lead_radius,
    depth=lead_length,
    location=(-(lead_spacing + lead_length/2), 0, 0)
)
left_lead = bpy.context.active_object
left_lead.name = "Left_Lead"
left_lead.data.materials.append(mat_lead)

# Bend left lead down
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.object.mode_set(mode='OBJECT')
left_lead.rotation_euler = (0, math.radians(90), 0)
left_lead.location.x = -(lead_spacing + lead_length/2)
left_lead.location.z = 0

# --- 4. Create Right Lead ---
bpy.ops.mesh.primitive_cylinder_add(
    vertices=16,
    radius=lead_radius,
    depth=lead_length,
    location=(lead_spacing + lead_length/2, 0, 0)
)
right_lead = bpy.context.active_object
right_lead.name = "Right_Lead"
right_lead.data.materials.append(mat_lead)
right_lead.rotation_euler = (0, math.radians(90), 0)
right_lead.location.x = lead_spacing + lead_length/2
right_lead.location.z = 0

# --- 5. Create End Caps (Epoxy Body) ---
# Left end cap
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=coil_radius + wire_radius + 0.2,
    depth=1.5,
    location=(-(coil_length/2 + 0.5), 0, 0)
)
left_cap = bpy.context.active_object
left_cap.name = "Left_End_Cap"
left_cap.data.materials.append(mat_body)

# Right end cap
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=coil_radius + wire_radius + 0.2,
    depth=1.5,
    location=((coil_length/2 + 0.5), 0, 0)
)
right_cap = bpy.context.active_object
right_cap.name = "Right_End_Cap"
right_cap.data.materials.append(mat_body)

# --- 6. Skip Body (show coil directly) ---
# We'll skip the opaque body to show the beautiful copper coil

# Create a thin transparent shell instead
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=coil_radius + wire_radius + 0.4,
    depth=coil_length + 2,
    location=(0, 0, 0)
)
body = bpy.context.active_object
body.name = "Epoxy_Shell"
body.data.materials.append(mat_body)
body.active_material.blend_method = 'BLEND'
body.active_material.alpha = 0.08

# --- 7. Add Silkscreen Label ---
bpy.ops.object.text_add(location=(0, coil_radius + wire_radius + 0.5, 0))
label = bpy.context.active_object
label.name = "Silkscreen_Label"
label.data.body = "100uH"
label.data.size = 1.2
label.rotation_euler = (math.radians(90), 0, 0)
label.data.materials.append(mat_silk)

# --- 8. Add Polarity Band (White stripe) ---
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=coil_radius + wire_radius + 0.35,
    depth=0.8,
    location=(coil_length/2 - 1, 0, 0)
)
band = bpy.context.active_object
band.name = "Polarity_Band"
band.data.materials.append(mat_silk)

# --- 9. Create Footprint Pads (PCB Mount Points) ---
pad_radius = 1.5
pad_thickness = 0.3

# Left pad
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=pad_radius,
    depth=pad_thickness,
    location=(-(lead_spacing + lead_length - 0.5), 0, -0.8)
)
left_pad = bpy.context.active_object
left_pad.name = "PCB_Pad_Left"
left_pad.data.materials.append(mat_copper)

# Right pad
bpy.ops.mesh.primitive_cylinder_add(
    vertices=32,
    radius=pad_radius,
    depth=pad_thickness,
    location=(lead_spacing + lead_length - 0.5, 0, -0.8)
)
right_pad = bpy.context.active_object
right_pad.name = "PCB_Pad_Right"
right_pad.data.materials.append(mat_copper)

# --- 10. Add Dimension Markers (for documentation) ---
# This helps visualize the inductor specs

# --- Lighting ---
bpy.ops.object.light_add(type='SUN', location=(5, 5, 10))
sun = bpy.context.active_object
sun.name = "Main_Sun"
sun.data.energy = 5

bpy.ops.object.light_add(type='POINT', location=(-5, -3, 5))
fill = bpy.context.active_object
fill.name = "Fill_Light"
fill.data.energy = 200

bpy.ops.object.light_add(type='AREA', location=(0, 8, 3))
area = bpy.context.active_object
area.name = "Top_Light"
area.data.energy = 100
area.data.size = 5

# --- Camera ---
bpy.ops.object.camera_add(location=(18, -14, 12))
camera = bpy.context.active_object
camera.name = "Inductor_Camera"
camera.rotation_euler = (math.radians(62), 0, math.radians(38))

bpy.context.scene.camera = camera

# Add track-to constraint so camera looks at inductor
camera.constraints.new(type='TRACK_TO')
camera.constraints['Track To'].target = core
camera.constraints['Track To'].track_axis = 'TRACK_NEGATIVE_Z'
camera.constraints['Track To'].up_axis = 'UP_Y'

bpy.context.view_layer.update()

# --- Render Settings ---
bpy.context.scene.render.engine = 'CYCLES'
bpy.context.scene.cycles.samples = 256
bpy.context.scene.render.resolution_x = 2560
bpy.context.scene.render.resolution_y = 1440
bpy.context.scene.render.filepath = '/tmp/inductor_footprint_3d.png'
bpy.context.scene.render.image_settings.file_format = 'PNG'
bpy.context.scene.render.film_transparent = True

# --- Render ---
print(" Rendering inductor coil for PCB footprint...")
bpy.ops.render.render(write_still=True)
print("✅ Done! Saved to /tmp/inductor_footprint_3d.png")

# Save blend file
bpy.ops.wm.save_as_mainfile(filepath='/tmp/inductor_footprint.blend')
print("💾 Scene saved to /tmp/inductor_footprint.blend")

# Print specifications
print("\n" + "="*60)
print("📊 INDUCTOR FOOTPRINT SPECIFICATIONS")
print("="*60)
print(f"  Type:            Axial Lead Inductor")
print(f"  Wire Diameter:   {wire_radius*2:.1f}mm")
print(f"  Coil OD:         {coil_radius*2 + wire_radius*2:.1f}mm")
print(f"  Coil Length:     {coil_length:.1f}mm")
print(f"  Number of Turns: {num_turns}")
print(f"  Core Diameter:   {core_radius*2:.1f}mm")
print(f"  Lead Pitch:      {lead_spacing*2:.1f}mm")
print(f"  Total Length:    {lead_spacing*2 + lead_length*2:.1f}mm")
print(f"  Pad Diameter:    {pad_radius*2:.1f}mm")
print("="*60)
