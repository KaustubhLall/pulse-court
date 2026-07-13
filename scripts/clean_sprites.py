#!/usr/bin/env python3
"""
scripts/clean_sprites.py

Deterministic developer utility that repacks all Pulse Court player body sprite
sheets (idle, move_loop, move_transition, strike, dash, ability, goal_win,
goal_loss) for kite, vale, and bastion so the Win32/GDI viewer can sample
frames without cross-frame contamination.

The raw ImageGen-style sheets do not respect hard frame-cell boundaries. This
script treats alpha >= 16 as foreground, finds 8-connected alpha components,
discards components with fewer than 16 pixels, and assigns each component to a
nominal grid cell. Components that are >= 90% alpha mass in a single cell are
kept whole; otherwise the component is split per-pixel along cell boundaries.

Each cell's components are then copied into a transparent square tile:
  * X placement: lower-quarter median X centered on the tile center.
  * Idle sheets: each frame's content bottom is anchored 8px above the tile
    bottom.
  * All other sheets: per-row uniform anchoring that preserves vertical motion
    within the row. The lowest frame in the row sits on the 8px baseline and
    the other frames keep their relative offsets.

A single shared tile size is used for every output sheet. Outputs are written
to assets/sprites/clean/.

Optional Python dependencies:
    - Pillow (PNG I/O)
    - NumPy (alpha/component math)
    - SciPy (8-connected component labelling; a pure-NumPy fallback is used
      if SciPy is not installed)

The normal Pulse Court CMake build and runtime do not require Python, NumPy,
or SciPy. Run this script from the repository root before building the viewer:

    python scripts/clean_sprites.py
"""

import argparse
import os
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

try:
    from scipy import ndimage
    _SCIPY = True
except Exception:  # pragma: no cover
    _SCIPY = False


_ALPHA_THRESHOLD = 16
_MIN_COMPONENT_SIZE = 16
_SAFETY_BORDER = 8
_DEFAULT_TILE_SIZE = 320
_LOWER_FRACTION = 4  # bottom 1/4 of the bounding box is used for "foot" X
_MASS_ASSIGN_FRACTION = 0.90
_SHEET_TYPES = [
    "idle",
    "move_loop",
    "move_transition",
    "strike",
    "dash",
    "ability",
    "goal_win",
    "goal_loss",
]


def _label_components(mask: np.ndarray) -> tuple[np.ndarray, int]:
    """Return 8-connected component labels and the number of components."""
    if _SCIPY:
        structure = np.ones((3, 3), dtype=int)
        return ndimage.label(mask, structure=structure)

    # Pure-NumPy 8-connected union-find fallback (slower but dependency-free).
    h, w = mask.shape
    parent = np.arange(h * w, dtype=np.int64)
    rank = np.zeros(h * w, dtype=np.int64)

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(a: int, b: int) -> None:
        ra = find(a)
        rb = find(b)
        if ra == rb:
            return
        if rank[ra] < rank[rb]:
            parent[ra] = rb
        elif rank[ra] > rank[rb]:
            parent[rb] = ra
        else:
            parent[rb] = ra
            rank[ra] += 1

    mask_flat = mask.ravel()
    idx = lambda r, c: r * w + c
    for r in range(h):
        for c in range(w):
            if not mask_flat[idx(r, c)]:
                continue
            # Union with the three already-visited 8-neighbors to avoid
            # duplicate work and keep the pass deterministic.
            for dr, dc in ((-1, -1), (-1, 0), (-1, 1), (0, -1)):
                nr, nc = r + dr, c + dc
                if 0 <= nr < h and 0 <= nc < w and mask_flat[idx(nr, nc)]:
                    union(idx(r, c), idx(nr, nc))

    labels = np.empty(h * w, dtype=np.int64)
    root_to_label = {}
    next_label = 1
    for i in range(h * w):
        if not mask_flat[i]:
            labels[i] = 0
            continue
        root = find(i)
        if root not in root_to_label:
            root_to_label[root] = next_label
            next_label += 1
        labels[i] = root_to_label[root]
    return labels.reshape(h, w), next_label - 1


