"""
HKX Conversion Pipeline
========================
リターゲティング済みアニメーションFBXをSkyrim SE互換のHKX形式に変換する。
hkxcmd または HavokBehaviorPostProcess.exe を使用。

Usage:
    python convert_hkx.py converted_fbx/          # ディレクトリ内の全FBXを変換
    python convert_hkx.py animation.fbx            # 単一ファイル
    python convert_hkx.py --verify output/         # 変換後のHKXを検証

変換フロー:
    1. FBX → hkxcmd / Havok Content Tools → 32bit HKX (Oldrim format)
    2. 32bit HKX → HavokBehaviorPostProcess.exe → 64bit HKX (Skyrim SE format)
    OR
    1. FBX → Havok Content Tools 2014 filter pipeline → 64bit HKX directly

Note: この変換にはHavok Content Tools 2014またはhkxcmdが必要です。
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional


SCRIPT_DIR = Path(__file__).parent
CONFIG_PATH = SCRIPT_DIR / "config.json"


def load_config() -> dict:
    """Load pipeline configuration."""
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


# --- Conversion Methods -------------------------------------------------------

class HKXConverter:
    """Manages conversion of animation files to Skyrim SE HKX format."""

    def __init__(self, config: dict):
        self.config = config
        self.hkxcmd_path = Path(config.get("hkxcmd_path", ""))
        self.havok_postprocess_path = Path(config.get("havok_postprocess_path", ""))
        self.output_root = Path(config.get("output_root", "")) / "converted"

        # Detect available conversion tools
        self.has_hkxcmd = self.hkxcmd_path.exists()
        self.has_postprocess = self.havok_postprocess_path.exists()

        self._print_tool_status()

    def _print_tool_status(self):
        """Print status of available conversion tools."""
        print("\n  Conversion Tool Status:")
        print(f"    hkxcmd:              {'✓ Found' if self.has_hkxcmd else '✗ Not found'}")
        print(f"      Path: {self.hkxcmd_path}")
        print(f"    HavokBehaviorPostProcess: {'✓ Found' if self.has_postprocess else '✗ Not found'}")
        print(f"      Path: {self.havok_postprocess_path}")

        if not self.has_hkxcmd and not self.has_postprocess:
            print("\n  [WARN] No conversion tools found!")
            print("         hkxcmd: Download from Nexus Mods or GitHub")
            print("         HavokBehaviorPostProcess: Install Skyrim SE Creation Kit")

    def convert_fbx_to_hkx_via_hkxcmd(self, fbx_path: Path, output_path: Path) -> bool:
        """
        Convert FBX to HKX using hkxcmd.
        hkxcmd can convert between FBX/KF/HKX formats.
        
        Command: hkxcmd convert <input.fbx> <output.hkx>
        """
        if not self.has_hkxcmd:
            print("[ERROR] hkxcmd not available")
            return False

        try:
            # First convert FBX to KF (Gamebryo Keyframe)
            kf_path = output_path.with_suffix(".kf")

            result = subprocess.run(
                [str(self.hkxcmd_path), "convert",
                 str(fbx_path), str(kf_path)],
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode != 0:
                print(f"[ERROR] hkxcmd FBX→KF failed: {result.stderr[:300]}")
                return False

            # Then convert KF to HKX
            result = subprocess.run(
                [str(self.hkxcmd_path), "convert",
                 str(kf_path), str(output_path)],
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode != 0:
                print(f"[ERROR] hkxcmd KF→HKX failed: {result.stderr[:300]}")
                return False

            # Clean up intermediate KF
            if kf_path.exists():
                kf_path.unlink()

            print(f"  ✓ Converted via hkxcmd: {output_path.name}")
            return True

        except subprocess.TimeoutExpired:
            print(f"[ERROR] hkxcmd timed out")
            return False
        except Exception as e:
            print(f"[ERROR] hkxcmd error: {e}")
            return False

    def convert_32bit_to_64bit(self, hkx_32bit_path: Path, output_path: Path) -> bool:
        """
        Convert 32-bit HKX (Oldrim/LE format) to 64-bit (Skyrim SE format)
        using HavokBehaviorPostProcess.exe from the Creation Kit.
        
        Command: HavokBehaviorPostProcess.exe --platformamd64 <input.hkx> <output.hkx>
        """
        if not self.has_postprocess:
            print("[ERROR] HavokBehaviorPostProcess not available")
            return False

        try:
            result = subprocess.run(
                [str(self.havok_postprocess_path),
                 "--platformamd64",
                 str(hkx_32bit_path),
                 str(output_path)],
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode != 0:
                print(f"[ERROR] HavokBehaviorPostProcess failed: {result.stderr[:300]}")
                return False

            print(f"  ✓ Converted to 64-bit: {output_path.name}")
            return True

        except subprocess.TimeoutExpired:
            print("[ERROR] HavokBehaviorPostProcess timed out")
            return False
        except Exception as e:
            print(f"[ERROR] HavokBehaviorPostProcess error: {e}")
            return False

    def convert_hkx_xml_roundtrip(self, hkx_path: Path, output_path: Path) -> bool:
        """
        Convert HKX via XML intermediate format using hkxcmd.
        This allows modifying the Havok version tag.
        
        Flow: HKX → XML → modify version → XML → HKX
        """
        if not self.has_hkxcmd:
            print("[ERROR] hkxcmd not available for XML roundtrip")
            return False

        xml_path = output_path.with_suffix(".xml")

        try:
            # HKX → XML
            result = subprocess.run(
                [str(self.hkxcmd_path), "exportxml",
                 str(hkx_path), str(xml_path)],
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode != 0:
                print(f"[ERROR] hkxcmd exportxml failed: {result.stderr[:300]}")
                return False

            # Modify XML to update Havok SDK version if needed
            if xml_path.exists():
                self._patch_hkx_xml_version(xml_path)

            # XML → HKX
            result = subprocess.run(
                [str(self.hkxcmd_path), "importxml",
                 str(xml_path), str(output_path)],
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode != 0:
                print(f"[ERROR] hkxcmd importxml failed: {result.stderr[:300]}")
                return False

            # Clean up XML
            if xml_path.exists():
                xml_path.unlink()

            print(f"  ✓ Converted via XML roundtrip: {output_path.name}")
            return True

        except Exception as e:
            print(f"[ERROR] XML roundtrip error: {e}")
            return False

    def _patch_hkx_xml_version(self, xml_path: Path):
        """
        Patch the Havok SDK version in XML to match Skyrim SE expectations.
        Changes hk_2018.x.x to hk_2014.x.x style version strings.
        """
        try:
            content = xml_path.read_text(encoding="utf-8")

            # Update SDK version references
            # Elden Ring: hk_2018.1.0-r1
            # Skyrim SE:  hk_2014.1.0-r1
            replacements = {
                'sdkversion="hk_2018': 'sdkversion="hk_2014',
                'classversion="12"': 'classversion="11"',  # Adjust class versions
            }

            modified = False
            for old, new in replacements.items():
                if old in content:
                    content = content.replace(old, new)
                    modified = True

            if modified:
                xml_path.write_text(content, encoding="utf-8")
                print(f"  [PATCH] Updated Havok version tags in {xml_path.name}")

        except Exception as e:
            print(f"  [WARN] Could not patch XML: {e}")

    def convert_file(self, input_path: Path, output_dir: Path,
                     method: str = "auto") -> bool:
        """
        Convert a single file to Skyrim SE HKX format.
        
        Methods:
            'hkxcmd'  - Use hkxcmd for conversion
            'xml'     - Use XML roundtrip via hkxcmd
            'postprocess' - Use HavokBehaviorPostProcess (32→64 bit)
            'auto'    - Try best available method
        """
        output_dir.mkdir(parents=True, exist_ok=True)
        output_path = output_dir / (input_path.stem + ".hkx")

        if method == "auto":
            # Determine best conversion path
            suffix = input_path.suffix.lower()
            if suffix == ".fbx":
                # FBX → HKX via hkxcmd
                if self.has_hkxcmd:
                    return self.convert_fbx_to_hkx_via_hkxcmd(input_path, output_path)
                else:
                    print("[ERROR] FBX conversion requires hkxcmd")
                    return False
            elif suffix == ".hkx":
                # HKX → HKX (version/format conversion)
                temp_path = output_dir / (input_path.stem + "_temp.hkx")
                if self.has_hkxcmd:
                    success = self.convert_hkx_xml_roundtrip(input_path, temp_path)
                    if success and self.has_postprocess:
                        success = self.convert_32bit_to_64bit(temp_path, output_path)
                        if temp_path.exists():
                            temp_path.unlink()
                        return success
                    elif success:
                        shutil.move(str(temp_path), str(output_path))
                        return True
                    return False
                elif self.has_postprocess:
                    return self.convert_32bit_to_64bit(input_path, output_path)
                else:
                    print("[ERROR] No conversion tools available")
                    return False
        elif method == "hkxcmd":
            return self.convert_fbx_to_hkx_via_hkxcmd(input_path, output_path)
        elif method == "xml":
            return self.convert_hkx_xml_roundtrip(input_path, output_path)
        elif method == "postprocess":
            return self.convert_32bit_to_64bit(input_path, output_path)
        else:
            print(f"[ERROR] Unknown conversion method: {method}")
            return False

    def convert_batch(self, input_dir: Path, output_dir: Path,
                      extension: str = ".fbx", method: str = "auto") -> tuple[int, int]:
        """
        Batch convert all files with the given extension.
        Returns (success_count, fail_count).
        """
        files = sorted(input_dir.rglob(f"*{extension}"))
        if not files:
            print(f"[WARN] No {extension} files found in {input_dir}")
            return 0, 0

        print(f"\n  Batch conversion: {len(files)} files")
        success = 0
        fail = 0

        for i, filepath in enumerate(files):
            print(f"\n  [{i+1}/{len(files)}] {filepath.name}")
            if self.convert_file(filepath, output_dir, method):
                success += 1
            else:
                fail += 1

        return success, fail


# --- Verification -------------------------------------------------------------

def verify_converted_hkx(hkx_dir: Path):
    """Verify converted HKX files are valid for Skyrim SE."""
    # Reuse the analyzer
    sys.path.insert(0, str(SCRIPT_DIR))
    from analyze_hkx import HKXInfo, check_skyrim_compatibility

    hkx_files = sorted(hkx_dir.rglob("*.hkx"))
    if not hkx_files:
        print(f"[WARN] No HKX files found in {hkx_dir}")
        return

    print(f"\n{'=' * 60}")
    print(f"  Verification: {len(hkx_files)} converted HKX files")
    print(f"{'=' * 60}")

    ok_count = 0
    issue_count = 0

    for hkx_file in hkx_files:
        info = HKXInfo(hkx_file)
        issues = check_skyrim_compatibility(info)

        has_problems = any(not i.startswith("OK") for i in issues)

        if has_problems:
            issue_count += 1
            print(f"\n  ✗ {hkx_file.name}")
            for issue in issues:
                if not issue.startswith("OK"):
                    print(f"    ⚠ {issue}")
        else:
            ok_count += 1

    print(f"\n  Results: {ok_count} OK, {issue_count} with issues")


# --- Main ---------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Convert retargeted animations to Skyrim SE HKX format"
    )
    parser.add_argument(
        "input", type=str, nargs="?", default=None,
        help="Input file or directory"
    )
    parser.add_argument(
        "--output", "-o", type=str, default=None,
        help="Output directory (default: CCW_AnimFramework/converted/)"
    )
    parser.add_argument(
        "--method", choices=["auto", "hkxcmd", "xml", "postprocess"],
        default="auto",
        help="Conversion method to use"
    )
    parser.add_argument(
        "--extension", type=str, default=".fbx",
        help="File extension filter for batch mode (default: .fbx)"
    )
    parser.add_argument(
        "--verify", action="store_true",
        help="Verify converted HKX files instead of converting"
    )
    parser.add_argument(
        "--status", action="store_true",
        help="Show conversion tool availability status and exit"
    )
    args = parser.parse_args()

    config = load_config()
    converter = HKXConverter(config)

    if args.status:
        return

    if args.verify:
        target = Path(args.input) if args.input else Path(config["output_root"]) / "converted"
        verify_converted_hkx(target)
        return

    if args.input is None:
        parser.print_help()
        return

    input_path = Path(args.input)
    output_dir = Path(args.output) if args.output else converter.output_root

    print(f"\n{'=' * 60}")
    print(f"  HKX Conversion Pipeline")
    print(f"  Method: {args.method}")
    print(f"{'=' * 60}")

    if input_path.is_file():
        success = converter.convert_file(input_path, output_dir, args.method)
        print(f"\n  Result: {'✓ Success' if success else '✗ Failed'}")
    elif input_path.is_dir():
        success, fail = converter.convert_batch(input_path, output_dir,
                                                 args.extension, args.method)
        print(f"\n  Batch Result: {success} success, {fail} failed")
    else:
        print(f"[ERROR] Input not found: {input_path}")


if __name__ == "__main__":
    main()
