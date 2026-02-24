"""
Skeleton Retargeting Script for Blender
========================================
Blender Python (bpy) スクリプト。
Elden RingのHKXアニメーションをXPMSSEスケルトンにリターゲティングする。

Usage (Blenderコマンドライン):
    blender --background --python retarget_skeleton.py -- \
        --source-fbx input_animation.fbx \
        --target-skeleton xpmsse_skeleton.nif \
        --output output_retargeted.fbx \
        --bone-mapping bone_mapping.json

Usage (Blender GUI内):
    Blenderのスクリプトエディタで実行。事前にソース/ターゲットarmatureを読み込むこと。
"""

import json
import math
import os
import sys
from pathlib import Path

# Blender imports - will fail outside Blender
try:
    import bpy
    import mathutils
    from mathutils import Matrix, Vector, Quaternion, Euler
    IN_BLENDER = True
except ImportError:
    IN_BLENDER = False
    print("[INFO] Not running inside Blender. Configuration validation mode only.")


SCRIPT_DIR = Path(__file__).parent
DEFAULT_BONE_MAPPING = SCRIPT_DIR / "bone_mapping.json"


# --- Bone Mapping Loader ----------------------------------------------------

def load_bone_mapping(mapping_path: Path) -> dict:
    """
    Load bone mapping from JSON file.
    Returns a flat dict of source_bone -> target_bone.
    """
    with open(mapping_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    flat_mapping = {}

    # Merge all bone groups into a single flat mapping
    for group_key in ["core_bones", "finger_bones", "weapon_bones"]:
        group = data.get(group_key, {})
        for src, tgt in group.items():
            if not src.startswith("_"):  # Skip comments
                flat_mapping[src] = tgt

    return flat_mapping


def load_transform_adjustments(mapping_path: Path) -> dict:
    """Load axis/scale transform adjustments from bone mapping."""
    with open(mapping_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("transform_adjustments", {})


# --- Blender Retargeting Functions -------------------------------------------

if IN_BLENDER:

    def find_armatures() -> tuple:
        """Find source and target armatures in the scene."""
        armatures = [obj for obj in bpy.data.objects if obj.type == 'ARMATURE']

        if len(armatures) < 2:
            print(f"[ERROR] Need at least 2 armatures in scene, found {len(armatures)}")
            print("        Import the source animation and target skeleton first.")
            return None, None

        # Try to identify by naming convention
        source = None
        target = None

        for arm in armatures:
            name_lower = arm.name.lower()
            if any(tag in name_lower for tag in ["elden", "er_", "c0000", "source", "from"]):
                source = arm
            elif any(tag in name_lower for tag in ["skyrim", "xpmsse", "xp32", "target", "to"]):
                target = arm

        if source is None or target is None:
            print("[INFO] Could not auto-detect armatures. Using first two found.")
            print(f"       Armatures: {[a.name for a in armatures]}")
            source = armatures[0]
            target = armatures[1]

        print(f"[INFO] Source armature: {source.name} ({len(source.data.bones)} bones)")
        print(f"[INFO] Target armature: {target.name} ({len(target.data.bones)} bones)")

        return source, target


    def create_bone_constraints(source_arm, target_arm, bone_mapping: dict):
        """
        Create Copy Rotation/Location constraints on target bones
        driven by source bones, using the bone mapping.
        """
        bpy.context.view_layer.objects.active = target_arm
        bpy.ops.object.mode_set(mode='POSE')

        mapped_count = 0
        unmapped_source = []
        unmapped_target = []

        # Get all source bone names (case-insensitive lookup)
        source_bone_names = {b.name.lower(): b.name for b in source_arm.data.bones}

        for src_name, tgt_name in bone_mapping.items():
            # Find source bone (try exact match, then case-insensitive)
            actual_src = None
            if src_name in source_bone_names.values():
                actual_src = src_name
            elif src_name.lower() in source_bone_names:
                actual_src = source_bone_names[src_name.lower()]

            # Find target bone
            actual_tgt = None
            for bone in target_arm.data.bones:
                if bone.name == tgt_name or tgt_name in bone.name:
                    actual_tgt = bone.name
                    break

            if actual_src is None:
                unmapped_source.append(src_name)
                continue
            if actual_tgt is None:
                unmapped_target.append(tgt_name)
                continue

            # Create constraints on the target pose bone
            pose_bone = target_arm.pose.bones.get(actual_tgt)
            if pose_bone is None:
                continue

            # Copy Rotation constraint
            rot_constraint = pose_bone.constraints.new('COPY_ROTATION')
            rot_constraint.name = f"CCW_CopyRot_{actual_src}"
            rot_constraint.target = source_arm
            rot_constraint.subtarget = actual_src
            rot_constraint.target_space = 'LOCAL'
            rot_constraint.owner_space = 'LOCAL'
            rot_constraint.mix_mode = 'REPLACE'

            # Copy Location constraint (for root/pelvis bones)
            if any(keyword in src_name.lower() for keyword in ["master", "pelvis", "root"]):
                loc_constraint = pose_bone.constraints.new('COPY_LOCATION')
                loc_constraint.name = f"CCW_CopyLoc_{actual_src}"
                loc_constraint.target = source_arm
                loc_constraint.subtarget = actual_src
                loc_constraint.target_space = 'LOCAL'
                loc_constraint.owner_space = 'LOCAL'

            mapped_count += 1

        print(f"\n[RESULT] Bone Mapping Results:")
        print(f"  Successfully mapped: {mapped_count} bone pairs")
        if unmapped_source:
            print(f"  Source bones not found ({len(unmapped_source)}):")
            for b in unmapped_source[:10]:
                print(f"    - {b}")
            if len(unmapped_source) > 10:
                print(f"    ... and {len(unmapped_source) - 10} more")
        if unmapped_target:
            print(f"  Target bones not found ({len(unmapped_target)}):")
            for b in unmapped_target[:10]:
                print(f"    - {b}")

        bpy.ops.object.mode_set(mode='OBJECT')
        return mapped_count


    def apply_axis_correction(armature, transform_adj: dict):
        """
        Apply coordinate system correction.
        Elden Ring: Y-up → Skyrim: Z-up
        """
        axis_conv = transform_adj.get("axis_conversion", {})
        rot = axis_conv.get("rotation_correction_degrees", {})

        if rot:
            rx = math.radians(rot.get("x", 0))
            ry = math.radians(rot.get("y", 0))
            rz = math.radians(rot.get("z", 0))

            correction_matrix = Euler((rx, ry, rz), 'XYZ').to_matrix().to_4x4()
            armature.matrix_world = correction_matrix @ armature.matrix_world

            print(f"[INFO] Applied axis correction: X={rot.get('x', 0)}° Y={rot.get('y', 0)}° Z={rot.get('z', 0)}°")


    def bake_retargeted_animation(target_arm, frame_start: int, frame_end: int):
        """
        Bake the constrained animation into keyframes on the target armature.
        This 'burns in' the retargeted animation so constraints can be removed.
        """
        bpy.context.view_layer.objects.active = target_arm
        bpy.ops.object.mode_set(mode='POSE')

        # Select all pose bones
        bpy.ops.pose.select_all(action='SELECT')

        # Bake action
        bpy.ops.nla.bake(
            frame_start=frame_start,
            frame_end=frame_end,
            only_selected=True,
            visual_keying=True,
            clear_constraints=True,
            use_current_action=True,
            bake_types={'POSE'}
        )

        bpy.ops.object.mode_set(mode='OBJECT')
        print(f"[INFO] Baked animation: frames {frame_start} to {frame_end}")


    def export_retargeted(target_arm, output_path: str, format: str = "FBX"):
        """Export the retargeted animation."""
        # Select only the target armature
        bpy.ops.object.select_all(action='DESELECT')
        target_arm.select_set(True)
        bpy.context.view_layer.objects.active = target_arm

        if format.upper() == "FBX":
            bpy.ops.export_scene.fbx(
                filepath=output_path,
                use_selection=True,
                object_types={'ARMATURE'},
                add_leaf_bones=False,
                bake_anim=True,
                bake_anim_use_all_bones=True,
                bake_anim_force_startend_keying=True,
            )
        elif format.upper() == "GLTF":
            bpy.ops.export_scene.gltf(
                filepath=output_path,
                use_selection=True,
                export_animations=True,
            )

        print(f"[INFO] Exported retargeted animation to: {output_path}")


    def retarget_batch(source_dir: str, output_dir: str, target_skeleton_path: str,
                       bone_mapping_path: str):
        """
        Batch retarget all FBX animation files in a directory.
        """
        source_path = Path(source_dir)
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        bone_mapping = load_bone_mapping(Path(bone_mapping_path))
        transform_adj = load_transform_adjustments(Path(bone_mapping_path))

        fbx_files = sorted(source_path.glob("*.fbx"))
        print(f"[INFO] Found {len(fbx_files)} FBX files to retarget")

        for i, fbx_file in enumerate(fbx_files):
            print(f"\n{'=' * 40}")
            print(f"  [{i+1}/{len(fbx_files)}] {fbx_file.name}")
            print(f"{'=' * 40}")

            # Clear scene
            bpy.ops.wm.read_factory_settings(use_empty=True)

            # Import target skeleton
            if target_skeleton_path.endswith(".nif"):
                # NIF import requires specific addon
                print("[WARN] NIF import requires the Blender NIF Plugin")
                print("       Install from: https://github.com/niftools/blender_nif_plugin")
                # bpy.ops.import_scene.nif(filepath=target_skeleton_path)
                continue
            elif target_skeleton_path.endswith(".fbx"):
                bpy.ops.import_scene.fbx(filepath=target_skeleton_path)

            # Import source animation
            bpy.ops.import_scene.fbx(filepath=str(fbx_file))

            # Find armatures
            source_arm, target_arm = find_armatures()
            if source_arm is None or target_arm is None:
                print(f"[ERROR] Skipping {fbx_file.name}: could not find armatures")
                continue

            # Apply axis correction
            apply_axis_correction(source_arm, transform_adj)

            # Create bone constraints
            mapped = create_bone_constraints(source_arm, target_arm, bone_mapping)
            if mapped == 0:
                print(f"[WARN] No bones mapped for {fbx_file.name}")
                continue

            # Determine frame range
            frame_start = int(bpy.context.scene.frame_start)
            frame_end = int(bpy.context.scene.frame_end)

            # Bake
            bake_retargeted_animation(target_arm, frame_start, frame_end)

            # Export
            out_file = output_path / fbx_file.name
            export_retargeted(target_arm, str(out_file))

        print(f"\n[DONE] Batch retargeting complete. Output: {output_path}")


# --- CLI Mode (outside Blender) -----------------------------------------------

def validate_mapping(mapping_path: Path):
    """Validate bone mapping file without Blender."""
    mapping = load_bone_mapping(mapping_path)
    transform = load_transform_adjustments(mapping_path)

    print(f"Bone Mapping Validation: {mapping_path}")
    print(f"  Total bone pairs: {len(mapping)}")
    print(f"  Axis conversion: {transform.get('axis_conversion', {}).get('from', 'N/A')} → "
          f"{transform.get('axis_conversion', {}).get('to', 'N/A')}")
    print(f"\n  Core bone mappings:")
    for src, tgt in list(mapping.items())[:10]:
        print(f"    {src:<20s} → {tgt}")
    if len(mapping) > 10:
        print(f"    ... and {len(mapping) - 10} more")


# --- Main Entry Point --------------------------------------------------------

def main():
    if IN_BLENDER:
        # Parse arguments after "--"
        argv = sys.argv
        if "--" in argv:
            argv = argv[argv.index("--") + 1:]
        else:
            # Running in Blender GUI - use interactive mode
            print("[INFO] Running in Blender GUI mode.")
            print("       Load source and target armatures, then run this script.")

            bone_mapping = load_bone_mapping(DEFAULT_BONE_MAPPING)
            transform_adj = load_transform_adjustments(DEFAULT_BONE_MAPPING)

            source_arm, target_arm = find_armatures()
            if source_arm and target_arm:
                apply_axis_correction(source_arm, transform_adj)
                create_bone_constraints(source_arm, target_arm, bone_mapping)
                print("\n[INFO] Constraints created. Review the animation, then:")
                print("       1. Set the correct frame range")
                print("       2. Run bake_retargeted_animation(target_arm, start, end)")
                print("       3. Run export_retargeted(target_arm, 'output.fbx')")
            return

        import argparse
        parser = argparse.ArgumentParser(description="Retarget ER animations to XPMSSE")
        parser.add_argument("--source-fbx", required=True, help="Source animation FBX")
        parser.add_argument("--target-skeleton", required=True, help="Target XPMSSE skeleton")
        parser.add_argument("--output", required=True, help="Output retargeted FBX")
        parser.add_argument("--bone-mapping", default=str(DEFAULT_BONE_MAPPING))
        parser.add_argument("--batch-dir", default=None, help="Batch process directory")
        args = parser.parse_args(argv)

        if args.batch_dir:
            retarget_batch(args.batch_dir, args.output, args.target_skeleton, args.bone_mapping)
        else:
            bone_mapping = load_bone_mapping(Path(args.bone_mapping))
            transform_adj = load_transform_adjustments(Path(args.bone_mapping))

            bpy.ops.wm.read_factory_settings(use_empty=True)
            bpy.ops.import_scene.fbx(filepath=args.target_skeleton)
            bpy.ops.import_scene.fbx(filepath=args.source_fbx)

            source_arm, target_arm = find_armatures()
            if source_arm and target_arm:
                apply_axis_correction(source_arm, transform_adj)
                mapped = create_bone_constraints(source_arm, target_arm, bone_mapping)
                if mapped > 0:
                    frame_start = bpy.context.scene.frame_start
                    frame_end = bpy.context.scene.frame_end
                    bake_retargeted_animation(target_arm, frame_start, frame_end)
                    export_retargeted(target_arm, args.output)
    else:
        # Outside Blender - just validate
        validate_mapping(DEFAULT_BONE_MAPPING)


if __name__ == "__main__":
    main()
