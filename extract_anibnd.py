"""
CCW Animation Extraction Pipeline
==================================
WitchyBNDを使用してCarian Combo Warriorsの.anibnd.dcxファイルから
HKXアニメーションデータとTAEイベントデータを抽出する。

Usage:
    python extract_anibnd.py                 # 全ファイルを抽出
    python extract_anibnd.py --dry-run       # 抽出せずにプレビュー
    python extract_anibnd.py --file c0000    # 特定ファイルのみ
    python extract_anibnd.py --list          # 対象ファイル一覧
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional


# --- Configuration -----------------------------------------------------------

SCRIPT_DIR = Path(__file__).parent
CONFIG_PATH = SCRIPT_DIR / "config.json"

# CCW animation bundle files (c0000 = player character)
ANIBND_FILES = [
    "c0000.anibnd.dcx",           # Base player animations
    "c0000_a00_hi.anibnd.dcx",    # High-priority action set 00
    "c0000_a1x.anibnd.dcx",       # Action set 1x (light attacks etc.)
    "c0000_a2x.anibnd.dcx",       # Action set 2x (heavy attacks etc.)
    "c0000_a3x.anibnd.dcx",       # Action set 3x (weapon arts etc.)
    "c0000_a4x.anibnd.dcx",       # Action set 4x (special moves)
    "c0000_a6x.anibnd.dcx",       # Action set 6x
    "c0000_a9x.anibnd.dcx",       # Action set 9x (largest set)
]

# Additional files of interest
BEHAVIOR_FILES = [
    "c0000.behbnd.dcx",           # Behavior state machine
    "c0000.chrbnd.dcx",           # Character binding (skeleton ref)
]


def load_config() -> dict:
    """Load configuration from config.json."""
    if not CONFIG_PATH.exists():
        print(f"[ERROR] Configuration file not found: {CONFIG_PATH}")
        print("        Please create config.json with your local paths.")
        print("        See config.json.example for reference.")
        sys.exit(1)

    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        config = json.load(f)

    return config


def validate_config(config: dict) -> bool:
    """Validate that all required paths exist."""
    valid = True

    witchy_path = Path(config.get("witchybnd_path", ""))
    if not witchy_path.exists():
        print(f"[WARN] WitchyBND not found at: {witchy_path}")
        print("       Download from: https://github.com/ividyon/WitchyBND/releases")
        valid = False

    ccw_root = Path(config.get("ccw_root", ""))
    if not ccw_root.exists():
        print(f"[ERROR] CCW root directory not found: {ccw_root}")
        valid = False

    return valid


def find_anibnd_files(ccw_root: Path, filter_name: Optional[str] = None) -> list[Path]:
    """Find all .anibnd.dcx files in the CCW chr directory."""
    chr_dir = ccw_root / "chr"
    if not chr_dir.exists():
        print(f"[ERROR] chr directory not found: {chr_dir}")
        return []

    files = []
    for name in ANIBND_FILES:
        filepath = chr_dir / name
        if filepath.exists():
            if filter_name is None or filter_name in name:
                files.append(filepath)
        else:
            print(f"[WARN] Expected file not found: {filepath}")

    return files


def find_behavior_files(ccw_root: Path) -> list[Path]:
    """Find behavior and character binding files."""
    chr_dir = ccw_root / "chr"
    files = []
    for name in BEHAVIOR_FILES:
        filepath = chr_dir / name
        if filepath.exists():
            files.append(filepath)
    return files


def extract_with_witchybnd(witchy_path: Path, target_file: Path, output_dir: Path,
                           dry_run: bool = False) -> bool:
    """
    Extract a .anibnd.dcx file using WitchyBND.

    WitchyBND can be invoked by passing the file as a command-line argument.
    It will create an unpacked folder next to the input file by default.
    We then move the results to our organized output directory.
    """
    print(f"\n{'=' * 60}")
    print(f"  Extracting: {target_file.name}")
    print(f"  Size: {target_file.stat().st_size / 1024 / 1024:.2f} MB")
    print(f"{'=' * 60}")

    if dry_run:
        print(f"  [DRY RUN] Would extract with WitchyBND")
        print(f"  [DRY RUN] Output would go to: {output_dir}")
        return True

    # WitchyBND creates an unpacked folder next to the source file
    # The folder name is typically: <filename>-witchybnd or <filename> (without extension)
    expected_output_patterns = [
        target_file.parent / target_file.stem,                           # c0000.anibnd
        target_file.parent / target_file.name.replace(".dcx", ""),       # c0000.anibnd
        target_file.parent / (target_file.stem + "-witchybnd"),          # c0000.anibnd-witchybnd
    ]

    # Remove .dcx first, then remove remaining extensions for the base name
    base_name = target_file.name
    for suffix in [".dcx", ".anibnd", ".behbnd", ".chrbnd"]:
        base_name = base_name.replace(suffix, "")
    if not base_name:
        base_name = target_file.stem.split(".")[0]

    try:
        # Run WitchyBND with the target file
        print(f"  Running: {witchy_path} \"{target_file}\"")
        result = subprocess.run(
            [str(witchy_path), str(target_file)],
            capture_output=True,
            text=True,
            timeout=300,  # 5 minute timeout per file
            cwd=str(target_file.parent)
        )

        if result.returncode != 0:
            print(f"  [ERROR] WitchyBND returned code {result.returncode}")
            if result.stderr:
                print(f"  STDERR: {result.stderr[:500]}")
            if result.stdout:
                print(f"  STDOUT: {result.stdout[:500]}")
            return False

        if result.stdout:
            print(f"  Output: {result.stdout[:200]}")

    except FileNotFoundError:
        print(f"  [ERROR] WitchyBND executable not found: {witchy_path}")
        return False
    except subprocess.TimeoutExpired:
        print(f"  [ERROR] WitchyBND timed out after 300 seconds")
        return False
    except Exception as e:
        print(f"  [ERROR] Unexpected error: {e}")
        return False

    # Find the unpacked output directory
    unpacked_dir = None
    for pattern in expected_output_patterns:
        if pattern.exists() and pattern.is_dir():
            unpacked_dir = pattern
            break

    # Also check for any new directories created in the parent
    if unpacked_dir is None:
        chr_dir = target_file.parent
        for item in chr_dir.iterdir():
            if item.is_dir() and base_name in item.name:
                unpacked_dir = item
                break

    if unpacked_dir is None:
        print(f"  [WARN] Could not find unpacked output directory")
        print(f"  Checked patterns: {[str(p) for p in expected_output_patterns]}")
        print(f"  WitchyBND may have created output in a different location.")
        print(f"  Please check {target_file.parent} manually.")
        return False

    # Organize extracted files
    print(f"  Found unpacked directory: {unpacked_dir}")
    organize_extracted_files(unpacked_dir, output_dir, base_name)

    return True


def organize_extracted_files(source_dir: Path, output_root: Path, prefix: str):
    """
    Organize extracted files into categorized directories.

    HKX files → extracted/hkx/<prefix>/
    TAE files → extracted/tae/<prefix>/
    Other files → extracted/other/<prefix>/
    """
    hkx_dir = output_root / "extracted" / "hkx" / prefix
    tae_dir = output_root / "extracted" / "tae" / prefix
    other_dir = output_root / "extracted" / "other" / prefix

    hkx_count = 0
    tae_count = 0
    other_count = 0

    for root, dirs, files in os.walk(source_dir):
        for filename in files:
            filepath = Path(root) / filename
            ext = filepath.suffix.lower()

            if ext == ".hkx":
                dest_dir = hkx_dir
                hkx_count += 1
            elif ext == ".tae":
                dest_dir = tae_dir
                tae_count += 1
            else:
                dest_dir = other_dir
                other_count += 1

            dest_dir.mkdir(parents=True, exist_ok=True)

            # Preserve subdirectory structure within the unpacked folder
            rel_path = filepath.relative_to(source_dir)
            dest_path = dest_dir / rel_path

            dest_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(filepath, dest_path)

    print(f"  Organized: {hkx_count} HKX, {tae_count} TAE, {other_count} other files")


def print_file_list(ccw_root: Path):
    """Print a list of all target files and their sizes."""
    print("\n=== CCW Animation Bundle Files ===\n")

    anibnd_files = find_anibnd_files(ccw_root)
    behavior_files = find_behavior_files(ccw_root)

    total_size = 0

    print("Animation Bundles (.anibnd.dcx):")
    for f in anibnd_files:
        size_mb = f.stat().st_size / 1024 / 1024
        total_size += size_mb
        print(f"  {f.name:<35s} {size_mb:>8.2f} MB")

    print("\nBehavior/Character Files:")
    for f in behavior_files:
        size_mb = f.stat().st_size / 1024 / 1024
        total_size += size_mb
        print(f"  {f.name:<35s} {size_mb:>8.2f} MB")

    print(f"\n{'Total:':<35s} {total_size:>8.2f} MB")
    print(f"{'File count:':<35s} {len(anibnd_files) + len(behavior_files):>8d}")


def main():
    parser = argparse.ArgumentParser(
        description="Extract CCW animation data using WitchyBND"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Preview extraction without actually running WitchyBND"
    )
    parser.add_argument(
        "--file", type=str, default=None,
        help="Extract only files matching this name (e.g., 'c0000_a2x')"
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List all target files and exit"
    )
    parser.add_argument(
        "--include-behavior", action="store_true",
        help="Also extract behavior (.behbnd.dcx) and character (.chrbnd.dcx) files"
    )
    parser.add_argument(
        "--config", type=str, default=None,
        help="Path to config.json (default: same directory as this script)"
    )
    args = parser.parse_args()

    # Load config
    if args.config:
        global CONFIG_PATH
        CONFIG_PATH = Path(args.config)

    config = load_config()

    ccw_root = Path(config["ccw_root"])
    output_root = Path(config["output_root"])
    witchy_path = Path(config["witchybnd_path"])

    # List mode
    if args.list:
        print_file_list(ccw_root)
        return

    # Validate
    print("=" * 60)
    print("  CCW Animation Extraction Pipeline")
    print("  Using WitchyBND for FromSoftware archive extraction")
    print("=" * 60)
    print()

    if not validate_config(config):
        if not args.dry_run:
            print("\n[ERROR] Configuration validation failed. Fix errors and retry.")
            print("        Use --dry-run to preview without WitchyBND.")
            sys.exit(1)
        else:
            print("\n[INFO] Running in dry-run mode despite configuration issues.")

    # Find target files
    anibnd_files = find_anibnd_files(ccw_root, args.file)
    if not anibnd_files:
        print("[ERROR] No animation bundle files found!")
        sys.exit(1)

    print(f"\nTarget files: {len(anibnd_files)} animation bundles")
    for f in anibnd_files:
        print(f"  - {f.name} ({f.stat().st_size / 1024 / 1024:.2f} MB)")

    # Create output directories
    (output_root / "extracted" / "hkx").mkdir(parents=True, exist_ok=True)
    (output_root / "extracted" / "tae").mkdir(parents=True, exist_ok=True)
    (output_root / "extracted" / "other").mkdir(parents=True, exist_ok=True)

    # Extract each file
    success_count = 0
    fail_count = 0

    for anibnd_file in anibnd_files:
        if extract_with_witchybnd(witchy_path, anibnd_file, output_root, args.dry_run):
            success_count += 1
        else:
            fail_count += 1

    # Optionally extract behavior files
    if args.include_behavior:
        behavior_files = find_behavior_files(ccw_root)
        for beh_file in behavior_files:
            if extract_with_witchybnd(witchy_path, beh_file, output_root, args.dry_run):
                success_count += 1
            else:
                fail_count += 1

    # Summary
    print(f"\n{'=' * 60}")
    print(f"  Extraction Complete")
    print(f"  Success: {success_count} | Failed: {fail_count}")
    if not args.dry_run:
        print(f"  Output: {output_root / 'extracted'}")
    print(f"{'=' * 60}")


if __name__ == "__main__":
    main()
