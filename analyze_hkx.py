"""
HKX File Analyzer
==================
抽出されたHKXファイルのヘッダーを解析し、メタデータを出力する。
Havokバージョン、ボーン数、アニメーション長などの情報を表示。

Usage:
    python analyze_hkx.py extracted/hkx/           # ディレクトリ内の全HKXを分析
    python analyze_hkx.py extracted/hkx/c0000/a000_000000.hkx  # 単一ファイル
    python analyze_hkx.py extracted/hkx/ --summary  # サマリーのみ
    python analyze_hkx.py extracted/hkx/ --export report.json  # JSON出力
"""

import argparse
import json
import os
import struct
import sys
from pathlib import Path
from typing import Optional


# --- HKX Header Parsing -----------------------------------------------------

# Havok HKX magic bytes
HKX_MAGIC_LE = b"\x57\xE0\xE0\x57\x10\xC0\xC0\x10"  # Little-endian
HKX_MAGIC_BE = b"\x57\xE0\xE0\x57\x10\xC0\xC0\x10"  # Big-endian (same magic)

# Known Havok SDK versions used by games
HAVOK_VERSIONS = {
    # Elden Ring uses Havok 2018 (hk2018)
    0x0B000000: "hk2014 (Skyrim SE)",
    0x0D000000: "hk2016",
    0x0E000000: "hk2018 (Elden Ring / DS3)",
    0x10000000: "hk2020",
}

# Elden Ring specific Havok signature patterns
ER_CLASSNAME_SIGNATURES = [
    b"hkaAnimationContainer",
    b"hkaSplineCompressedAnimation",
    b"hkaInterleavedUncompressedAnimation",
    b"hkaAnimationBinding",
    b"hkaSkeleton",
    b"hkaSkeletonMapper",
    b"hkRootLevelContainer",
]


