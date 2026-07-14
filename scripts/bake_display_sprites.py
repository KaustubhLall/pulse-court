#!/usr/bin/env python3
"""
scripts/bake_display_sprites.py

Downscales every sheet listed in assets/sprites/animated/manifest.json (that
carries a "displayTile" field) to a shared per-frame tile size close to its
actual on-screen draw size, and writes the result to assets/sprites/display/.
This is what AssetManager::kSheets in src/apps/viewer_assets.cpp actually
loads at runtime.

Why this exists: the master art (raw AI-generated sheets in
sprites/animated/, or their cleaned/repacked form in sprites/clean/ written
by scripts/clean_sprites.py) is authored at several hundred pixels per frame
-- e.g. cleaned player body tiles are 456x456. Nothing on screen ever draws a
body sprite anywhere near that large (see SIZE_NOTES below), so drawing
directly from the master art means GDI+ resamples 10-25x more source pixels
than are ever displayed, every single frame. This script does that expensive
high-quality resample once, offline, instead.

This script does no content-aware cleanup (no alpha-component analysis, no
re-anchoring) -- it assumes each input sheet's frame grid is already correct.
Run scripts/clean_sprites.py first if player body source art changed; then
run this script whenever ANY source sprite sheet changes (body, VFX, or
icons):

    python scripts/clean_sprites.py          # only if body sheets changed
    python scripts/bake_display_sprites.py

SIZE_NOTES -- how each displayTile bucket in manifest.json was chosen, given
the court's internal render resolution is capped at kCourtInternalMaxWidth
(2560px, see viewer_win32.cpp) independent of window/monitor size. Raised
from an initial 1100px cap after profiling showed the internal buffer size
itself (destination pixel count) barely affects cost -- but a first attempt
at scaling every displayTile proportionally with the cap (192->448 for body
sheets) measured a real regression, ~6.5ms -> ~28ms entity render time at
the same destination size. The actual driver is the SOURCE/destination size
MISMATCH (how much resampling work HighQualityBicubic has to do), not
either dimension alone -- this is the same class of problem the bake step
was built to fix in the first place, just re-triggered by making the source
too big again relative to draw size. Rule of thumb used below: pick the
smallest tile that stays close to (not far above) the sheet's typical
on-screen draw size; a mild upscale in the rare worst case (e.g. an ability
pulse, or an infrequently-active persistent effect) is a much better trade
than resampling a much-larger-than-needed source on every draw call.
  - 48px:  ability icons, drawn at a fixed 24x24 in the sidebar/setup screen
           regardless of the internal cap -- does not scale with it.
  - 128px: core/strike-scale transient VFX (core spin/impact, strike sweep,
           dash trail, kickoff pulse) -- tied to kCoreRadius/kStrikeReach,
           the smallest world-space footprints that get their own sheet.
           Not re-profiled at the 2560px cap (low duty cycle, kept at its
           original value); revisit if it turns out to matter.
  - 256px: player-scale sprites (all body sheets, jetstep FX, goal burst) --
           empirically the sweet spot at the 2560px cap: kPlayerRadius draws
           at roughly 205-290px on screen depending on animation pulse, and
           256px measured ~9-12ms entity render time (2 players + occasional
           VFX) vs ~6.5ms at 192px (too little headroom, mild upscale even
           in the common case) and ~28ms at 448px (too much oversampling).
           This is the tier drawn every frame for both players -- the one
           worth actually curve-fitting.
  - 384px: area-scale persistent effects (Anchor Well, Pulse Gate) -- tied to
           kAnchorRadius/kGateRadius, the largest effect footprints on the
           court (Anchor Well alone can reach ~470px on screen at the 2560px
           cap). Bumped from an unretuned 256px (leftover from the original
           1100px cap) for headroom, but not curve-fit like the player tier
           above -- these are only on screen while that ability is active,
           so the performance risk of a slightly-too-small or -large tile is
           much lower than for body sprites.
If the internal resolution cap or a sheet's on-screen size formula changes,
re-derive these -- don't just scale proportionally with the cap, since
oversized source tiles cost real render time regardless of how generous the
destination cap is. Re-run this script afterward; nothing else needs to
change.

Optional Python dependencies (same as clean_sprites.py):
    - Pillow (PNG I/O and resampling)
    - NumPy (alpha-fringing cleanup before resample)
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

try:
    _RESAMPLE = Image.Resampling.LANCZOS
except AttributeError:  # pragma: no cover - older Pillow
    _RESAMPLE = Image.LANCZOS


def _zero_transparent_rgb(img: Image.Image) -> Image.Image:
    """Zero the RGB of fully-transparent pixels before resampling.

    AI-generated sheets sometimes leave arbitrary RGB "garbage" under
    alpha=0 pixels. Lanczos resampling blends RGB and alpha independently in
    Pillow, so garbage color under a transparent pixel can bleed into a
    semi-transparent neighbor's color after downscaling, producing a faint
    fringe around sprite edges. Zeroing it first is a cheap, safe no-op for
    already-clean art and a real fix for sheets that have not been through
    scripts/clean_sprites.py.
    """
    arr = np.array(img.convert("RGBA"))
    transparent = arr[:, :, 3] == 0
    arr[transparent] = 0
    return Image.fromarray(arr, "RGBA")


def bake_one(input_path: Path, output_path: Path, columns: int, rows: int,
            display_tile: int) -> tuple[int, int]:
    img = _zero_transparent_rgb(Image.open(input_path))
    img_w, img_h = img.size
    src_tile = max(img_w / columns, img_h / rows)
    scale = display_tile / src_tile

    out_w = max(1, round(img_w * scale))
    out_h = max(1, round(img_h * scale))
    resized = img.resize((out_w, out_h), _RESAMPLE)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    resized.save(output_path, compress_level=6, optimize=False)
    return out_w, out_h


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Bake manifest-listed sprite sheets down to runtime "
                     "display size."
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=Path("assets"),
        help="Assets root directory (default: assets)",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Path to manifest.json (default: <assets-dir>/sprites/animated/manifest.json)",
    )
    args = parser.parse_args()

    manifest_path = args.manifest or (
        args.assets_dir / "sprites" / "animated" / "manifest.json"
    )
    if not manifest_path.exists():
        print(f"Manifest not found: {manifest_path}", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    output_dir = args.assets_dir / "sprites" / "display"

    baked = 0
    skipped = 0
    for sheet in manifest["sheets"]:
        display_tile = sheet.get("displayTile")
        if display_tile is None:
            skipped += 1
            continue

        input_path = args.assets_dir / sheet["file"]
        if not input_path.exists():
            print(f"Source not found, skipping: {input_path}", file=sys.stderr)
            skipped += 1
            continue

        output_path = output_dir / Path(sheet["file"]).name
        out_w, out_h = bake_one(
            input_path, output_path, sheet["columns"], sheet["rows"], display_tile
        )
        print(f"{input_path.name} -> {output_path.relative_to(args.assets_dir)} "
              f"({out_w}x{out_h}, tile={display_tile})")
        baked += 1

    print(f"\nBaked {baked} sheet(s), skipped {skipped} (no displayTile, e.g. arena.png).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
