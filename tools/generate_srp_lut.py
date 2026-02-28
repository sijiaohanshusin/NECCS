#!/usr/bin/env python3
"""
SRP-PHAT 查找表生成器

根据 16 路麦克风物理坐标:
1. 计算所有 C(16,2)=120 对的基线距离
2. 按基线长度降序排列, 选取前 40 对
3. 计算 7x7 粗搜网格的 TDOA 值
4. 输出 C 源文件 ai_srp_lut.c

用法: python tools/generate_srp_lut.py
"""

import numpy as np
from itertools import combinations
import os

# ============================================================
# 麦克风物理坐标 (mm), 中心 (50, 50)
# 按物理位置 ID1~ID16 排列 (PCB 丝印编号)
# ============================================================
MIC_COORDS_MM = np.array([
    [65.00, 50.00],   # ID1
    [36.45, 62.41],   # ID2
    [51.85, 28.87],   # ID3
    [64.43, 68.82],   # ID4
    [24.42, 45.47],   # ID5
    [73.68, 34.94],   # ID6
    [42.21, 78.97],   # ID7
    [35.33, 21.76],   # ID8
    [81.51, 61.51],   # ID9
    [17.48, 63.42],   # ID10
    [65.57, 16.72],   # ID11
    [61.44, 86.49],   # ID12
    [15.66, 30.10],   # ID13
    [90.12, 41.18],   # ID14
    [25.60, 84.71],   # ID15
    [44.38,  6.63],   # ID16
])

# ============================================================
# 通道映射: 物理位置 ID → SAI TDM 通道号 (0-based)
#
# MIC_CHANNEL_MAP[i] = 物理位置 ID(i+1) 对应的 SAI 通道号
# 例: MIC_CHANNEL_MAP[0] = 0 表示 ID1 接在 SAI 通道 0
#
# *** 请根据实际接线修改此表 ***
# 默认: ID(n) → 通道(n-1), 即物理编号与通道号一一对应
# ============================================================
MIC_CHANNEL_MAP = [
     5,   # ID1  → SAI 通道 ?
     4,   # ID2  → SAI 通道 ?
     7,   # ID3  → SAI 通道 ?
     6,   # ID4  → SAI 通道 ?
     3,   # ID5  → SAI 通道 ?
     2,   # ID6  → SAI 通道 ?
     1,   # ID7  → SAI 通道 ?
     0,   # ID8  → SAI 通道 ?
     9,   # ID9  → SAI 通道 ?
     8,   # ID10 → SAI 通道 ?
    11,   # ID11 → SAI 通道 ?
    10,   # ID12 → SAI 通道 ?
    13,   # ID13 → SAI 通道 ?
    12,   # ID14 → SAI 通道 ?
    15,   # ID15 → SAI 通道 ?
    14,   # ID16 → SAI 通道 ?
]

assert len(MIC_CHANNEL_MAP) == len(MIC_COORDS_MM), "映射表长度必须等于麦克风数量"
assert len(set(MIC_CHANNEL_MAP)) == len(MIC_CHANNEL_MAP), "通道号不能重复"

# 构建按 SAI 通道号索引的坐标数组
# mic_coords_by_channel[ch] = 通道 ch 对应的物理坐标 (米)
MIC_CENTER = np.array([50.0, 50.0])
_coords_m = (MIC_COORDS_MM - MIC_CENTER) / 1000.0

MIC_COORDS_M = np.zeros_like(_coords_m)
for phys_id, sai_ch in enumerate(MIC_CHANNEL_MAP):
    MIC_COORDS_M[sai_ch] = _coords_m[phys_id]

# 参数
SPEED_OF_SOUND = 343.0
PAIR_COUNT = 40
COARSE_GRID_SIZE = 7
COARSE_ANGLE_MIN = -60.0
COARSE_ANGLE_MAX = 60.0
COARSE_ANGLE_STEP = 20.0

OUTPUT_PATH = os.path.join(os.path.dirname(__file__),
                           '..', 'START', 'User', 'Algorithm', 'ai_srp_lut.c')