class HKXInfo:
    """Parsed information from an HKX file."""

    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.filename = filepath.name
        self.filesize = filepath.stat().st_size
        self.valid = False
        self.error: Optional[str] = None

        # Header fields
        self.endianness = "unknown"
        self.havok_version_raw = 0
        self.havok_version_str = "unknown"
        self.user_tag = 0
        self.num_sections = 0
        self.content_version_string = ""

        # Detected content classes
        self.detected_classes: list[str] = []
        self.has_animation = False
        self.has_skeleton = False
        self.has_binding = False
        self.compression_type = "unknown"

        # Parse
        self._parse()

    def _parse(self):
        """Parse the HKX file header and scan for class signatures."""
        try:
            with open(self.filepath, "rb") as f:
                data = f.read(min(self.filesize, 65536))  # Read up to 64KB for analysis
        except Exception as e:
            self.error = f"Cannot read file: {e}"
            return

        if len(data) < 64:
            self.error = "File too small to be a valid HKX"
            return

        # Check magic bytes
        magic = data[:8]
        if magic != HKX_MAGIC_LE:
            # Try checking if it's already decompressed or a different variant
            # Some HKX files from FromSoftware games have a different header structure
            self._parse_alternative_header(data)
            return

        self.valid = True

        # Parse standard HKX header
        # Offset 0x08: User tag
        # Offset 0x0C: Version (format version, not SDK version)
        # Offset 0x10: Pointer size, endianness, padding option, base class
        # Offset 0x14: Num sections
        # Offset 0x18: Content section index
        # Offset 0x1C: Content section offset
        # Offset 0x20: Content class name section index
        # Offset 0x24: Content class name section offset
        # Offset 0x28: Content version string offset
        # Offsets vary depending on exact Havok versioning

        try:
            self.user_tag = struct.unpack_from("<I", data, 0x08)[0]

            # Detect endianness from a byte at offset 0x11
            endian_byte = data[0x11] if len(data) > 0x11 else 0
            self.endianness = "big" if endian_byte == 0 else "little"

            # Havok version info - different offsets depending on format
            version_candidate = struct.unpack_from("<I", data, 0x0C)[0]
            if version_candidate in HAVOK_VERSIONS:
                self.havok_version_raw = version_candidate
                self.havok_version_str = HAVOK_VERSIONS[version_candidate]
            else:
                # Try other common offsets
                for offset in [0x10, 0x14, 0x28, 0x2C]:
                    if offset + 4 <= len(data):
                        v = struct.unpack_from("<I", data, offset)[0]
                        if v in HAVOK_VERSIONS:
                            self.havok_version_raw = v
                            self.havok_version_str = HAVOK_VERSIONS[v]
                            break

                if self.havok_version_raw == 0:
                    self.havok_version_str = f"unknown (0x{version_candidate:08X})"

            # Section count
            for offset in [0x14, 0x18]:
                if offset + 4 <= len(data):
                    v = struct.unpack_from("<I", data, offset)[0]
                    if 0 < v < 100:  # Reasonable section count
                        self.num_sections = v
                        break

        except struct.error:
            pass  # Non-critical; continue with class scanning

        # Scan for content version string (ASCII string near header)
        self._scan_version_string(data)

        # Scan for class name signatures
        self._scan_classes(data)

    def _parse_alternative_header(self, data: bytes):
        """
        Parse alternative HKX formats.
        Some FromSoftware HKX files (especially from newer games) use
        a tagfile or packfile format with different magic/structure.
        """
        # Check for Havok tagfile format (used in newer games)
        # Tagfile starts with version string like "hk_2018.1.0-r1"
        if data[:3] == b"hk_" or data[:3] == b"HK_":
            self.valid = True
            # Extract version string
            end = data.find(b"\x00", 0, 64)
            if end > 0:
                self.content_version_string = data[:end].decode("ascii", errors="replace")
                if "2018" in self.content_version_string:
                    self.havok_version_str = "hk2018 (Elden Ring / DS3)"
                elif "2014" in self.content_version_string:
                    self.havok_version_str = "hk2014 (Skyrim SE)"
                elif "2016" in self.content_version_string:
                    self.havok_version_str = "hk2016"
            self._scan_classes(data)
            return

        # Check for packfile format signature at various offsets
        for offset in range(0, min(256, len(data) - 8), 4):
            if data[offset:offset + 8] == HKX_MAGIC_LE:
                self.valid = True
                # Found magic at alternative offset; re-parse from there
                # (This handles cases where there's a container header)
                break

        if not self.valid:
            # Still try to scan for Havok class names - file might be
            # a raw animation without standard header
            self._scan_classes(data)
            if self.detected_classes:
                self.valid = True
                self.havok_version_str = "unknown (headerless, classes detected)"

        if not self.valid:
            self.error = "No valid HKX header or Havok class signatures found"

    def _scan_version_string(self, data: bytes):
        """Scan for Havok version string in file data."""
        for pattern in [b"hk_", b"Havok-", b"hkVersionUtil"]:
            idx = data.find(pattern)
            if idx >= 0:
                end = data.find(b"\x00", idx, idx + 64)
                if end > idx:
                    self.content_version_string = data[idx:end].decode("ascii", errors="replace")
                    break

    def _scan_classes(self, data: bytes):
        """Scan the file for known Havok class name signatures."""
        for sig in ER_CLASSNAME_SIGNATURES:
            if sig in data:
                class_name = sig.decode("ascii")
                self.detected_classes.append(class_name)

                if "Animation" in class_name and "Container" not in class_name:
                    self.has_animation = True
                    if "Spline" in class_name:
                        self.compression_type = "spline"
                    elif "Interleaved" in class_name:
                        self.compression_type = "interleaved"
                elif "Skeleton" in class_name and "Mapper" not in class_name:
                    self.has_skeleton = True
                elif "Binding" in class_name:
                    self.has_binding = True

    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "filename": self.filename,
            "filepath": str(self.filepath),
            "filesize_bytes": self.filesize,
            "filesize_kb": round(self.filesize / 1024, 2),
            "valid": self.valid,
            "error": self.error,
            "endianness": self.endianness,
            "havok_version": self.havok_version_str,
            "havok_version_raw": hex(self.havok_version_raw) if self.havok_version_raw else None,
            "content_version": self.content_version_string or None,
            "num_sections": self.num_sections,
            "detected_classes": self.detected_classes,
            "has_animation": self.has_animation,
            "has_skeleton": self.has_skeleton,
            "has_binding": self.has_binding,
            "compression_type": self.compression_type,
        }

    def __str__(self) -> str:
        lines = [
            f"File: {self.filename}",
            f"  Size: {self.filesize / 1024:.1f} KB",
            f"  Valid: {self.valid}",
        ]
        if self.error:
            lines.append(f"  Error: {self.error}")
        if self.valid:
            lines.append(f"  Havok Version: {self.havok_version_str}")
            if self.content_version_string:
                lines.append(f"  Content Version: {self.content_version_string}")
            lines.append(f"  Endianness: {self.endianness}")
            if self.num_sections:
                lines.append(f"  Sections: {self.num_sections}")
            if self.detected_classes:
                lines.append(f"  Classes: {', '.join(self.detected_classes)}")
            lines.append(f"  Has Animation: {self.has_animation}")
            lines.append(f"  Has Skeleton: {self.has_skeleton}")
            lines.append(f"  Compression: {self.compression_type}")
        return "\n".join(lines)


# --- Skyrim SE Compatibility Check -------------------------------------------

