"""
device_matrix.py — ShortVideoSDK Firebase Test Lab device selector

Usage:
    python3 scripts/ci/device_matrix.py --tier lite   --format gcloud
    python3 scripts/ci/device_matrix.py --tier full   --format json

Tiers
-----
lite     : 3 devices — one flagship, one mid-range, one low-end (fast CI gate)
standard : 8 devices — top Android OEMs, API 24–34 spread
full     : 20 devices — mirrors the Douyin Top-200 matrix subset
"""

import argparse
import json
import sys

# ---------------------------------------------------------------------------
# Device catalogue  (model, version, locale, orientation)
# Model IDs: gcloud firebase test android models list
# ---------------------------------------------------------------------------
CATALOGUE = {
    # ── Flagships ────────────────────────────────────────────────────────────
    "pixel_8_pro":    ("Pixel8Pro",       "34", "en", "portrait"),
    "pixel_7":        ("Pixel7",          "33", "en", "portrait"),
    "pixel_6a":       ("Pixel6a",         "33", "en", "portrait"),
    "samsung_s24":    ("p3q",             "34", "en", "portrait"),   # Galaxy S24
    "samsung_s21":    ("o1q",             "31", "zh", "portrait"),   # Galaxy S21
    "samsung_a54":    ("a54xq",           "33", "zh", "portrait"),   # Galaxy A54 (mid)
    # ── Mid-range ────────────────────────────────────────────────────────────
    "xiaomi_13":      ("thor",            "33", "zh", "portrait"),
    "xiaomi_redmi12": ("fire",            "33", "zh", "portrait"),
    "oppo_find_x6":   ("OP5354L1",        "33", "zh", "portrait"),
    "vivo_x90":       ("V2272A",          "33", "zh", "portrait"),
    "oneplus_11":     ("salami",          "33", "zh", "portrait"),
    # ── Low-end ──────────────────────────────────────────────────────────────
    "samsung_a23":    ("a23xq",           "31", "en", "portrait"),   # API 31, 4GB RAM
    "samsung_a13":    ("a13x",            "31", "en", "portrait"),   # API 31, entry
    # ── Legacy (API 24/26 coverage) ──────────────────────────────────────────
    "pixel_2_api24":  ("walleye",         "24", "en", "portrait"),
    "nexus_5x":       ("bullhead",        "26", "en", "portrait"),
    # ── Tablet ───────────────────────────────────────────────────────────────
    "pixel_tab":      ("tangorpro",       "33", "en", "landscape"),
    "samsung_tab_s8": ("gts8uwifi",       "33", "en", "landscape"),
    # ── Extended coverage ─────────────────────────────────────────────────────
    "pixel_5":        ("redfin",          "30", "en", "portrait"),
    "pixel_4a":       ("sunfish",         "30", "en", "portrait"),
    "samsung_s10":    ("beyond3q",        "29", "zh", "portrait"),
}

TIERS = {
    "lite": [
        "pixel_8_pro",
        "samsung_a54",
        "samsung_a13",
    ],
    "standard": [
        "pixel_8_pro",
        "pixel_6a",
        "samsung_s24",
        "samsung_s21",
        "samsung_a54",
        "xiaomi_13",
        "samsung_a23",
        "pixel_2_api24",
    ],
    "full": list(CATALOGUE.keys()),
}


def build_gcloud_flags(devices: list[str]) -> str:
    """Return repeated --device flags for gcloud firebase test android run."""
    parts = []
    for key in devices:
        model, version, locale, orientation = CATALOGUE[key]
        parts.append(
            f"--device model={model},version={version},"
            f"locale={locale},orientation={orientation}"
        )
    return " \\\n  ".join(parts)


def build_json(devices: list[str]) -> str:
    result = []
    for key in devices:
        model, version, locale, orientation = CATALOGUE[key]
        result.append({
            "key": key,
            "model": model,
            "version": version,
            "locale": locale,
            "orientation": orientation,
        })
    return json.dumps(result, indent=2, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description="ShortVideoSDK device matrix selector")
    parser.add_argument("--tier",   choices=["lite", "standard", "full"], default="standard")
    parser.add_argument("--format", choices=["gcloud", "json"],           default="gcloud")
    args = parser.parse_args()

    keys = TIERS[args.tier]
    unknown = [k for k in keys if k not in CATALOGUE]
    if unknown:
        print(f"ERROR: unknown device keys: {unknown}", file=sys.stderr)
        sys.exit(1)

    if args.format == "gcloud":
        print(build_gcloud_flags(keys))
    else:
        print(build_json(keys))


if __name__ == "__main__":
    main()
