#!/usr/bin/env python3
"""
scripts/clean_idle_sprites.py

Deterministic developer utility that repacks the raw 4x4 player idle sprite
sheets for Pulse Court so that the Win32/GDI viewer no longer samples
neighboring-frame pixels.

The raw ImageGen-style 4x4 sheets do not respect hard frame boundaries. This
script treats alpha >= 16 as foreground, finds 8-connected alpha components,
and assigns each non-noise component to its nominal 4x4 grid cell using the
alpha centroid. Components with fewer than 16 pixels are discarded. The
components assigned to a cell are then copied to a transparent output tile
with the frame's bottom/foot region anchored to a common baseline and the
lower-pixel median X aligned to the tile center. An 8px transparent safety
border is preserved on every side.

Inputs:
    assets/sprites/animated/{kite,vale,bastion}_idle_4x4.png

Outputs:
    assets/sprites/clean/{kite,vale,bastion}_idle_4x4.png

Optional Python dependencies:
    - Pillow (PNG I/O)
    - NumPy (alpha/component math)
    - SciPy (8-connected component labelling; a pure-NumPy fallback is used
      if SciPy is not installed)

The normal Pulse Court CMake build and runtime do not require Python, NumPy,
or SciPy. Run this script from the repository root before building the viewer:

    python scripts/clean_idle_sprites.py

The script exits with a non-zero status and prints the required tile size if
the default 320 px tile cannot pack every frame with the required 8px safety
border.
"""

import argparse
import os
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
_NUM_GRID_COLS = 4
_NUM_GRID_ROWS = 4
_LOWER_FRACTION = 4  # bottom 1/4 of the bounding box is used for "foot" X


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