def select_pairs():
    """选取基线最长的 PAIR_COUNT 对麦克风"""
    all_pairs = list(combinations(range(16), 2))
    distances = []
    for i, j in all_pairs:
        dx = MIC_COORDS_M[i, 0] - MIC_COORDS_M[j, 0]
        dy = MIC_COORDS_M[i, 1] - MIC_COORDS_M[j, 1]
        dist = np.sqrt(dx**2 + dy**2)
        distances.append((dist, i, j))

    # 按基线长度降序排列
    distances.sort(key=lambda x: x[0], reverse=True)

    selected = distances[:PAIR_COUNT]
    pair_idx = [(s[1], s[2]) for s in selected]
    pair_dx = [MIC_COORDS_M[s[1], 0] - MIC_COORDS_M[s[2], 0] for s in selected]
    pair_dy = [MIC_COORDS_M[s[1], 1] - MIC_COORDS_M[s[2], 1] for s in selected]

    print(f"Selected {PAIR_COUNT} pairs (baseline range: "
          f"{selected[-1][0]*1000:.2f} ~ {selected[0][0]*1000:.2f} mm)")

    return pair_idx, pair_dx, pair_dy


def compute_coarse_tdoa(pair_dx, pair_dy):
    """计算 7x7 粗搜网格的 TDOA"""
    angles = np.arange(COARSE_ANGLE_MIN, COARSE_ANGLE_MAX + 0.1, COARSE_ANGLE_STEP)
    assert len(angles) == COARSE_GRID_SIZE, f"Expected {COARSE_GRID_SIZE} angles, got {len(angles)}"

    theta_rad = np.deg2rad(angles)  # 水平角
    phi_rad = np.deg2rad(angles)    # 垂直角

    # tdoa_coarse_lut[grid_idx][pair_idx]
    # grid_idx = theta_idx * COARSE_GRID_SIZE + phi_idx
    tdoa = np.zeros((COARSE_GRID_SIZE * COARSE_GRID_SIZE, PAIR_COUNT))

    for ti, th in enumerate(theta_rad):
        for pi, ph in enumerate(phi_rad):
            grid_idx = ti * COARSE_GRID_SIZE + pi
            for p in range(PAIR_COUNT):
                # TDOA = (dx * sin(theta_h) * cos(theta_v) + dy * sin(theta_v)) / c
                tdoa[grid_idx, p] = (pair_dx[p] * np.sin(th) * np.cos(ph) +
                                     pair_dy[p] * np.sin(ph)) / SPEED_OF_SOUND

    return angles, tdoa


def format_float(val):
    """格式化浮点数为 C 字面量"""
    if abs(val) < 1e-15:
        return " 0.0f"
    return f"{val: .10e}f"