def _round_toward_nearest(value: float) -> int:
    """Round a float to the nearest integer, matching common expectations."""
    return int(np.floor(value + 0.5))


def _parse_sheet_name(filename: str) -> tuple[str, str, int, int]:
    """Parse char, sheet type, and grid from a filename like kite_strike_6x4.png."""
    stem = Path(filename).stem
    m = re.match(r"^([^_]+)_(.+)_(\d+)x(\d+)$", stem)
    if not m:
        raise ValueError(f"Cannot parse sheet name: {filename}")
    return m.group(1), m.group(2), int(m.group(3)), int(m.group(4))


def _compute_cell_id(
    alpha: np.ndarray, mask: np.ndarray, cols: int, rows: int
) -> np.ndarray:
    """
    Assign each foreground pixel to a grid cell.

    8-connected components are examined. If >= 90% of a component's alpha mass
    lies in one cell, the whole component is assigned to that cell; otherwise
    the component is split per-pixel to the cell that geometrically contains
    the pixel.
    """
    labels, n = _label_components(mask)
    h, w = mask.shape
    cell_w = w / cols
    cell_h = h / rows

    # 255 marks unassigned (background/noise). Cells are 0..(rows*cols-1).
    cell_id = np.full((h, w), 255, dtype=np.uint8)

    for lab in range(1, n + 1):
        y, x = np.where(labels == lab)
        if y.size < _MIN_COMPONENT_SIZE:
            continue
        a = alpha[y, x]
        total_alpha = int(a.sum())
        if total_alpha == 0:
            continue

        col_idx = np.clip((x / cell_w).astype(np.int64), 0, cols - 1)
        row_idx = np.clip((y / cell_h).astype(np.int64), 0, rows - 1)
        cell_idx = row_idx * cols + col_idx

        mass_per_cell = np.bincount(cell_idx, weights=a, minlength=cols * rows)
        dominant = int(np.argmax(mass_per_cell))
        if mass_per_cell[dominant] >= _MASS_ASSIGN_FRACTION * total_alpha:
            cell_id[y, x] = dominant
        else:
            cell_id[y, x] = cell_idx.astype(np.uint8)

    return cell_id