def _pack_frame(
    src: np.ndarray,
    labels: np.ndarray,
    assigned_labels: list[int],
    tile_size: int,
    tile_col: int,
    tile_row: int,
    out: np.ndarray,
) -> None:
    """Copy assigned components into the output tile with baseline alignment."""
    if not assigned_labels:
        return

    all_y = np.concatenate(
        [np.where(labels == lab)[0] for lab in assigned_labels]
    )
    all_x = np.concatenate(
        [np.where(labels == lab)[1] for lab in assigned_labels]
    )

    if all_x.size == 0:
        return

    min_x = int(all_x.min())
    max_x = int(all_x.max())
    min_y = int(all_y.min())
    max_y = int(all_y.max())
    bbox_h = max_y - min_y + 1

    # Lower-pixel median X: median x of the bottom quarter of the content.
    lower_y_threshold = min_y + (bbox_h * (_LOWER_FRACTION - 1) // _LOWER_FRACTION)
    lower_mask = all_y >= lower_y_threshold
    lower_x = all_x[lower_mask]
    if lower_x.size > 0:
        median_x = _round_toward_nearest(float(np.median(lower_x)))
    else:
        median_x = (min_x + max_x) // 2

    # Place the bottom of the bounding box on a baseline 8px above the tile
    # bottom. The tile center X is aligned with the lower-pixel median.
    half_tile = tile_size // 2
    baseline = tile_size - _SAFETY_BORDER
    dx = half_tile - median_x
    dy = baseline - max_y

    for lab in assigned_labels:
        y, x = np.where(labels == lab)
        if y.size == 0:
            continue
        ox = tile_col + x + dx
        oy = tile_row + y + dy
        # Bounds are validated before this is called, but guard anyway.
        valid = (
            (ox >= 0)
            & (ox < out.shape[1])
            & (oy >= 0)
            & (oy < out.shape[0])
        )
        out[oy[valid], ox[valid]] = src[y[valid], x[valid]]


def _compute_required_tile_size(
    labels: np.ndarray,
    assigned_labels: list[int],
) -> int:
    """Return the smallest tile size that packs the given frame with 8px margin."""
    if not assigned_labels:
        return 0

    all_y = np.concatenate(
        [np.where(labels == lab)[0] for lab in assigned_labels]
    )
    all_x = np.concatenate(
        [np.where(labels == lab)[1] for lab in assigned_labels]
    )
    if all_x.size == 0:
        return 0

    min_x = int(all_x.min())
    max_x = int(all_x.max())
    min_y = int(all_y.min())
    max_y = int(all_y.max())
    bbox_h = max_y - min_y + 1

    lower_y_threshold = min_y + (bbox_h * (_LOWER_FRACTION - 1) // _LOWER_FRACTION)
    lower_mask = all_y >= lower_y_threshold
    lower_x = all_x[lower_mask]
    if lower_x.size > 0:
        median_x = _round_toward_nearest(float(np.median(lower_x)))
    else:
        median_x = (min_x + max_x) // 2

    left = median_x - min_x
    right = max_x - median_x
    req_w = 2 * max(left, right) + 2 * _SAFETY_BORDER
    req_h = bbox_h + 2 * _SAFETY_BORDER
    return max(req_w, req_h)


def _clean_idle_sheet(input_path: Path, output_path: Path, tile_size: int) -> None:
    img = Image.open(input_path).convert("RGBA")
    src = np.array(img)
    alpha = src[:, :, 3]
    mask = alpha >= _ALPHA_THRESHOLD

    labels, n = _label_components(mask)

    h, w = mask.shape
    cell_w = w / _NUM_GRID_COLS
    cell_h = h / _NUM_GRID_ROWS

    # Assign each non-noise component to the 4x4 cell containing its alpha
    # centroid. The alpha centroid is the alpha-weighted average of component
    # pixel coordinates.
    grid: dict[tuple[int, int], list[int]] = {}
    for lab in range(1, n + 1):
        y, x = np.where(labels == lab)
        if y.size < _MIN_COMPONENT_SIZE:
            continue
        a = alpha[y, x]
        total_alpha = int(a.sum())
        if total_alpha == 0:
            continue
        cx = float(np.sum(x * a)) / total_alpha
        cy = float(np.sum(y * a)) / total_alpha
        col = int(cx // cell_w)
        row = int(cy // cell_h)
        col = max(0, min(_NUM_GRID_COLS - 1, col))
        row = max(0, min(_NUM_GRID_ROWS - 1, row))
        grid.setdefault((row, col), []).append(lab)

    # Determine the required tile size, optionally bumping it to the user
    # request and validating it against the 4x4 grid.
    required_tile = 0
    for row in range(_NUM_GRID_ROWS):
        for col in range(_NUM_GRID_COLS):
            assigned = grid.get((row, col), [])
            if not assigned:
                raise RuntimeError(
                    f"{input_path.name}: cell ({row},{col}) has no non-noise components"
                )
            required_tile = max(
                required_tile, _compute_required_tile_size(labels, assigned)
            )

    if tile_size < required_tile:
        raise RuntimeError(
            f"{input_path.name}: tile size {tile_size} is too small; "
            f"required tile size is at least {required_tile}px"
        )

    out = np.zeros((tile_size * _NUM_GRID_ROWS, tile_size * _NUM_GRID_COLS, 4), dtype=np.uint8)
    for row in range(_NUM_GRID_ROWS):
        for col in range(_NUM_GRID_COLS):
            assigned = grid[(row, col)]
            _pack_frame(
                src,
                labels,
                assigned,
                tile_size,
                col * tile_size,
                row * tile_size,
                out,
            )

    out_img = Image.fromarray(out, "RGBA")
    out_img.save(output_path)


def _next_multiple_of(value: int, step: int) -> int:
    return ((value + step - 1) // step) * step


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Clean Pulse Court player idle sprite sheets."
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("assets/sprites/animated"),
        help="Directory containing raw *_idle_4x4.png sheets",
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

    # Determine the smallest tile size that can fit all characters. If the user
    # supplied a tile size, it must be a multiple of 4 and large enough.
    required_tile = 0
    for char in args.characters:
        input_path = args.input_dir / f"{char}_idle_4x4.png"
        if not input_path.exists():
            print(f"Input not found: {input_path}", file=sys.stderr)
            return 1

        # Compute the required tile size without writing output.
        img = Image.open(input_path).convert("RGBA")
        src = np.array(img)
        alpha = src[:, :, 3]
        mask = alpha >= _ALPHA_THRESHOLD
        labels, n = _label_components(mask)

        h, w = mask.shape
        cell_w = w / _NUM_GRID_COLS
        cell_h = h / _NUM_GRID_ROWS

        grid: dict[tuple[int, int], list[int]] = {}
        for lab in range(1, n + 1):
            y, x = np.where(labels == lab)
            if y.size < _MIN_COMPONENT_SIZE:
                continue
            a = alpha[y, x]
            total_alpha = int(a.sum())
            if total_alpha == 0:
                continue
            cx = float(np.sum(x * a)) / total_alpha
            cy = float(np.sum(y * a)) / total_alpha
            col = int(cx // cell_w)
            row = int(cy // cell_h)
            col = max(0, min(_NUM_GRID_COLS - 1, col))
            row = max(0, min(_NUM_GRID_ROWS - 1, row))
            grid.setdefault((row, col), []).append(lab)

        for row in range(_NUM_GRID_ROWS):
            for col in range(_NUM_GRID_COLS):
                assigned = grid.get((row, col), [])
                if not assigned:
                    print(
                        f"{input_path.name}: cell ({row},{col}) has no components",
                        file=sys.stderr,
                    )
                    return 1
                required_tile = max(
                    required_tile, _compute_required_tile_size(labels, assigned)
                )

    if args.tile_size:
        if args.tile_size % 4 != 0:
            print(
                "--tile-size must be a multiple of 4 so the 4x4 output grid is "
                "evenly divisible",
                file=sys.stderr,
            )
            return 1
        tile_size = args.tile_size
        if tile_size < required_tile:
            print(
                f"Supplied tile size {tile_size} is too small; "
                f"required tile size is at least {required_tile}px",
                file=sys.stderr,
            )
            return 1
    else:
        tile_size = _next_multiple_of(max(_DEFAULT_TILE_SIZE, required_tile), 4)

    print(f"Using tile size {tile_size}x{tile_size}")

    for char in args.characters:
        input_path = args.input_dir / f"{char}_idle_4x4.png"
        output_path = args.output_dir / f"{char}_idle_4x4.png"
        print(f"Cleaning {input_path} -> {output_path}")
        _clean_idle_sheet(input_path, output_path, tile_size)

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