def generate_c_file(pair_idx, pair_dx, pair_dy, angles, tdoa):
    """生成 C 源文件"""
    lines = []
    lines.append("/**")
    lines.append(" * @file    ai_srp_lut.c")
    lines.append(" * @brief   SRP-PHAT 预计算查找表 (自动生成, 请勿手动修改)")
    lines.append(" * @note    由 tools/generate_srp_lut.py 生成")
    lines.append(" */")
    lines.append("")
    lines.append('#include "ai_srp_lut.h"')
    lines.append("")

    # --- pair indices ---
    lines.append("/* 麦克风对索引 (按基线长度降序选取的前40对) */")
    lines.append(f"const uint8_t srp_pair_idx[{PAIR_COUNT}][2] = {{")
    for p in range(PAIR_COUNT):
        comma = "," if p < PAIR_COUNT - 1 else ""
        lines.append(f"    {{{pair_idx[p][0]:2d}, {pair_idx[p][1]:2d}}}{comma}")
    lines.append("};")
    lines.append("")

    # --- pair dx ---
    lines.append("/* 麦克风对 x 坐标差 (米) */")
    lines.append(f"const float srp_pair_dx[{PAIR_COUNT}] = {{")
    for p in range(0, PAIR_COUNT, 4):
        chunk = pair_dx[p:min(p+4, PAIR_COUNT)]
        vals = ", ".join(format_float(v) for v in chunk)
        comma = "," if p + 4 < PAIR_COUNT else ""
        lines.append(f"    {vals}{comma}")
    lines.append("};")
    lines.append("")

    # --- pair dy ---
    lines.append("/* 麦克风对 y 坐标差 (米) */")
    lines.append(f"const float srp_pair_dy[{PAIR_COUNT}] = {{")
    for p in range(0, PAIR_COUNT, 4):
        chunk = pair_dy[p:min(p+4, PAIR_COUNT)]
        vals = ", ".join(format_float(v) for v in chunk)
        comma = "," if p + 4 < PAIR_COUNT else ""
        lines.append(f"    {vals}{comma}")
    lines.append("};")
    lines.append("")

    # --- coarse angle tables ---
    lines.append("/* 粗搜网格角度 (度) */")
    vals = ", ".join(f"{a:.1f}f" for a in angles)
    lines.append(f"const float coarse_theta_deg[{COARSE_GRID_SIZE}] = {{{vals}}};")
    lines.append(f"const float coarse_phi_deg[{COARSE_GRID_SIZE}] = {{{vals}}};")
    lines.append("")

    # --- TDOA coarse LUT ---
    total = COARSE_GRID_SIZE * COARSE_GRID_SIZE
    lines.append(f"/* 粗搜 TDOA 查找表 [{total}][{PAIR_COUNT}] (秒) */")
    lines.append(f"const float tdoa_coarse_lut[{total}][{PAIR_COUNT}] = {{")
    for g in range(total):
        theta_idx = g // COARSE_GRID_SIZE
        phi_idx = g % COARSE_GRID_SIZE
        lines.append(f"    /* grid[{g:2d}]: theta={angles[theta_idx]:+.0f} deg, phi={angles[phi_idx]:+.0f} deg */")
        lines.append("    {")
        for p in range(0, PAIR_COUNT, 4):
            chunk = tdoa[g, p:min(p+4, PAIR_COUNT)]
            vals = ", ".join(format_float(v) for v in chunk)
            comma = "," if p + 4 < PAIR_COUNT else ""
            lines.append(f"        {vals}{comma}")
        comma = "," if g < total - 1 else ""
        lines.append(f"    }}{comma}")
    lines.append("};")

    return "\n".join(lines) + "\n"


def main():
    print("=== SRP-PHAT LUT Generator ===")
    print(f"Microphone count: {len(MIC_COORDS_MM)}")
    print(f"Target pair count: {PAIR_COUNT}")
    print()

    # 打印通道映射表
    print("Channel mapping (Physical ID → SAI Channel):")
    for phys_id, sai_ch in enumerate(MIC_CHANNEL_MAP):
        x_mm = MIC_COORDS_MM[phys_id, 0]
        y_mm = MIC_COORDS_MM[phys_id, 1]
        print(f"  ID{phys_id+1:2d} @ ({x_mm:5.2f}, {y_mm:5.2f}) mm → SAI Ch{sai_ch:2d}")
    print()

    pair_idx, pair_dx, pair_dy = select_pairs()
    angles, tdoa = compute_coarse_tdoa(pair_dx, pair_dy)

    c_source = generate_c_file(pair_idx, pair_dx, pair_dy, angles, tdoa)

    output_path = os.path.normpath(OUTPUT_PATH)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(c_source)

    print(f"\nGenerated: {output_path}")
    print(f"File size: {len(c_source)} bytes")

    # Memory estimate
    flash_bytes = (PAIR_COUNT * 2 +                             # pair_idx
                   PAIR_COUNT * 4 * 2 +                         # pair_dx + pair_dy
                   COARSE_GRID_SIZE * 4 * 2 +                   # angle tables
                   COARSE_GRID_SIZE**2 * PAIR_COUNT * 4)        # tdoa_coarse_lut
    print(f"Estimated Flash usage: {flash_bytes} bytes ({flash_bytes/1024:.1f} KB)")


if __name__ == "__main__":
    main()
