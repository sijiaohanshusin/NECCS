#!/usr/bin/env python3
"""
Generate SRP-PHAT coarse TDOA LUT and pair geometry for STM32 firmware.

Default output:
  START/User/Algorithm/ai_srp_lut.c
"""

from __future__ import annotations

import argparse
from itertools import combinations
import os

import numpy as np

# Physical microphone coordinates (mm), centered at (50, 50)
MIC_COORDS_MM = np.array(
    [
        [65.00, 50.00],  # ID1
        [36.45, 62.41],  # ID2
        [51.85, 28.87],  # ID3
        [64.43, 68.82],  # ID4
        [24.42, 45.47],  # ID5
        [73.68, 34.94],  # ID6
        [42.21, 78.97],  # ID7
        [35.33, 21.76],  # ID8
        [81.51, 61.51],  # ID9
        [17.48, 63.42],  # ID10
        [65.57, 16.72],  # ID11
        [61.44, 86.49],  # ID12
        [15.66, 30.10],  # ID13
        [90.12, 41.18],  # ID14
        [25.60, 84.71],  # ID15
        [44.38, 6.63],   # ID16
    ],
    dtype=np.float64,
)

# Mapping: physical ID(n) -> SAI TDM channel (0-based)
MIC_CHANNEL_MAP = [
    5, 4, 7, 6, 3, 2, 1, 0,
    9, 8, 11, 10, 13, 12, 15, 14,
]

MIC_CENTER_MM = np.array([50.0, 50.0], dtype=np.float64)
SPEED_OF_SOUND = 343.0

DEFAULT_PAIR_COUNT = 40
DEFAULT_COARSE_GRID_SIZE = 9
DEFAULT_COARSE_ANGLE_MIN = -60.0
DEFAULT_COARSE_ANGLE_MAX = 60.0
DEFAULT_OUTPUT_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "START", "User", "Algorithm", "ai_srp_lut.c")
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate SRP LUT source file")
    parser.add_argument(
        "--coarse-grid-size",
        type=int,
        default=DEFAULT_COARSE_GRID_SIZE,
        help=f"coarse grid size per axis (default: {DEFAULT_COARSE_GRID_SIZE})",
    )
    parser.add_argument(
        "--coarse-angle-min",
        type=float,
        default=DEFAULT_COARSE_ANGLE_MIN,
        help=f"coarse angle min in degree (default: {DEFAULT_COARSE_ANGLE_MIN})",
    )
    parser.add_argument(
        "--coarse-angle-max",
        type=float,
        default=DEFAULT_COARSE_ANGLE_MAX,
        help=f"coarse angle max in degree (default: {DEFAULT_COARSE_ANGLE_MAX})",
    )
    parser.add_argument(
        "--pair-count",
        type=int,
        default=DEFAULT_PAIR_COUNT,
        help=f"selected microphone pair count (default: {DEFAULT_PAIR_COUNT})",
    )
    parser.add_argument(
        "--output",
        type=str,
        default=DEFAULT_OUTPUT_PATH,
        help=f"output .c file path (default: {DEFAULT_OUTPUT_PATH})",
    )
    return parser.parse_args()


def build_coarse_angles(grid_size: int, angle_min: float, angle_max: float) -> np.ndarray:
    if grid_size < 2:
        raise ValueError("coarse-grid-size must be >= 2")
    if angle_max <= angle_min:
        raise ValueError("coarse-angle-max must be greater than coarse-angle-min")
    return np.linspace(angle_min, angle_max, grid_size, dtype=np.float64)


def build_channel_coords_m() -> np.ndarray:
    """Return coordinates indexed by SAI channel, units in meters."""
    if len(MIC_CHANNEL_MAP) != len(MIC_COORDS_MM):
        raise ValueError("MIC_CHANNEL_MAP length must equal MIC_COORDS_MM length")
    if len(set(MIC_CHANNEL_MAP)) != len(MIC_CHANNEL_MAP):
        raise ValueError("MIC_CHANNEL_MAP has duplicate channel indices")

    coords_rel_m = (MIC_COORDS_MM - MIC_CENTER_MM) / 1000.0
    coords_by_ch = np.zeros_like(coords_rel_m)
    for phys_idx, sai_ch in enumerate(MIC_CHANNEL_MAP):
        coords_by_ch[sai_ch] = coords_rel_m[phys_idx]
    return coords_by_ch


