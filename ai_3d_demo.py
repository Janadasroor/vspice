import bpy
import math

# ============================================================
# AI-Generated 3D Scene: Parametric Mechanical Assembly
# Created by Qwen AI talking to Blender 5.1.0
# ============================================================

# Clear default scene
bpy.ops.wm.read_factory_settings(use_empty=True)

# --- Helper Functions ---
def create_material(name, color, metallic=0.5, roughness=0.3):
    """Create a PBR material"""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs['Base Color'].default_value = (*color, 1.0)
        bsdf.inputs['Metallic'].default_value = metallic
        bsdf.inputs['Roughness'].default_value = roughness
    return mat

def add_object(obj_type, name, location, material):
    """Add a primitive object with material"""
    loc = tuple(location[:3])  # Ensure 3D location
    if obj_type == "cube":
        bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    elif obj_type == "cylinder":
        bpy.ops.mesh.primitive_cylinder_add(radius=0.5, depth=1, location=loc)
    elif obj_type == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.5, location=loc)
    elif obj_type == "torus":
        bpy.ops.mesh.primitive_torus_add(location=loc, major_radius=0.5, minor_radius=0.15)
    
    obj = bpy.context.active_object
    obj.name = name
    obj.data.materials.append(material)
    return obj

# --- Create Materials ---
mat_base = create_material("Base_Metal", (0.2, 0.25, 0.3), metallic=0.8, roughness=0.2)
mat_accent = create_material("Accent_Red", (0.8, 0.1, 0.1), metallic=0.3, roughness=0.4)
mat_glass = create_material("Glass", (0.9, 0.95, 1.0), metallic=0.0, roughness=0.05)

# --- Create Assembly ---

# Base platform
base = add_object("cube", "Base_Platform", (0, 0, 0.1), mat_base)
base.scale = (3, 3, 0.2)

# Central column
column = add_object("cylinder", "Central_Column", (0, 0, 1.5), mat_base)
column.scale = (0.5, 0.5, 1.0)

# Decorative rings on column
for i in range(3):
    ring = add_object("torus", f"Ring_{i+1}", (0, 0, 0.8 + i * 0.8), mat_accent)
    ring.scale = (1.2, 1.2, 1.0)

# Corner pillars
positions = [(1.2, 1.2, 0), (-1.2, 1.2, 0), (1.2, -1.2, 0), (-1.2, -1.2, 0)]
for idx, pos in enumerate(positions):
    x, y, z = pos
    pillar = add_object("cylinder", f"Pillar_{idx+1}", (x, y, z + 1.2), mat_base)
    pillar.scale = (0.3, 0.3, 1.0)
    
    # Top spheres
    sphere = add_object("sphere", f"Orb_{idx+1}", (x, y, z + 2.5), mat_accent)
    sphere.scale = (0.4, 0.4, 0.4)

# Glass dome on top
dome = add_object("sphere", "Glass_Dome", (0, 0, 3.2), mat_glass)
dome.scale = (0.8, 0.8, 0.6)

# --- Lighting ---
bpy.ops.object.light_add(type='SUN', location=(5, 5, 10))
sun = bpy.context.active_object
sun.name = "Main_Sun"
sun.data.energy = 3

bpy.ops.object.light_add(type='POINT', location=(-3, -3, 4))
point = bpy.context.active_object
point.name = "Fill_Light"
point.data.energy = 100

# --- Camera ---
bpy.ops.object.camera_add(location=(6, -6, 5))
camera = bpy.context.active_object
camera.name = "AI_Camera"
camera.rotation_euler = (math.radians(65), 0, math.radians(45))

# Set camera as active
bpy.context.scene.camera = camera

# --- Render Settings ---
bpy.context.scene.render.engine = 'CYCLES'
bpy.context.scene.cycles.samples = 128
bpy.context.scene.render.resolution_x = 1920
bpy.context.scene.render.resolution_y = 1080
bpy.context.scene.render.filepath = '/tmp/ai_generated_3d.png'
bpy.context.scene.render.image_settings.file_format = 'PNG'

# --- Render ---
print("🎨 AI is rendering the 3D scene...")
bpy.ops.render.render(write_still=True)
print("✅ Done! Saved to /tmp/ai_generated_3d.png")

# Save blend file
bpy.ops.wm.save_as_mainfile(filepath='/tmp/ai_generated_scene.blend')
print("💾 Scene saved to /tmp/ai_generated_scene.blend")