def check_skyrim_compatibility(info: HKXInfo) -> list[str]:
    """
    Check if an HKX file is compatible with Skyrim SE.
    Returns a list of compatibility issues found.
    """
    issues = []

    if not info.valid:
        issues.append("INVALID: File is not a valid HKX file")
        return issues

    # Skyrim SE uses Havok 2014 with 64-bit pointers
    if "2018" in info.havok_version_str or "Elden Ring" in info.havok_version_str:
        issues.append(
            "VERSION: Elden Ring uses Havok 2018, Skyrim SE uses Havok 2014. "
            "Conversion required via hkxcmd or HavokBehaviorPostProcess."
        )

    if info.compression_type == "spline":
        issues.append(
            "COMPRESSION: Spline-compressed animation data. "
            "Soulstruct or custom decompressor needed before retargeting."
        )

    if info.endianness == "big":
        issues.append(
            "ENDIAN: Big-endian format detected. Skyrim SE uses little-endian. "
            "Byte-swap conversion required."
        )

    if not issues:
        issues.append("OK: No obvious compatibility issues detected (further testing needed)")

    return issues


# --- Main --------------------------------------------------------------------

def analyze_path(target_path: Path, summary_only: bool = False) -> list[HKXInfo]:
    """Analyze a single file or all HKX files in a directory."""
    results = []

    if target_path.is_file():
        info = HKXInfo(target_path)
        results.append(info)
    elif target_path.is_dir():
        hkx_files = sorted(target_path.rglob("*.hkx"))
        if not hkx_files:
            print(f"[WARN] No .hkx files found in {target_path}")
            return results
        for hkx_file in hkx_files:
            info = HKXInfo(hkx_file)
            results.append(info)
    else:
        print(f"[ERROR] Path not found: {target_path}")

    return results


def print_results(results: list[HKXInfo], summary_only: bool = False):
    """Print analysis results."""
    if not results:
        print("No files analyzed.")
        return

    if not summary_only:
        for info in results:
            print(f"\n{info}")
            issues = check_skyrim_compatibility(info)
            for issue in issues:
                print(f"  Skyrim Compat: {issue}")

    # Summary
    total = len(results)
    valid = sum(1 for r in results if r.valid)
    with_anim = sum(1 for r in results if r.has_animation)
    with_skel = sum(1 for r in results if r.has_skeleton)
    with_binding = sum(1 for r in results if r.has_binding)

    # Compression types
    compression_counts = {}
    for r in results:
        compression_counts[r.compression_type] = compression_counts.get(r.compression_type, 0) + 1

    # Version distribution
    version_counts = {}
    for r in results:
        if r.valid:
            version_counts[r.havok_version_str] = version_counts.get(r.havok_version_str, 0) + 1

    total_size = sum(r.filesize for r in results)

    print(f"\n{'=' * 60}")
    print(f"  HKX Analysis Summary")
    print(f"{'=' * 60}")
    print(f"  Total files:       {total}")
    print(f"  Valid HKX:         {valid}")
    print(f"  Total size:        {total_size / 1024 / 1024:.2f} MB")
    print(f"  With animation:    {with_anim}")
    print(f"  With skeleton:     {with_skel}")
    print(f"  With binding:      {with_binding}")
    print(f"\n  Havok Versions:")
    for v, count in sorted(version_counts.items()):
        print(f"    {v}: {count} files")
    print(f"\n  Compression Types:")
    for c, count in sorted(compression_counts.items()):
        print(f"    {c}: {count} files")
    print(f"{'=' * 60}")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze extracted HKX files for Skyrim SE compatibility"
    )
    parser.add_argument(
        "path", type=str,
        help="Path to an HKX file or directory containing HKX files"
    )
    parser.add_argument(
        "--summary", action="store_true",
        help="Show only summary statistics"
    )
    parser.add_argument(
        "--export", type=str, default=None,
        help="Export results to JSON file"
    )
    parser.add_argument(
        "--compat-only", action="store_true",
        help="Show only Skyrim SE compatibility issues"
    )
    args = parser.parse_args()

    target = Path(args.path)
    results = analyze_path(target, args.summary)

    if args.compat_only:
        for info in results:
            issues = check_skyrim_compatibility(info)
            problem_issues = [i for i in issues if not i.startswith("OK")]
            if problem_issues:
                print(f"\n{info.filename}:")
                for issue in problem_issues:
                    print(f"  ⚠ {issue}")
    else:
        print_results(results, args.summary)

    if args.export:
        export_data = {
            "analysis_results": [r.to_dict() for r in results],
            "summary": {
                "total_files": len(results),
                "valid_files": sum(1 for r in results if r.valid),
                "total_size_mb": round(sum(r.filesize for r in results) / 1024 / 1024, 2),
                "with_animation": sum(1 for r in results if r.has_animation),
                "with_skeleton": sum(1 for r in results if r.has_skeleton),
            }
        }
        export_path = Path(args.export)
        with open(export_path, "w", encoding="utf-8") as f:
            json.dump(export_data, f, indent=2, ensure_ascii=False)
        print(f"\nExported results to: {export_path}")


if __name__ == "__main__":
    main()