def _compute_cell_info(
    cell_id: np.ndarray, cols: int, rows: int, is_idle: bool
) -> dict[tuple[int, int], dict]:
    """Compute per-cell bounding box, lower median X, and anchoring metadata."""
    cells: dict[tuple[int, int], dict] = {}

    for row in range(rows):
        for col in range(cols):
            idx = row * cols + col
            cell_mask = cell_id == idx
            if not np.any(cell_mask):
                raise RuntimeError(f"cell ({row},{col}) has no non-noise components")

            y, x = np.where(cell_mask)
            min_y = int(y.min())
            max_y = int(y.max())
            min_x = int(x.min())
            max_x = int(x.max())
            bbox_h = max_y - min_y + 1

            lower_y_threshold = min_y + (bbox_h * (_LOWER_FRACTION - 1) // _LOWER_FRACTION)
            lower_mask = y >= lower_y_threshold
            lower_x = x[lower_mask]
            if lower_x.size > 0:
                median_x = _round_toward_nearest(float(np.median(lower_x)))
            else:
                median_x = (min_x + max_x) // 2

            cells[(row, col)] = {
                "min_y": min_y,
                "max_y": max_y,
                "min_x": min_x,
                "max_x": max_x,
                "median_x": median_x,
                "width": max_x - min_x + 1,
                "height": max_y - min_y + 1,
                "count": y.size,
                "bottom_y": max_y,
            }

    # Per-row uniform anchoring for non-idle sheets: all cells in a row share
    # the same bottom anchor (the row's largest max_y).
    if not is_idle:
        for row in range(rows):
            row_max_y = max(cells[(row, col)]["max_y"] for col in range(cols))
            for col in range(cols):
                cells[(row, col)]["bottom_y"] = row_max_y

    return cells


def _required_tile_size(cell_info: dict) -> int:
    """Return the smallest square tile size that fits the given cell."""
    required = 0
    for c in cell_info.values():
        left = c["median_x"] - c["min_x"]
        right = c["max_x"] - c["median_x"]
        req_w = 2 * max(left, right) + 2 * _SAFETY_BORDER
        req_h = (c["bottom_y"] - c["min_y"] + 1) + 2 * _SAFETY_BORDER
        required = max(required, req_w, req_h)
    return required


def _next_multiple_of(value: int, step: int) -> int:
    return ((value + step - 1) // step) * step


def _pack_sheet(
    src: np.ndarray,
    cell_id: np.ndarray,
    cell_info: dict[tuple[int, int], dict],
    tile_size: int,
    cols: int,
    rows: int,
) -> np.ndarray:
    """Repack assigned pixels into a (rows*T, cols*T) RGBA sheet."""
    out = np.zeros((rows * tile_size, cols * tile_size, 4), dtype=np.uint8)

    for row in range(rows):
        for col in range(cols):
            idx = row * cols + col
            ci = cell_info[(row, col)]
            mask = cell_id == idx
            y, x = np.where(mask)
            if y.size == 0:
                continue

            dx = tile_size // 2 - ci["median_x"]
            dy = tile_size - _SAFETY_BORDER - ci["bottom_y"]
            ox = col * tile_size + x + dx
            oy = row * tile_size + y + dy

            valid = (
                (ox >= 0)
                & (ox < out.shape[1])
                & (oy >= 0)
                & (oy < out.shape[0])
            )
            out[oy[valid], ox[valid]] = src[y[valid], x[valid]]

    return out


def _self_check(output_path: Path, cols: int, rows: int, tile_size: int) -> None:
    """Reload output and assert every cell has at least a 2px transparent margin."""
    img = Image.open(output_path).convert("RGBA")
    out = np.array(img)
    if out.shape[0] != rows * tile_size or out.shape[1] != cols * tile_size:
        raise RuntimeError(
            f"{output_path.name}: output size {out.shape[:2]} does not match "
            f"({cols * tile_size}x{rows * tile_size})"
        )

    alpha = out[:, :, 3]
    for row in range(rows):
        for col in range(cols):
            cell = alpha[row * tile_size : (row + 1) * tile_size, col * tile_size : (col + 1) * tile_size]
            mask = cell >= _ALPHA_THRESHOLD
            if not np.any(mask):
                raise RuntimeError(
                    f"{output_path.name}: cell ({row},{col}) is empty after packing"
                )
            ys, xs = np.where(mask)
            if (
                ys.min() < 2
                or xs.min() < 2
                or ys.max() > tile_size - 3
                or xs.max() > tile_size - 3
            ):
                raise RuntimeError(
                    f"{output_path.name}: cell ({row},{col}) has less than 2px "
                    "transparent margin on at least one side"
                )


def _idle_south_metrics(
    cell_info: dict[tuple[int, int], dict], rows: int
) -> tuple[int, int]:
    """Return (median width, median height) for row 2 (south) of an idle sheet."""
    widths = [cell_info[(2, col)]["width"] for col in range(rows)]
    heights = [cell_info[(2, col)]["height"] for col in range(rows)]
    return (
        _round_toward_nearest(float(np.median(widths))),
        _round_toward_nearest(float(np.median(heights))),
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Clean all Pulse Court player body sprite sheets."
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("assets/sprites/animated"),
        help="Directory containing raw *_<cols>x<rows>.png sheets",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("assets/sprites/clean"),
        help="Directory for cleaned output sheets",
    )
    parser.add_argument(
        "--tile-size",
        type=int,
        default=0,
        help="Output tile size in pixels (default: smallest multiple of 4 "
             ">= 320 that fits every frame)",
    )
    parser.add_argument(
        "--characters",
        nargs="+",
        default=["kite", "vale", "bastion"],
        help="Character prefixes to process",
    )
    args = parser.parse_args()

    if not args.input_dir.exists():
        print(f"Input directory not found: {args.input_dir}", file=sys.stderr)
        return 1

    args.output_dir.mkdir(parents=True, exist_ok=True)

    # Build the list of sheets to process.
    sheets: list[dict] = []
    for char in args.characters:
        for stype in _SHEET_TYPES:
            pattern = f"{char}_{stype}_*.png"
            matches = sorted(p for p in args.input_dir.glob(pattern) if p.is_file())
            if not matches:
                print(f"Input not found: {args.input_dir / pattern}", file=sys.stderr)
                return 1
            if len(matches) > 1:
                print(
                    f"Ambiguous input for {pattern}: {matches}", file=sys.stderr
                )
                return 1
            input_path = matches[0]
            try:
                char_parsed, type_parsed, cols, rows = _parse_sheet_name(input_path.name)
            except ValueError as e:
                print(f"Bad filename {input_path.name}: {e}", file=sys.stderr)
                return 1
            if char_parsed != char or type_parsed != stype:
                print(f"Filename mismatch: {input_path.name}", file=sys.stderr)
                return 1
            sheets.append(
                {
                    "input_path": input_path,
                    "output_path": args.output_dir / input_path.name,
                    "char": char,
                    "type": stype,
                    "cols": cols,
                    "rows": rows,
                    "is_idle": stype == "idle",
                }
            )

    # First pass: determine the shared tile size and collect idle metrics.
    required_tile = 0
    idle_metrics: dict[str, tuple[int, int]] = {}

    for sheet in sheets:
        print(f"Measuring {sheet['input_path'].name} ...")
        img = Image.open(sheet["input_path"]).convert("RGBA")
        src = np.array(img)
        alpha = src[:, :, 3]
        mask = alpha >= _ALPHA_THRESHOLD

        cell_id = _compute_cell_id(alpha, mask, sheet["cols"], sheet["rows"])
        cell_info = _compute_cell_info(
            cell_id, sheet["cols"], sheet["rows"], sheet["is_idle"]
        )

        sheet["cell_id"] = cell_id
        sheet["cell_info"] = cell_info
        required_tile = max(required_tile, _required_tile_size(cell_info))

        if sheet["is_idle"]:
            idle_metrics[sheet["char"]] = _idle_south_metrics(cell_info, sheet["rows"])

    if args.tile_size:
        if args.tile_size % 4 != 0:
            print(
                "--tile-size must be a multiple of 4",
                file=sys.stderr,
            )
            return 1
        if args.tile_size < required_tile:
            print(
                f"Supplied tile size {args.tile_size} is too small; "
                f"required tile size is at least {required_tile}px",
                file=sys.stderr,
            )
            return 1
        tile_size = args.tile_size
    else:
        tile_size = _next_multiple_of(
            max(_DEFAULT_TILE_SIZE, required_tile), 4
        )

    print(f"\nUsing shared tile size {tile_size}x{tile_size}\n")

    # Second pass: write cleaned sheets.
    for sheet in sheets:
        print(f"Packing {sheet['input_path'].name} -> {sheet['output_path']}")
        src = np.array(Image.open(sheet["input_path"]).convert("RGBA"))
        out = _pack_sheet(
            src,
            sheet["cell_id"],
            sheet["cell_info"],
            tile_size,
            sheet["cols"],
            sheet["rows"],
        )
        Image.fromarray(out, "RGBA").save(
            sheet["output_path"], compress_level=6, optimize=False
        )

    # Post-write self-check.
    print("\nRunning post-write self-check ...")
    for sheet in sheets:
        _self_check(
            sheet["output_path"], sheet["cols"], sheet["rows"], tile_size
        )

    # Summary.
    print("\nMetrics:")
    print(f"  Shared tile size: {tile_size}")
    print("  Idle south row (row 2) median content size:")
    for char in args.characters:
        width, height = idle_metrics[char]
        print(f"    {char}: height={height}, width={width}")

    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