def select_longest_pairs(coords_m: np.ndarray, pair_count: int):
    """Select pair_count longest baselines from all C(16,2) pairs."""
    pairs = list(combinations(range(coords_m.shape[0]), 2))
    if pair_count <= 0 or pair_count > len(pairs):
        raise ValueError(f"pair-count must be in [1, {len(pairs)}]")

    baseline = []
    for i, j in pairs:
        dx = coords_m[i, 0] - coords_m[j, 0]
        dy = coords_m[i, 1] - coords_m[j, 1]
        dist = np.hypot(dx, dy)
        baseline.append((dist, i, j, dx, dy))

    baseline.sort(key=lambda x: x[0], reverse=True)
    sel = baseline[:pair_count]

    pair_idx = [(it[1], it[2]) for it in sel]
    pair_dx = np.array([it[3] for it in sel], dtype=np.float64)
    pair_dy = np.array([it[4] for it in sel], dtype=np.float64)
    return pair_idx, pair_dx, pair_dy


def compute_coarse_tdoa(pair_dx: np.ndarray, pair_dy: np.ndarray, coarse_angles_deg: np.ndarray) -> np.ndarray:
    """tdoa[grid_idx, pair_idx], seconds."""
    theta_rad = np.deg2rad(coarse_angles_deg)
    phi_rad = np.deg2rad(coarse_angles_deg)
    coarse_grid_size = coarse_angles_deg.size
    pair_count = pair_dx.size

    total = coarse_grid_size * coarse_grid_size
    tdoa = np.zeros((total, pair_count), dtype=np.float64)

    for ti, th in enumerate(theta_rad):
        sth = np.sin(th)
        for pi, ph in enumerate(phi_rad):
            cph = np.cos(ph)
            sph = np.sin(ph)
            grid_idx = ti * coarse_grid_size + pi
            tdoa[grid_idx, :] = (pair_dx * sth * cph + pair_dy * sph) / SPEED_OF_SOUND

    return tdoa


def ff(v: float) -> str:
    if abs(v) < 1.0e-15:
        return "0.0f"
    return f"{v:.10e}f"


def emit_c(pair_idx, pair_dx, pair_dy, tdoa, coarse_angles_deg) -> str:
    pair_count = len(pair_idx)
    coarse_grid_size = coarse_angles_deg.size
    total = coarse_grid_size * coarse_grid_size

    lines = []
    lines.append("/* Auto-generated by tools/generate_srp_lut.py. Do not edit manually. */")
    lines.append('#include "ai_srp_lut.h"')
    lines.append("")

    lines.append(f"const uint8_t srp_pair_idx[{pair_count}][2] = {{")
    for i, (a, b) in enumerate(pair_idx):
        comma = "," if i < pair_count - 1 else ""
        lines.append(f"    {{{a:2d}, {b:2d}}}{comma}")
    lines.append("};")
    lines.append("")

    lines.append(f"const float32_t srp_pair_dx[{pair_count}] = {{")
    for i in range(0, pair_count, 4):
        chunk = ", ".join(ff(v) for v in pair_dx[i:i + 4])
        comma = "," if (i + 4) < pair_count else ""
        lines.append(f"    {chunk}{comma}")
    lines.append("};")
    lines.append("")

    lines.append(f"const float32_t srp_pair_dy[{pair_count}] = {{")
    for i in range(0, pair_count, 4):
        chunk = ", ".join(ff(v) for v in pair_dy[i:i + 4])
        comma = "," if (i + 4) < pair_count else ""
        lines.append(f"    {chunk}{comma}")
    lines.append("};")
    lines.append("")

    angle_vals = ", ".join(f"{a:.1f}f" for a in coarse_angles_deg)
    lines.append(f"const float32_t coarse_theta_deg[{coarse_grid_size}] = {{{angle_vals}}};")
    lines.append(f"const float32_t coarse_phi_deg[{coarse_grid_size}] = {{{angle_vals}}};")
    lines.append("")

    lines.append(f"const float32_t tdoa_coarse_lut[{total}][{pair_count}] = {{")
    for g in range(total):
        lines.append("    {")
        for p in range(0, pair_count, 4):
            chunk = ", ".join(ff(v) for v in tdoa[g, p:p + 4])
            comma = "," if (p + 4) < pair_count else ""
            lines.append(f"        {chunk}{comma}")
        comma = "," if g < total - 1 else ""
        lines.append(f"    }}{comma}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    args = parse_args()
    coarse_angles_deg = build_coarse_angles(
        args.coarse_grid_size,
        args.coarse_angle_min,
        args.coarse_angle_max,
    )
    output_path = os.path.normpath(args.output)

    coords_m = build_channel_coords_m()
    pair_idx, pair_dx, pair_dy = select_longest_pairs(coords_m, args.pair_count)
    tdoa = compute_coarse_tdoa(pair_dx, pair_dy, coarse_angles_deg)

    source = emit_c(pair_idx, pair_dx, pair_dy, tdoa, coarse_angles_deg)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(source)

    print("Generated:", output_path)
    print("Grid points:", coarse_angles_deg.size * coarse_angles_deg.size)
    print("Pairs:", args.pair_count)
    print("Angle range:", f"{coarse_angles_deg[0]:.2f} .. {coarse_angles_deg[-1]:.2f}")


if __name__ == "__main__":
    main()
