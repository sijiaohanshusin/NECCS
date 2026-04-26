#!/usr/bin/env python3
"""Generate the 32-channel industrial acoustic-array deliverables."""

from __future__ import annotations

import csv
import json
import math
from dataclasses import dataclass
from datetime import date
from html import escape
from itertools import combinations
from pathlib import Path
from string import Template

ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = ROOT / "docs"
CSV_PATH = DOCS_DIR / "array_32ch_coords.csv"
HTML_PATH = DOCS_DIR / "array_32ch_design.html"
PLOT_JS_PATH = ROOT / "tools" / "plot_array_32ch.js"
DOCS_PLOT_JS_PATH = DOCS_DIR / "plot_array_32ch.js"

BOARD_HALF_MM = 50.0
KEEPOUT_HALF_MM = 14.0
SPEED_OF_SOUND = 343.0
TODAY = date.today().isoformat()

# Footprint-local definition:
# local +X points from package center toward the acoustic port in the provided
# footprint drawing. The port center sits 0.68 mm to the right of the package
# center in that local frame.
DEFAULT_PORT_OFFSET_X_MM = 0.68
DEFAULT_PORT_OFFSET_Y_MM = 0.00


@dataclass(frozen=True)
class Mic:
    mic_id: str
    acoustic_x_mm: float
    acoustic_y_mm: float
    default_rotation_deg: float
    bus: str
    chip: str
    chip_slot: int
    tdm_slot_48k: int
    tdm_slot_192k_core: int | None
    is_core16: bool


@dataclass(frozen=True)
class Scheme:
    key: str
    title: str
    short_label: str
    rationale: str
    strengths: str
    tradeoffs: str
    mics: tuple[Mic, ...]


@dataclass(frozen=True)
class Mode:
    name: str
    sample_rate: int
    nfft: int
    coarse_grid: int
    fine_grid: int
    fine_top_k: int
    pair_budget: int
    band_low_hz: int
    band_high_hz: int
    mic_ids: tuple[str, ...]
    chips: str
    pair_strategy: str
    algorithm: str
    advantages: str
    limitations: str
    suited_for: str
    cadence_class: str


CURRENT_BASELINE_16_MM = (
    (15.00, 0.00), (-13.55, 12.41), (1.85, -21.13), (14.43, 18.82),
    (-25.58, -4.53), (23.68, -15.06), (-7.79, 28.97), (-14.67, -28.24),
    (31.51, 11.51), (-32.52, 13.42), (15.57, -33.28), (11.44, 36.49),
    (-34.34, -19.90), (40.12, -8.82), (-24.40, 34.71), (-5.62, -43.37),
)

CHIP_I2C_ADDR = {
    "U1": "0x4C",
    "U2": "0x4D",
    "U3": "0x4E",
    "U4": "0x4F",
}

# (bus, chip, chip_slot, tdm48_slot, tdm192_slot_if_core, is_core16)
ASSIGNMENTS = {
    "M01": ("A", "U1", 0, 0, 0, True),
    "M02": ("A", "U1", 1, 1, 1, True),
    "M03": ("A", "U2", 0, 8, 4, True),
    "M04": ("A", "U2", 1, 9, 5, True),
    "M05": ("B", "U3", 0, 0, 0, True),
    "M06": ("B", "U3", 1, 1, 1, True),
    "M07": ("B", "U4", 0, 8, 4, True),
    "M08": ("B", "U4", 1, 9, 5, True),
    "M09": ("A", "U1", 2, 2, 2, True),
    "M10": ("A", "U1", 3, 3, 3, True),
    "M11": ("A", "U2", 2, 10, 6, True),
    "M12": ("A", "U2", 3, 11, 7, True),
    "M13": ("B", "U3", 2, 2, 2, True),
    "M14": ("B", "U3", 3, 3, 3, True),
    "M15": ("B", "U4", 2, 10, 6, True),
    "M16": ("B", "U4", 3, 11, 7, True),
    "M17": ("A", "U1", 4, 4, None, False),
    "M18": ("A", "U1", 5, 5, None, False),
    "M19": ("A", "U1", 6, 6, None, False),
    "M20": ("A", "U1", 7, 7, None, False),
    "M21": ("B", "U4", 4, 12, None, False),
    "M22": ("B", "U4", 5, 13, None, False),
    "M23": ("B", "U4", 6, 14, None, False),
    "M24": ("B", "U4", 7, 15, None, False),
    "M25": ("B", "U3", 4, 4, None, False),
    "M26": ("B", "U3", 5, 5, None, False),
    "M27": ("B", "U3", 6, 6, None, False),
    "M28": ("B", "U3", 7, 7, None, False),
    "M29": ("A", "U2", 4, 12, None, False),
    "M30": ("A", "U2", 5, 13, None, False),
    "M31": ("A", "U2", 6, 14, None, False),
    "M32": ("A", "U2", 7, 15, None, False),
}


def outward_rotation_deg(x_mm: float, y_mm: float) -> float:
    angle = math.degrees(math.atan2(y_mm, x_mm))
    return (angle + 360.0) % 360.0


def build_scheme(
    key: str,
    title: str,
    short_label: str,
    rationale: str,
    strengths: str,
    tradeoffs: str,
    coords: tuple[tuple[str, float, float], ...],
) -> Scheme:
    mics: list[Mic] = []
    for mic_id, x_mm, y_mm in coords:
        bus, chip, chip_slot, slot48, slot192, is_core = ASSIGNMENTS[mic_id]
        mics.append(
            Mic(
                mic_id=mic_id,
                acoustic_x_mm=x_mm,
                acoustic_y_mm=y_mm,
                default_rotation_deg=outward_rotation_deg(x_mm, y_mm),
                bus=bus,
                chip=chip,
                chip_slot=chip_slot,
                tdm_slot_48k=slot48,
                tdm_slot_192k_core=slot192,
                is_core16=is_core,
            )
        )
    validate_scheme(tuple(mics))
    return Scheme(key, title, short_label, rationale, strengths, tradeoffs, tuple(mics))


def validate_scheme(mics: tuple[Mic, ...]) -> None:
    if len(mics) != 32:
        raise ValueError("scheme must contain exactly 32 microphones")
    ids = {m.mic_id for m in mics}
    if ids != {f"M{i:02d}" for i in range(1, 33)}:
        raise ValueError("scheme ids must be M01..M32")
    for mic in mics:
        if abs(mic.acoustic_x_mm) > BOARD_HALF_MM or abs(mic.acoustic_y_mm) > BOARD_HALF_MM:
            raise ValueError(f"{mic.mic_id} outside board outline")
        if abs(mic.acoustic_x_mm) < KEEPOUT_HALF_MM and abs(mic.acoustic_y_mm) < KEEPOUT_HALF_MM:
            raise ValueError(f"{mic.mic_id} violates keepout")
    for chip in CHIP_I2C_ADDR:
        chip_mics = sorted((m for m in mics if m.chip == chip), key=lambda item: item.chip_slot)
        if len(chip_mics) != 8:
            raise ValueError(f"{chip} must own 8 microphones")
        if [m.chip_slot for m in chip_mics] != list(range(8)):
            raise ValueError(f"{chip} chip slots must be 0..7")
        if sum(1 for m in chip_mics if m.is_core16) != 4:
            raise ValueError(f"{chip} must contribute 4 core microphones")


MAIN_SCHEME = build_scheme(
    "main",
    "工业化三尺度主方案：Inner8 + Transition8 + Outer16",
    "主方案",
    "把阵列拆成高频短基线层、宽频桥接层和大孔径成像层，同时满足 192k 模式下 4 片 PCMD3180 都参与、每片只开 4 个核心 mic 的带宽现实。",
    "110 mm 级有效孔径 + 7.64 mm 最小间距，兼顾宽频、旁瓣控制和 192k 近场高频精扫。",
    "50~80 kHz 仍主要是近场热点确认，20~50 m 只能承诺强异常检测，不承诺细致热图。",
    (
        ("M01", 15.0, 1.0), ("M02", 15.8, 8.6), ("M03", 8.2, 15.2), ("M04", -0.8, 15.7),
        ("M05", -15.3, 7.1), ("M06", -15.6, -0.8), ("M07", -8.3, -14.8), ("M08", 1.5, -15.4),
        ("M09", 25.5, 10.5), ("M10", 13.0, 27.5), ("M11", -11.0, 26.0), ("M12", -27.5, 11.0),
        ("M13", -25.0, -11.5), ("M14", -13.5, -28.0), ("M15", 10.5, -26.5), ("M16", 27.0, -12.0),
        ("M17", 5.0, 46.0), ("M18", 19.0, 47.0), ("M19", 36.0, 44.5), ("M20", 46.0, 28.0),
        ("M21", 47.0, 10.0), ("M22", 43.0, -11.0), ("M23", 31.0, -31.0), ("M24", 13.0, -46.0),
        ("M25", -8.0, -47.0), ("M26", -26.0, -45.0), ("M27", -43.0, -33.0), ("M28", -47.0, -12.0),
        ("M29", -47.0, 9.0), ("M30", -40.0, 30.0), ("M31", -24.0, 46.0), ("M32", -3.0, 44.0),
    ),
)

ALT_DOUBLE_RING = build_scheme(
    "alt_double_ring",
    "备选 A：非均匀双环阵列",
    "备选 A",
    "角向覆盖直观，适合做对照组和答辩解释。",
    "结构清晰、布线压力中等、近似各向同性。",
    "重复基线更多，规则副瓣风险高于主方案，192k 子阵不如主方案顺手。",
    (
        ("M01", 21.5, 6.0), ("M02", 18.0, 13.0), ("M03", 13.0, 19.5), ("M04", 6.5, 24.0),
        ("M05", -5.5, 24.5), ("M06", -14.0, 19.5), ("M07", -21.0, 14.0), ("M08", -24.5, 6.0),
        ("M09", -24.0, -6.5), ("M10", -18.5, -13.5), ("M11", -13.0, -18.5), ("M12", -6.0, -24.0),
        ("M13", 6.0, -23.5), ("M14", 13.5, -18.5), ("M15", 19.5, -13.0), ("M16", 24.5, -5.5),
        ("M17", 44.0, 6.0), ("M18", 38.0, 22.0), ("M19", 29.0, 38.0), ("M20", 12.0, 46.0),
        ("M21", -10.0, 45.0), ("M22", -27.0, 38.0), ("M23", -39.5, 22.5), ("M24", -46.0, 7.0),
        ("M25", -45.5, -6.0), ("M26", -38.0, -23.0), ("M27", -28.0, -39.0), ("M28", -11.5, -46.0),
        ("M29", 12.0, -45.0), ("M30", 29.0, -38.0), ("M31", 39.5, -21.5), ("M32", 46.5, -5.5),
    ),
)

ALT_SPARSE_PLANE = build_scheme(
    "alt_sparse_plane",
    "备选 B：边缘加权稀疏平面阵列",
    "备选 B",
    "进一步强调边缘占比，对低频和远距离强源检测更积极。",
    "中低频基线覆盖更足，远距强源检测潜力更好。",
    "Core16 抽取不如主方案优雅，高频近场模式的一致性稍差。",
    (
        ("M01", 4.0, 20.0), ("M02", 18.0, 8.0), ("M03", 31.0, 20.0), ("M04", 24.0, 34.0),
        ("M05", -6.0, 19.0), ("M06", -20.0, 7.0), ("M07", -33.0, 18.0), ("M08", -23.0, 33.0),
        ("M09", -19.0, -8.0), ("M10", -5.0, -21.0), ("M11", -31.0, -22.0), ("M12", -22.0, -34.0),
        ("M13", 7.0, -19.0), ("M14", 21.0, -6.0), ("M15", 33.0, -18.0), ("M16", 24.0, -33.0),
        ("M17", 2.0, 46.0), ("M18", 17.0, 48.0), ("M19", 34.0, 43.0), ("M20", 46.0, 24.0),
        ("M21", -13.0, 47.0), ("M22", -31.0, 45.0), ("M23", -46.0, 28.0), ("M24", -48.0, 8.0),
        ("M25", -47.0, -10.0), ("M26", -43.0, -31.0), ("M27", -28.0, -46.0), ("M28", -9.0, -48.0),
        ("M29", 9.0, -47.0), ("M30", 28.0, -45.0), ("M31", 44.0, -30.0), ("M32", 48.0, -9.0),
    ),
)

SCHEMES = (MAIN_SCHEME, ALT_DOUBLE_RING, ALT_SPARSE_PLANE)
INNER8_IDS = tuple(f"M{i:02d}" for i in range(1, 9))
CORE16_IDS = tuple(f"M{i:02d}" for i in range(1, 17))
HF_CLEAN_IDS = ("M01", "M02", "M03", "M04", "M05", "M06", "M07", "M08", "M09", "M11", "M13", "M15")

MODES = (
    Mode(
        "32 路通用宽孔径主模式",
        48_000,
        256,
        9,
        5,
        3,
        96,
        1_500,
        14_000,
        tuple(f"M{i:02d}" for i in range(1, 33)),
        "U1~U4 / 双 TDM16",
        "Outer16 长中基线为主，叠加 Core16 做稳定补偿",
        "SRP-PHAT + 频带自适应加权 + Top-3 粗细搜索",
        "工业巡检主模式，兼顾检测、定位、热图展示。",
        "16 kHz 以上不应承诺稳定高质量热图。",
        "定位 + 成像",
        "实时",
    ),
    Mode(
        "16 路高采样核心模式",
        192_000,
        512,
        11,
        5,
        3,
        48,
        6_000,
        40_000,
        CORE16_IDS,
        "U1~U4 / 双 TDM8（4 片都工作，每片 4 core mic）",
        "保留核心短中基线，放弃外围长基线",
        "SRP-PHAT + ROI 内 MVDR/MUSIC 精修",
        "适合近场高频泄漏、局放、摩擦尖叫类异常。",
        "更适合作为低帧率高质量模式，不宜常开。",
        "检测 + 近场定位",
        "低帧率高质量",
    ),
    Mode(
        "12 路高频低旁瓣模式",
        192_000,
        512,
        11,
        5,
        2,
        28,
        12_000,
        30_000,
        HF_CLEAN_IDS,
        "U1~U4 / 采 16 core，算法只用 12 路",
        "剔除重复短基线较多的点，压假峰",
        "SRP-PHAT + 高频门控 + 空间加权",
        "高频热图更干净，异常热点更稳。",
        "角分辨率不如全 16/32 路。",
        "成像 + 热点确认",
        "实时",
    ),
    Mode(
        "8 路超高频热点确认模式",
        192_000,
        512,
        13,
        5,
        2,
        12,
        25_000,
        80_000,
        INNER8_IDS,
        "U1~U4 / 采 16 core，处理 Inner8",
        "只保留最短基线，拒绝伪高分辨",
        "GCC-PHAT + 局部 SRP/MUSIC",
        "适合 40~80 kHz 研究级近场热点确认。",
        "不是高质量全场成像模式。",
        "检测 + 方位估计",
        "研究模式",
    ),
    Mode(
        "中频主成像模式",
        48_000,
        256,
        9,
        5,
        3,
        112,
        2_000,
        8_000,
        tuple(f"M{i:02d}" for i in range(1, 33)),
        "U1~U4 / 双 TDM16",
        "中长基线偏置",
        "SRP-PHAT + 多帧融合",
        "最稳妥的比赛主画面频段。",
        "对 20 kHz 以上异常不敏感。",
        "成像",
        "实时",
    ),
    Mode(
        "低频增强模式",
        48_000,
        256,
        9,
        5,
        3,
        120,
        800,
        4_000,
        tuple(f"M{i:02d}" for i in range(1, 33)),
        "U1~U4 / 双 TDM16",
        "长中基线全开，多帧补偿",
        "SRP-PHAT + 超指向后滤波 + IMU 微运动补偿",
        "尽量榨干 100 mm 手持阵列的低频能力。",
        "仍以粗定位和热点增强为主，不是低频高清热图。",
        "检测 + 粗定位",
        "低帧率高质量",
    ),
    Mode(
        "远距离强源检测模式",
        48_000,
        256,
        9,
        5,
        2,
        56,
        2_000,
        10_000,
        tuple(f"M{i:02d}" for i in range(1, 33)),
        "U1~U4 / 双 TDM16",
        "Outer16 最长基线优先，Core16 只做置信度校核",
        "SRP-PHAT + 稀疏长基线 pair set",
        "对中远距离强异常源更敏感。",
        "热图更尖、更挑环境，不适合作为默认展示。",
        "检测 + 定位",
        "实时",
    ),
)


def layer_of(mic_id: str) -> str:
    number = int(mic_id[1:])
    if number <= 8:
        return "Inner8"
    if number <= 16:
        return "Transition8"
    return "Outer16"


def subset_for_ids(scheme: Scheme, mic_ids: tuple[str, ...]) -> tuple[Mic, ...]:
    wanted = set(mic_ids)
    return tuple(m for m in scheme.mics if m.mic_id in wanted)


def pdm_pair_index(mic: Mic) -> int:
    return mic.chip_slot // 2 + 1


def pdm_edge_label(mic: Mic) -> str:
    return "L" if mic.chip_slot % 2 == 0 else "R"


def pdm_data_pin_name(mic: Mic) -> str:
    pair = pdm_pair_index(mic)
    return f"PDMDIN{pair}_GPI{pair}"


def pdm_clock_pin_name(mic: Mic) -> str:
    pair = pdm_pair_index(mic)
    return f"PDMCLK{pair}_GPO{pair}"


def board_pdm_data_net(mic: Mic) -> str:
    return f"{mic.chip}_PDM_DAT{pdm_pair_index(mic)}"


def board_pdm_clock_net(mic: Mic) -> str:
    return f"{mic.chip}_PDM_CLK{pdm_pair_index(mic)}"


def chip_channel_name(mic: Mic) -> str:
    return f"CH{mic.chip_slot + 1}"


def tdm_bus_name(bus: str) -> str:
    return f"TDM_{bus}"


def tdm_bclk_net(bus: str) -> str:
    return f"TDM_{bus}_BCLK"


def tdm_fsync_net(bus: str) -> str:
    return f"TDM_{bus}_FSYNC"


def tdm_sdout_net(bus: str) -> str:
    return f"TDM_{bus}_SDOUT"


def rotated_offset(offset_x_mm: float, offset_y_mm: float, rotation_deg: float) -> tuple[float, float]:
    theta = math.radians(rotation_deg)
    rot_x = offset_x_mm * math.cos(theta) - offset_y_mm * math.sin(theta)
    rot_y = offset_x_mm * math.sin(theta) + offset_y_mm * math.cos(theta)
    return rot_x, rot_y


def package_center(mic: Mic, offset_x_mm: float = DEFAULT_PORT_OFFSET_X_MM, offset_y_mm: float = DEFAULT_PORT_OFFSET_Y_MM, rotation_deg: float | None = None) -> tuple[float, float]:
    rotation = mic.default_rotation_deg if rotation_deg is None else rotation_deg
    rot_x, rot_y = rotated_offset(offset_x_mm, offset_y_mm, rotation)
    return mic.acoustic_x_mm - rot_x, mic.acoustic_y_mm - rot_y


def pairwise_distances(mics: tuple[Mic, ...]) -> list[tuple[float, str, str]]:
    distances: list[tuple[float, str, str]] = []
    for left, right in combinations(mics, 2):
        distances.append(
            (
                math.hypot(left.acoustic_x_mm - right.acoustic_x_mm, left.acoustic_y_mm - right.acoustic_y_mm),
                left.mic_id,
                right.mic_id,
            )
        )
    distances.sort(key=lambda item: item[0])
    return distances


def geometry_summary(mics: tuple[Mic, ...]) -> dict[str, float]:
    distances = pairwise_distances(mics)
    dmin, _, _ = distances[0]
    dmax, _, _ = distances[-1]
    return {
        "dmin_mm": dmin,
        "dmax_mm": dmax,
        "alias_hz": SPEED_OF_SOUND / (2.0 * (dmin / 1000.0)),
    }


def duplicate_baseline_ratio(mics: tuple[Mic, ...], bin_mm: float = 0.2) -> float:
    buckets: dict[float, int] = {}
    for distance, _, _ in pairwise_distances(mics):
        bucket = round(distance / bin_mm) * bin_mm
        buckets[bucket] = buckets.get(bucket, 0) + 1
    duplicate_pairs = sum(count - 1 for count in buckets.values() if count > 1)
    total_pairs = len(mics) * (len(mics) - 1) // 2
    return 100.0 * duplicate_pairs / total_pairs


def angle_resolution_deg(aperture_mm: float, frequency_hz: float) -> float:
    wavelength_m = SPEED_OF_SOUND / frequency_hz
    aperture_m = aperture_mm / 1000.0
    theta_rad = wavelength_m / aperture_m
    return math.degrees(theta_rad)


def fraunhofer_distance_m(aperture_mm: float, frequency_hz: float) -> float:
    aperture_m = aperture_mm / 1000.0
    wavelength_m = SPEED_OF_SOUND / frequency_hz
    return 2.0 * aperture_m * aperture_m / wavelength_m


def current_baseline_metrics() -> dict[str, float]:
    distances = []
    for left, right in combinations(CURRENT_BASELINE_16_MM, 2):
        distances.append(math.hypot(left[0] - right[0], left[1] - right[1]))
    distances.sort()
    dmin = distances[0]
    dmax = distances[-1]
    return {
        "dmin_mm": dmin,
        "dmax_mm": dmax,
        "alias_hz": SPEED_OF_SOUND / (2.0 * (dmin / 1000.0)),
    }


def fmt(value: float, digits: int = 2) -> str:
    return f"{value:.{digits}f}"


def html_table(headers: tuple[str, ...], rows: list[list[str]]) -> str:
    head = "".join(f"<th>{escape(header)}</th>" for header in headers)
    body_rows = []
    for row in rows:
        body_rows.append("<tr>" + "".join(f"<td>{escape(cell)}</td>" for cell in row) + "</tr>")
    return "<table><thead><tr>" + head + "</tr></thead><tbody>" + "".join(body_rows) + "</tbody></table>"


def comparison_summary_table() -> str:
    rows = [
        ["为何主方案更优", "三尺度结构同时照顾 32@48k 的大孔径和 16@192k 的核心高频模式。", "结构直观但规则副瓣更明显。", "低频/远距更积极，但核心子阵抽取不如主方案。"],
        ["哪个方案低频更强", "中上水平。", "中等。", "最强。"],
        ["哪个方案高频旁瓣更低", "最好。", "最差。", "中等。"],
        ["哪个方案对子阵模式更友好", "最好。", "一般。", "较弱。"],
        ["哪个方案更适合 STM32N6 实时实现", "最好，模式切换自然。", "中等。", "中等偏上。"],
    ]
    return html_table(("对比项", MAIN_SCHEME.short_label, ALT_DOUBLE_RING.short_label, ALT_SPARSE_PLANE.short_label), rows)


def scheme_metrics_table() -> str:
    rows = []
    for scheme in SCHEMES:
        geom = geometry_summary(scheme.mics)
        core = geometry_summary(tuple(m for m in scheme.mics if m.is_core16))
        dup = duplicate_baseline_ratio(scheme.mics)
        rows.append(
            [
                scheme.short_label,
                fmt(geom["dmax_mm"]),
                fmt(geom["dmin_mm"]),
                fmt(geom["alias_hz"] / 1000.0),
                fmt(core["dmax_mm"]),
                fmt(dup, 1),
            ]
        )
    return html_table(("方案", "32 路孔径 mm", "32 路最小间距 mm", "32 路混叠起点 kHz", "Core16 孔径 mm", "重复基线占比 %"), rows)


def performance_tables() -> dict[str, str]:
    baseline = current_baseline_metrics()
    main32 = geometry_summary(MAIN_SCHEME.mics)
    core16 = geometry_summary(tuple(m for m in MAIN_SCHEME.mics if m.is_core16))
    inner8 = geometry_summary(subset_for_ids(MAIN_SCHEME, INNER8_IDS))
    main_dup = duplicate_baseline_ratio(MAIN_SCHEME.mics)
    alt_dup = duplicate_baseline_ratio(ALT_DOUBLE_RING.mics)
    rows_32 = [
        ["最大有效孔径", f"{fmt(main32['dmax_mm'])} mm"],
        ["最小阵元间距", f"{fmt(main32['dmin_mm'])} mm"],
        ["空间混叠起点", f"{fmt(main32['alias_hz'] / 1000.0)} kHz"],
        ["推荐使用频带", "1.5 kHz ~ 14 kHz"],
        ["理论角分辨率", ", ".join(f"{f//1000} kHz: {fmt(angle_resolution_deg(main32['dmax_mm'], f), 1)}°" for f in (4_000, 8_000, 12_000, 16_000))],
        ["近/远场边界", ", ".join(f"{f//1000} kHz: {fmt(fraunhofer_distance_m(main32['dmax_mm'], f), 2)} m" for f in (4_000, 8_000, 12_000))],
        ["现实可检测距离", "0.3 m ~ 20 m；安静环境下强异常源可向 20~50 m 试探"],
        ["现实可定位距离", "0.3 m ~ 8 m"],
        ["现实可成像距离", "0.25 m ~ 4 m"],
        ["旁瓣代理指标", f"重复基线占比 {fmt(main_dup, 1)}%，低于双环备选的 {fmt(alt_dup, 1)}%"],
        ["相对当前 16 路提升", f"孔径 +{fmt((main32['dmax_mm'] / baseline['dmax_mm'] - 1.0) * 100.0)}%，混叠起点 +{fmt((main32['alias_hz'] / baseline['alias_hz'] - 1.0) * 100.0)}%"],
    ]
    rows_core = [
        ["采集结构", "4 片 PCMD3180 同时工作，每片只开 4 个 core mic；双总线 TDM8 同步"],
        ["最大有效孔径", f"{fmt(core16['dmax_mm'])} mm"],
        ["最小阵元间距", f"{fmt(core16['dmin_mm'])} mm"],
        ["空间混叠起点", f"{fmt(core16['alias_hz'] / 1000.0)} kHz"],
        ["推荐使用频带", "6 kHz ~ 25 kHz 稳妥；25 kHz ~ 40 kHz 研究模式"],
        ["理论角分辨率", ", ".join(f"{f//1000} kHz: {fmt(angle_resolution_deg(core16['dmax_mm'], f), 1)}°" for f in (10_000, 20_000, 40_000, 60_000))],
        ["近/远场边界", ", ".join(f"{f//1000} kHz: {fmt(fraunhofer_distance_m(core16['dmax_mm'], f), 2)} m" for f in (10_000, 20_000, 40_000))],
        ["现实可检测距离", "0.1 m ~ 3 m；强高频异常源可试探到 5 m"],
        ["现实可定位距离", "0.1 m ~ 2 m"],
        ["现实可成像距离", "0.1 m ~ 1 m"],
    ]
    rows_hf = [
        ["Inner8 指标", f"孔径 {fmt(inner8['dmax_mm'])} mm，最小间距 {fmt(inner8['dmin_mm'])} mm，混叠起点 {fmt(inner8['alias_hz'] / 1000.0)} kHz"],
        ["50 kHz 以上的意义", "可以形成有意义的近场空间判别，但更像热点确认、方位估计和二次验证。"],
        ["建议距离范围", "20~40 kHz：0.1~2 m；40~80 kHz：0.1~0.8 m"],
        ["主要限制因素", "阵元间距、MEMS 带宽、空气衰减、跨芯片相位误差、外壳散射和假峰。"],
        ["改善手段", "切到 Inner8 / Core16、裁掉长基线、频带门控、做宽带标定和前壳声学设计。"],
    ]
    rows_lf = [
        ["低频现实", "0.8~2 kHz 主要是粗定位和热点增强，2~4 kHz 才逐渐进入可用成像频段。"],
        ["值得投入的增强", "多帧融合、超指向后滤波、IMU 微运动合成孔径都值得做。"],
        ["不要硬承诺", "低频高清热图、百米级低频高分辨成像。"],
    ]
    rows_conclusion = [
        ["可以承诺", "0.3~8 m 的工业级检测/定位/成像主能力，0.3~20 m 的强异常检测能力。"],
        ["可以研究但不应轻承诺", "20~40 kHz 高频精扫、40~80 kHz 近场热点确认、20~50 m 强异常检测。"],
        ["物理上基本不现实", "在 100 mm 级 32 路手持阵列上承诺 50~80 kHz 远距离高质量热图。"],
    ]
    return {
        "main32": html_table(("指标", "结论"), rows_32),
        "core16": html_table(("指标", "结论"), rows_core),
        "hf": html_table(("项目", "结论"), rows_hf),
        "lf": html_table(("项目", "结论"), rows_lf),
        "conclusion": html_table(("层级", "结论"), rows_conclusion),
    }


def coordinate_table_static(scheme: Scheme) -> str:
    rows = []
    for mic in scheme.mics:
        pkg_x, pkg_y = package_center(mic)
        rows.append(
            [
                mic.mic_id,
                layer_of(mic.mic_id),
                fmt(mic.acoustic_x_mm),
                fmt(mic.acoustic_y_mm),
                fmt(mic.default_rotation_deg, 1),
                fmt(pkg_x),
                fmt(pkg_y),
                mic.chip,
                chip_channel_name(mic),
            ]
        )
    return html_table(("Mic", "层级", "声孔 X", "声孔 Y", "默认角度 °", "封装中心 X", "封装中心 Y", "芯片", "通道"), rows)


def chip_mapping_table() -> str:
    rows = []
    for chip in ("U1", "U2", "U3", "U4"):
        chip_mics = sorted((m for m in MAIN_SCHEME.mics if m.chip == chip), key=lambda item: item.chip_slot)
        for pair in range(1, 5):
            pair_mics = [mic for mic in chip_mics if pdm_pair_index(mic) == pair]
            left = next(mic for mic in pair_mics if pdm_edge_label(mic) == "L")
            right = next(mic for mic in pair_mics if pdm_edge_label(mic) == "R")
            rows.append(
                [
                    chip,
                    CHIP_I2C_ADDR[chip],
                    f"Pair {pair}",
                    pdm_data_pin_name(left),
                    pdm_clock_pin_name(left),
                    board_pdm_data_net(left),
                    board_pdm_clock_net(left),
                    f"{left.mic_id} / {right.mic_id}",
                    f"{left.tdm_slot_48k}-{right.tdm_slot_48k}",
                    "关" if left.tdm_slot_192k_core is None else f"{left.tdm_slot_192k_core}-{right.tdm_slot_192k_core}",
                ]
            )
    return html_table(("芯片", "I2C", "PDM 对", "PCMD 数据脚", "PCMD 时钟脚", "推荐数据网名", "推荐时钟网名", "对应 mic", "48k slot", "192k core slot"), rows)


def mic_connection_table(scheme: Scheme) -> str:
    rows = []
    for mic in scheme.mics:
        rows.append(
            [
                mic.mic_id,
                layer_of(mic.mic_id),
                mic.chip,
                CHIP_I2C_ADDR[mic.chip],
                chip_channel_name(mic),
                f"Pair {pdm_pair_index(mic)} / {pdm_edge_label(mic)}",
                pdm_data_pin_name(mic),
                pdm_clock_pin_name(mic),
                board_pdm_data_net(mic),
                board_pdm_clock_net(mic),
                f"{tdm_bus_name(mic.bus)} / slot {mic.tdm_slot_48k}",
                "关" if mic.tdm_slot_192k_core is None else f"{tdm_bus_name(mic.bus)} / slot {mic.tdm_slot_192k_core}",
            ]
        )
    return html_table(("Mic", "层级", "芯片", "I2C", "PCMD 通道", "PDM 对 / LR", "数据脚", "时钟脚", "板级数据网", "板级时钟网", "48k", "192k core"), rows)


def mode_table() -> str:
    rows = []
    for mode in MODES:
        rows.append(
            [
                mode.name,
                ", ".join(mode.mic_ids),
                mode.chips,
                f"{mode.band_low_hz/1000:.1f} ~ {mode.band_high_hz/1000:.1f} kHz",
                mode.algorithm,
                mode.advantages,
                mode.limitations,
                mode.suited_for,
            ]
        )
    return html_table(("模式", "使用 mic", "使用芯片", "推荐频带", "推荐算法", "主要优势", "主要短板", "更适合"), rows)


def mode_budget(mode: Mode) -> dict[str, float]:
    capture_channels = 32 if mode.sample_rate == 48_000 else 16
    logical_channels = len(mode.mic_ids)
    active_buses = 2
    slots_per_bus = 16 if mode.sample_rate == 48_000 else 8
    bits_per_slot = 16
    bin_count = mode.nfft // 2 + 1
    coarse_points = mode.coarse_grid * mode.coarse_grid
    fine_points = mode.fine_top_k * mode.fine_grid * mode.fine_grid
    total_search_points = coarse_points + fine_points
    fps = {"实时": 20.0, "低帧率高质量": 8.0, "研究模式": 4.0}[mode.cadence_class]
    return {
        "logical_channels": logical_channels,
        "capture_channels": capture_channels,
        "bclk_mhz": mode.sample_rate * slots_per_bus * bits_per_slot / 1e6,
        "bin_count": bin_count,
        "raw_pcm_mb_s": capture_channels * mode.sample_rate * 2 / (1024 * 1024),
        "coarse_tdoa_kb": mode.pair_budget * coarse_points * 2 / 1024,
        "full_steer_mb": logical_channels * bin_count * total_search_points * 8 / (1024 * 1024),
        "algo_fps": fps,
        "search_macc": mode.pair_budget * bin_count * total_search_points * fps / 1e6,
        "active_buses": active_buses,
    }


def mode_budget_table() -> str:
    rows = []
    for mode in MODES:
        budget = mode_budget(mode)
        rows.append(
            [
                mode.name,
                f"{budget['logical_channels']} / {budget['capture_channels']}",
                f"{mode.sample_rate/1000:.0f} kHz / {mode.nfft}",
                str(mode.pair_budget),
                f"{mode.coarse_grid}x{mode.coarse_grid} + {mode.fine_top_k}x{mode.fine_grid}x{mode.fine_grid}",
                str(int(budget["bin_count"])),
                fmt(budget["raw_pcm_mb_s"]),
                f"{fmt(budget['bclk_mhz'])} MHz x {int(budget['active_buses'])}",
                f"{fmt(budget['coarse_tdoa_kb'])} KB",
                f"{fmt(budget['full_steer_mb'])} MB",
                f"{fmt(budget['algo_fps'])} fps",
                f"{fmt(budget['search_macc'])} Macc/s",
            ]
        )
    return html_table(("模式", "逻辑/采集通道", "采样/FFT", "Pair", "搜索网格", "频点数", "原始 PCM 带宽", "单总线 BCLK", "粗搜 TDOA LUT", "若预存全 steering", "建议算法帧率", "搜索乘加量级"), rows)


def short_notes_html() -> str:
    return """
<div class="grid2">
  <div class="card small">
    <h3>给硬件团队看的简版说明</h3>
    <ul>
      <li>32@48k：4 片 PCMD3180 全开，每片 8 mic，两条同步 TDM16。</li>
      <li>16@192k：4 片也都工作，但每片只开 4 个核心 mic，建议双总线 TDM8。</li>
      <li>每片按 4 对 PDM 立体声线来布：DAT1/CLK1，DAT2/CLK2，DAT3/CLK3，DAT4/CLK4。</li>
      <li>文档里的性能评估按收音孔坐标算，EDA 放置按封装中心坐标下板。</li>
    </ul>
  </div>
  <div class="card small">
    <h3>给算法团队看的简版说明</h3>
    <ul>
      <li>主流程仍是 SRP-PHAT，但 pair set 必须模式化，不能默认 32 路全对穷举。</li>
      <li>32@48k 是主展示模式；16@192k 是高频近场高质量模式，不是默认主画面。</li>
      <li>40~80 kHz 只作为近场热点确认，不要讲成远距离全场高质量成像。</li>
      <li>MVDR/MUSIC 更适合 ROI 精修，不适合替代主流程。</li>
    </ul>
  </div>
</div>
"""


def build_plot_js() -> str:
    return """(function () {
  function computePackageCenter(mic, options) {
    const offsetX = options && Number.isFinite(options.offsetX) ? options.offsetX : 0.68;
    const offsetY = options && Number.isFinite(options.offsetY) ? options.offsetY : 0.0;
    const globalDelta = options && Number.isFinite(options.globalDelta) ? options.globalDelta : 0.0;
    const rotation = (options && options.rotationByMic && Number.isFinite(options.rotationByMic[mic.id]))
      ? options.rotationByMic[mic.id]
      : (mic.rotation_deg || 0);
    const theta = (rotation + globalDelta) * Math.PI / 180;
    const rotX = offsetX * Math.cos(theta) - offsetY * Math.sin(theta);
    const rotY = offsetX * Math.sin(theta) + offsetY * Math.cos(theta);
    return {
      x_mm: mic.acoustic_x_mm - rotX,
      y_mm: mic.acoustic_y_mm - rotY,
      rotation_deg: rotation + globalDelta
    };
  }

  function renderArray(canvasOrId, scheme, options) {
    const canvas = typeof canvasOrId === "string" ? document.getElementById(canvasOrId) : canvasOrId;
    if (!canvas || !scheme) return;
    const ctx = canvas.getContext("2d");
    const boardHalf = scheme.board_half_mm || 50;
    const keepoutHalf = scheme.keepout_half_mm || 14;
    const width = canvas.width;
    const height = canvas.height;
    const scale = Math.min(width, height) / (2 * boardHalf + 12);
    const px = (x) => width / 2 + x * scale;
    const py = (y) => height / 2 - y * scale;
    const viewMode = (options && options.viewMode) || "package";

    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#fbfdff";
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = "#d1d9e6";
    ctx.lineWidth = 1;
    ctx.strokeRect(px(-boardHalf), py(boardHalf), 2 * boardHalf * scale, 2 * boardHalf * scale);
    ctx.strokeStyle = "#dc2626";
    ctx.strokeRect(px(-keepoutHalf), py(keepoutHalf), 2 * keepoutHalf * scale, 2 * keepoutHalf * scale);

    scheme.mics.forEach((mic) => {
      const acousticX = mic.acoustic_x_mm;
      const acousticY = mic.acoustic_y_mm;
      const pkg = computePackageCenter(mic, options || {});
      const color = mic.layer === "Outer16" ? "#2563eb" : (mic.layer === "Transition8" ? "#f59e0b" : "#0f766e");

      if (viewMode === "package") {
        ctx.strokeStyle = "#94a3b8";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(px(pkg.x_mm), py(pkg.y_mm));
        ctx.lineTo(px(acousticX), py(acousticY));
        ctx.stroke();

        ctx.fillStyle = "#ffffff";
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.6;
        ctx.fillRect(px(pkg.x_mm) - 4, py(pkg.y_mm) - 4, 8, 8);
        ctx.strokeRect(px(pkg.x_mm) - 4, py(pkg.y_mm) - 4, 8, 8);

        ctx.beginPath();
        ctx.fillStyle = color;
        ctx.arc(px(acousticX), py(acousticY), 2.5, 0, Math.PI * 2);
        ctx.fill();
      } else {
        ctx.beginPath();
        ctx.fillStyle = color;
        ctx.strokeStyle = "#0f172a";
        ctx.lineWidth = 1;
        ctx.arc(px(acousticX), py(acousticY), mic.core16 ? 5.2 : 4.8, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
      }

      ctx.font = "10px 'Segoe UI', sans-serif";
      ctx.fillStyle = "#0f172a";
      const labelX = viewMode === "package" ? pkg.x_mm : acousticX;
      const labelY = viewMode === "package" ? pkg.y_mm : acousticY;
      ctx.fillText(mic.id, px(labelX) + 6, py(labelY) - 6);
    });

    ctx.font = "bold 13px 'Segoe UI', sans-serif";
    ctx.fillStyle = "#0f172a";
    ctx.fillText((options && options.title) || scheme.title || "", 12, 18);
  }

  if (typeof window !== "undefined") {
    window.NECCSArrayPlot = { computePackageCenter, renderArray };
  }
})();
"""


def build_interactive_script(design_data_json: str) -> str:
    return (
        """
<script>
const DESIGN_DATA = __DESIGN_DATA_JSON__;

function presetRotation(mic, preset) {
  const outward = mic.outward_rotation_deg;
  if (preset === "outward") return outward;
  if (preset === "inward") return outward + 180;
  if (preset === "tangent_cw") return outward - 90;
  if (preset === "tangent_ccw") return outward + 90;
  if (preset === "fixed_0") return 0;
  return mic.default_rotation_deg;
}

function buildState() {
  const main = DESIGN_DATA.schemes.main;
  const rotationByMic = {};
  main.mics.forEach((mic) => {
    rotationByMic[mic.id] = mic.default_rotation_deg;
  });
  return {
    offsetX: DESIGN_DATA.footprint.port_offset_x_mm,
    offsetY: DESIGN_DATA.footprint.port_offset_y_mm,
    globalDelta: 0,
    preset: "default",
    viewMode: "package",
    rotationByMic
  };
}

const state = buildState();

function computePackageCenter(mic) {
  return window.NECCSArrayPlot.computePackageCenter(mic, {
    offsetX: state.offsetX,
    offsetY: state.offsetY,
    globalDelta: state.globalDelta,
    rotationByMic: state.rotationByMic
  });
}

function micRowHtml(mic) {
  const pkg = computePackageCenter(mic);
  const slot192 = mic.tdm_slot_192k_core === null ? "关" : `TDM_${mic.bus} / ${mic.tdm_slot_192k_core}`;
  return `
    <tr>
      <td>${mic.id}</td>
      <td>${mic.layer}</td>
      <td>${mic.acoustic_x_mm.toFixed(2)}</td>
      <td>${mic.acoustic_y_mm.toFixed(2)}</td>
      <td><input type="number" class="rot-input" data-mic="${mic.id}" step="0.1" value="${state.rotationByMic[mic.id].toFixed(1)}"></td>
      <td>${pkg.x_mm.toFixed(2)}</td>
      <td>${pkg.y_mm.toFixed(2)}</td>
      <td>${mic.chip}</td>
      <td>${mic.channel}</td>
      <td>${mic.pdm_pair} / ${mic.pdm_edge}</td>
      <td>TDM_${mic.bus} / ${mic.tdm_slot_48k}</td>
      <td>${slot192}</td>
    </tr>`;
}

function renderCoordinateTables() {
  const main = DESIGN_DATA.schemes.main;
  const coordBody = document.getElementById("coord-body");
  const coreBody = document.getElementById("core16-body");
  coordBody.innerHTML = main.mics.map(micRowHtml).join("");
  coreBody.innerHTML = main.mics.filter((mic) => mic.core16).map(micRowHtml).join("");
  coordBody.querySelectorAll(".rot-input").forEach((input) => {
    input.addEventListener("input", () => {
      state.rotationByMic[input.dataset.mic] = Number(input.value);
      state.preset = "custom";
      const preset = document.getElementById("preset-select");
      if (preset) preset.value = "custom";
      refresh();
    });
  });
  coreBody.querySelectorAll(".rot-input").forEach((input) => {
    input.addEventListener("input", () => {
      state.rotationByMic[input.dataset.mic] = Number(input.value);
      state.preset = "custom";
      const preset = document.getElementById("preset-select");
      if (preset) preset.value = "custom";
      refresh();
    });
  });
}

function renderPlots() {
  if (!window.NECCSArrayPlot) return;
  const commonOptions = {
    offsetX: state.offsetX,
    offsetY: state.offsetY,
    globalDelta: state.globalDelta,
    rotationByMic: state.rotationByMic,
    viewMode: state.viewMode
  };
  window.NECCSArrayPlot.renderArray("plot-main", DESIGN_DATA.schemes.main, { ...commonOptions, title: "主方案（可切换声孔/封装中心）" });
  window.NECCSArrayPlot.renderArray("plot-alt-a", DESIGN_DATA.schemes.alt_double_ring, { ...commonOptions, title: "备选 A：非均匀双环" });
  window.NECCSArrayPlot.renderArray("plot-alt-b", DESIGN_DATA.schemes.alt_sparse_plane, { ...commonOptions, title: "备选 B：边缘加权稀疏平面" });
}

function exportCurrentCsv() {
  const header = [
    "mic_id","layer","acoustic_x_mm","acoustic_y_mm","rotation_deg",
    "package_center_x_mm","package_center_y_mm","chip","channel",
    "pdm_pair","pdm_edge","tdm_bus_48k","tdm_slot_48k","tdm_bus_192k_core","tdm_slot_192k_core"
  ];
  const rows = [header.join(",")];
  DESIGN_DATA.schemes.main.mics.forEach((mic) => {
    const pkg = computePackageCenter(mic);
    rows.push([
      mic.id,
      mic.layer,
      mic.acoustic_x_mm.toFixed(2),
      mic.acoustic_y_mm.toFixed(2),
      state.rotationByMic[mic.id].toFixed(1),
      pkg.x_mm.toFixed(2),
      pkg.y_mm.toFixed(2),
      mic.chip,
      mic.channel,
      mic.pdm_pair,
      mic.pdm_edge,
      `TDM_${mic.bus}`,
      mic.tdm_slot_48k,
      mic.tdm_slot_192k_core === null ? "" : `TDM_${mic.bus}`,
      mic.tdm_slot_192k_core === null ? "" : mic.tdm_slot_192k_core
    ].join(","));
  });
  const blob = new Blob([rows.join("\\n")], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "array_32ch_coords_live.csv";
  link.click();
  URL.revokeObjectURL(url);
}

function syncControlValues() {
  document.getElementById("offset-x").value = state.offsetX;
  document.getElementById("offset-y").value = state.offsetY;
  document.getElementById("global-delta").value = state.globalDelta;
  document.getElementById("view-mode").value = state.viewMode;
  document.getElementById("preset-select").value = state.preset;
}

function applyPreset(preset) {
  state.preset = preset;
  DESIGN_DATA.schemes.main.mics.forEach((mic) => {
    state.rotationByMic[mic.id] = presetRotation(mic, preset);
  });
}

function refresh() {
  syncControlValues();
  renderCoordinateTables();
  renderPlots();
}

document.addEventListener("DOMContentLoaded", () => {
  document.getElementById("offset-x").addEventListener("input", (event) => {
    state.offsetX = Number(event.target.value);
    refresh();
  });
  document.getElementById("offset-y").addEventListener("input", (event) => {
    state.offsetY = Number(event.target.value);
    refresh();
  });
  document.getElementById("global-delta").addEventListener("input", (event) => {
    state.globalDelta = Number(event.target.value);
    refresh();
  });
  document.getElementById("view-mode").addEventListener("change", (event) => {
    state.viewMode = event.target.value;
    refresh();
  });
  document.getElementById("preset-select").addEventListener("change", (event) => {
    const preset = event.target.value;
    if (preset !== "custom") {
      applyPreset(preset);
      refresh();
    } else {
      state.preset = "custom";
      syncControlValues();
    }
  });
  document.getElementById("reset-controls").addEventListener("click", () => {
    Object.assign(state, buildState());
    refresh();
  });
  document.getElementById("export-csv").addEventListener("click", exportCurrentCsv);
  refresh();
});
</script>
"""
    ).replace("__DESIGN_DATA_JSON__", design_data_json)


def build_design_data() -> dict[str, object]:
    def scheme_payload(scheme: Scheme) -> dict[str, object]:
        mics = []
        for mic in scheme.mics:
            pkg_x, pkg_y = package_center(mic)
            mics.append(
                {
                    "id": mic.mic_id,
                    "layer": layer_of(mic.mic_id),
                    "acoustic_x_mm": mic.acoustic_x_mm,
                    "acoustic_y_mm": mic.acoustic_y_mm,
                    "package_x_mm": pkg_x,
                    "package_y_mm": pkg_y,
                    "default_rotation_deg": mic.default_rotation_deg,
                    "outward_rotation_deg": outward_rotation_deg(mic.acoustic_x_mm, mic.acoustic_y_mm),
                    "rotation_deg": mic.default_rotation_deg,
                    "bus": mic.bus,
                    "chip": mic.chip,
                    "channel": chip_channel_name(mic),
                    "tdm_slot_48k": mic.tdm_slot_48k,
                    "tdm_slot_192k_core": mic.tdm_slot_192k_core,
                    "core16": mic.is_core16,
                    "pdm_pair": pdm_pair_index(mic),
                    "pdm_edge": pdm_edge_label(mic),
                }
            )
        return {
            "title": scheme.title,
            "board_half_mm": BOARD_HALF_MM,
            "keepout_half_mm": KEEPOUT_HALF_MM,
            "mics": mics,
        }

    return {
        "generated_on": TODAY,
        "footprint": {
            "port_offset_x_mm": DEFAULT_PORT_OFFSET_X_MM,
            "port_offset_y_mm": DEFAULT_PORT_OFFSET_Y_MM,
            "local_zero_definition": "0° 时收音孔位于封装中心右侧，角度按逆时针为正。",
            "formula": "package_center = acoustic_port - R(theta) * [offset_x, offset_y]",
        },
        "schemes": {scheme.key: scheme_payload(scheme) for scheme in SCHEMES},
    }


def build_html() -> str:
    baseline = current_baseline_metrics()
    main32 = geometry_summary(MAIN_SCHEME.mics)
    perf = performance_tables()
    design_data_json = json.dumps(build_design_data(), ensure_ascii=False)
    interactive_script = build_interactive_script(design_data_json)
    html = Template(
        """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NECCS 32 路工业化声学阵列方案</title>
  <style>
    body { margin: 0; background: #f3f6fb; color: #172033; font: 15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif; }
    .wrap { max-width: 1320px; margin: 0 auto; padding: 28px; }
    .hero { background: linear-gradient(135deg,#0f766e,#0b3a57 55%,#111827); color: #fff; border-radius: 20px; padding: 28px 32px; }
    .hero h1 { margin: 0 0 8px; font-size: 32px; }
    .hero p { margin: 0; }
    .tags span { display: inline-block; margin: 6px 8px 0 0; padding: 4px 10px; border-radius: 999px; border: 1px solid rgba(255,255,255,.2); font-size: 12px; }
    h2 { margin: 28px 0 12px; }
    h3 { margin: 0 0 8px; }
    .card { background: #fff; border: 1px solid #d9e0ea; border-radius: 16px; padding: 18px 20px; box-shadow: 0 10px 28px rgba(15,23,42,.06); }
    .small h3 { margin-top: 0; }
    .grid2 { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 16px; }
    .grid3 { display: grid; grid-template-columns: repeat(3,minmax(0,1fr)); gap: 16px; }
    .controls { display: grid; grid-template-columns: repeat(6,minmax(0,1fr)); gap: 12px; align-items: end; }
    .controls label { display: block; font-size: 12px; color: #475569; margin-bottom: 6px; }
    .controls input, .controls select, .controls button { width: 100%; box-sizing: border-box; padding: 9px 10px; border: 1px solid #cbd5e1; border-radius: 10px; background: #fff; font: inherit; }
    .controls button { background: #0f766e; color: #fff; border-color: #0f766e; cursor: pointer; }
    .controls button.secondary { background: #fff; color: #0f172a; }
    .hint { margin-top: 10px; color: #475569; font-size: 13px; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 13px; }
    th, td { border: 1px solid #d9e0ea; padding: 8px 10px; vertical-align: top; }
    th { background: #eff6ff; }
    td input[type=number] { width: 84px; padding: 4px 6px; border: 1px solid #cbd5e1; border-radius: 6px; }
    canvas { width: 100%; max-width: 100%; background: #fff; border: 1px solid #d9e0ea; border-radius: 14px; }
    ul { margin: 8px 0 0 18px; padding: 0; }
    code { background: #e2e8f0; padding: 1px 4px; border-radius: 4px; }
    @media (max-width: 1100px) { .grid2, .grid3, .controls { grid-template-columns: 1fr; } }
  </style>
  <script src="./plot_array_32ch.js"></script>
</head>
<body>
  <div class="wrap">
    <section class="hero">
      <h1>NECCS 32 路工业化声学阵列方案</h1>
      <p>生成时间：$today。当前版本按 <strong>4 × PCMD3180</strong>、<strong>32@48k 双 TDM16</strong>、<strong>16@192k 双 TDM8</strong> 输出，并新增“收音孔坐标 → 封装中心坐标”的动态转换。</p>
      <div class="tags">
        <span>32 物理麦</span><span>4 × PCMD3180</span><span>Inner8 + Transition8 + Outer16</span>
        <span>声孔坐标 / 封装中心坐标双定义</span><span>按 mic 粒度给出 PDM/TDM 连接</span>
      </div>
    </section>

    <h2>执行摘要</h2>
    <div class="grid2">
      <div class="card">
        <p>主方案继续采用工业化三尺度结构，但文档坐标体系升级成双定义：<strong>声学性能评估仍按收音孔坐标</strong>，<strong>EDA 下板按封装中心坐标</strong>。本页新增可调的封装角度、局部偏移和实时坐标表，硬件团队可以直接拿它推封装摆位。</p>
        <p>与当前 16 路基线相比，主阵列的最大孔径从 <strong>$baseline_dmax mm</strong> 提升到 <strong>$main_dmax mm</strong>，最小间距压到 <strong>$main_dmin mm</strong>，空间混叠起点提升到 <strong>$main_alias kHz</strong>。</p>
      </div>
      <div class="card">
        <ul>
          <li>可承诺：0.3~8 m 的工业级检测 / 定位 / 成像主能力。</li>
          <li>可研究：20~40 kHz 高频精扫，40~80 kHz 近场热点确认。</li>
          <li>不应承诺：50~80 kHz 远距离高质量热图。</li>
          <li>16@192k 的前提：4 个 PCMD3180 都工作，每片只开 4 个核心 mic。</li>
        </ul>
      </div>
    </div>

    <h2>现有工程约束复述</h2>
    <div class="card">
      <ul>
        <li>当前平台：STM32H743，后续目标迁移 STM32N6。</li>
        <li>当前链路：PDM mic → PCMD3180 → TDM/SAI → DMA → FFT → GCC/SRP-PHAT → 粗细搜索 → 热力图。</li>
        <li>当前真实基线：最大孔径 $baseline_dmax mm，最小间距 $baseline_dmin mm，混叠起点 $baseline_alias kHz。</li>
        <li>延续工程风格：坐标驱动 + LUT 驱动 + Pair set + coarse-to-fine。</li>
      </ul>
    </div>

    <h2>新阵列设计目标</h2>
    <div class="grid3">
      <div class="card"><strong>物理约束</strong><br>100 mm × 100 mm，中心 28 mm × 28 mm keepout，二维平面阵列。</div>
      <div class="card"><strong>工作模式</strong><br>32@48k 为主显示模式，16@192k 为近场高频高质量模式。</div>
      <div class="card"><strong>工程目标</strong><br>强调工业异常检测、比赛可答辩性和 STM32N6 上的落地可信度。</div>
    </div>

    <h2>坐标定义更新：收音孔坐标 → 封装中心坐标</h2>
    <div class="grid2">
      <div class="card">
        <p><strong>默认封装定义：</strong>根据你给的封装图，默认认为收音孔中心在封装中心的局部坐标 <code>(+0.68 mm, 0.00 mm)</code> 处；<code>0°</code> 时收音孔位于封装中心右侧，角度按逆时针为正。</p>
        <p><strong>换算公式：</strong><code>package_center = acoustic_port - R(theta) · [offset_x, offset_y]</code></p>
        <ul>
          <li>性能评估、孔径、混叠起点等仍按收音孔坐标计算。</li>
          <li>EDA 下板、丝印定位、封装放置改按封装中心坐标。</li>
          <li>如果后续封装原点定义有变化，只需改偏移或角度，不用重做阵列设计。</li>
        </ul>
      </div>
      <div class="card">
        <div class="controls">
          <div><label for="offset-x">局部偏移 X(mm)</label><input id="offset-x" type="number" step="0.01"></div>
          <div><label for="offset-y">局部偏移 Y(mm)</label><input id="offset-y" type="number" step="0.01"></div>
          <div><label for="global-delta">全局角度修正(°)</label><input id="global-delta" type="number" step="0.1"></div>
          <div><label for="preset-select">角度预设</label>
            <select id="preset-select">
              <option value="default">默认外向</option>
              <option value="outward">全部外向</option>
              <option value="inward">全部内向</option>
              <option value="tangent_cw">切向顺时针</option>
              <option value="tangent_ccw">切向逆时针</option>
              <option value="fixed_0">全部 0°</option>
              <option value="custom">自定义</option>
            </select>
          </div>
          <div><label for="view-mode">绘图视角</label>
            <select id="view-mode">
              <option value="package">封装中心视角</option>
              <option value="acoustic">收音孔视角</option>
            </select>
          </div>
          <div><label>&nbsp;</label><button id="reset-controls" class="secondary" type="button">恢复默认</button></div>
        </div>
        <div class="controls" style="margin-top:12px; grid-template-columns: repeat(6,minmax(0,1fr));">
          <div style="grid-column: span 2;"><label>&nbsp;</label><button id="export-csv" type="button">导出当前封装中心 CSV</button></div>
        </div>
        <div class="hint">提示：主表里的每个 mic 角度都可以单独改，表格和图会同步刷新。</div>
      </div>
    </div>
    <div class="grid3">
      <canvas id="plot-main" width="360" height="360"></canvas>
      <canvas id="plot-alt-a" width="360" height="360"></canvas>
      <canvas id="plot-alt-b" width="360" height="360"></canvas>
    </div>

    <h2>推荐主方案</h2>
    <div class="card">
      <p><strong>主方案：</strong>$main_title</p>
      <p><strong>设计逻辑：</strong>$main_rationale</p>
      <p><strong>优势：</strong>$main_strengths</p>
      <p><strong>代价：</strong>$main_tradeoffs</p>
      <p><strong>布线建议：</strong>U1/U2 共用 <code>TDM_A_BCLK / FSYNC / SDOUT</code>，U3/U4 共用 <code>TDM_B_BCLK / FSYNC / SDOUT</code>。每片按 4 对 PDM 线布线，每对由一个 <code>PDMDINx_GPIx</code> 和一个 <code>PDMCLKx_GPOx</code> 组成，两只 mic 通过 PDM 的 L/R 选边共享这对线。</p>
    </div>

    <h2>备选方案对比</h2>
    <div class="card">$comparison_table$scheme_metrics_table</div>

    <h2>32 路坐标表（主方案）</h2>
    <div class="card">
      <p>下面这张主表会随上面的偏移 / 角度实时刷新。</p>
      <table>
        <thead>
          <tr>
            <th>Mic</th><th>层级</th><th>声孔 X</th><th>声孔 Y</th><th>封装角度 °</th>
            <th>封装中心 X</th><th>封装中心 Y</th><th>芯片</th><th>通道</th><th>PDM 对 / LR</th><th>48k</th><th>192k core</th>
          </tr>
        </thead>
        <tbody id="coord-body"></tbody>
      </table>
    </div>

    <h2>16 路核心子阵表</h2>
    <div class="card">
      <table>
        <thead>
          <tr>
            <th>Mic</th><th>层级</th><th>声孔 X</th><th>声孔 Y</th><th>封装角度 °</th>
            <th>封装中心 X</th><th>封装中心 Y</th><th>芯片</th><th>通道</th><th>PDM 对 / LR</th><th>48k</th><th>192k core</th>
          </tr>
        </thead>
        <tbody id="core16-body"></tbody>
      </table>
    </div>

    <h2>PCMD3180 分组映射表</h2>
    <div class="card">
      <p><strong>依据：</strong>PCMD3180 数据手册给出的 4 组 PDM 数据 / 时钟引脚分别为 <code>PDMDIN1_GPI1 ~ PDMDIN4_GPI4</code> 和 <code>PDMCLK1_GPO1 ~ PDMCLK4_GPO4</code>。本方案按每组 1 对 PDM 立体声线服务 2 个 mic 的方式组织，每片共 8 mic。</p>
      $chip_mapping_table
    </div>

    <h2>PCMD3180 与麦克风的具体连接方式</h2>
    <div class="card">
      <p>这张表按每个 mic 展开，已经具体到所属芯片、PCMD 通道、PDM 数据线 / 时钟线编号，以及 48k / 192k 的 TDM slot。</p>
      $mic_connection_table
    </div>

    <h2>多模式使用策略</h2>
    <div class="card">$mode_table</div>

    <h2>嵌入式实现建议（面向 STM32N6）</h2>
    <div class="card">
      <ul>
        <li>32@48k：4 片全开，双 TDM16，单总线 BCLK 与现有 16@48k 同量级，适合作为默认实时模式。</li>
        <li>16@192k：4 片都参与，但每片只开 4 个核心 mic；把每条总线压成 TDM8 后，单总线 BCLK 约 24.576 MHz。</li>
        <li>应预计算：pair_idx、pair_dx/dy、coarse TDOA LUT、频带权重、增益 / 相位 / 分数延迟标定表。</li>
        <li>不建议预存全 steering 立方体；fine search 继续在线算，保留 coarse-to-fine 的嵌入式风格。</li>
        <li>192k 模式更适合触发式、ROI 式、低帧率高质量运行，而不是默认常开。</li>
      </ul>
      $mode_budget_table
    </div>

    <h2>新阵列性能评估</h2>
    <div class="card"><h3>5.1 对 32 路 @ 48 kHz 模式</h3>$perf_main32</div>
    <div class="card"><h3>5.2 对 16 路 @ 192 kHz 模式</h3>$perf_core16</div>
    <div class="card"><h3>5.3 高频 / 超声潜力评估</h3>$perf_hf</div>
    <div class="card"><h3>5.4 低频能力评估</h3>$perf_lf</div>
    <div class="card"><h3>5.5 结论必须明确</h3>$perf_conclusion</div>

    <h2>风险点与工程注意事项</h2>
    <div class="card">
      <ul>
        <li>192k 模式的第一瓶颈是前端时钟和跨芯片一致性，不是 MCU 算力。</li>
        <li>跨芯片标定必须流程化，至少覆盖增益、整采样延迟、分数延迟和宽带相位斜率。</li>
        <li>高频能力高度依赖前壳开孔、倒角、吸声、支撑柱和相机周边散射控制。</li>
        <li>如果后续麦克风封装方向为了布线要做 90° / 180° 调整，可以直接用本页动态表先算封装中心坐标再落板。</li>
      </ul>
    </div>

    <h2>最终建议：哪些值得冲，哪些不要硬冲</h2>
    <div class="grid2">
      <div class="card small">
        <h3>值得冲</h3>
        <ul>
          <li>32@48k 主模式下的工业级高质量热图。</li>
          <li>16@192k 的近场高频精扫和热点确认。</li>
          <li>模式化 Pair set、频带门控、多帧融合和 IMU 微运动补偿。</li>
        </ul>
      </div>
      <div class="card small">
        <h3>不要硬冲</h3>
        <ul>
          <li>50~80 kHz 的远距离高质量全场热图。</li>
          <li>100 mm 手持阵列上的百米级低频高分辨成像。</li>
          <li>把 MUSIC / MVDR 包装成全流程主算法。</li>
        </ul>
      </div>
    </div>

    $short_notes_html

    <h2>备选方案默认坐标快照</h2>
    <div class="grid2">
      <div class="card"><h3>备选 A</h3>$alt_a_table</div>
      <div class="card"><h3>备选 B</h3>$alt_b_table</div>
    </div>
  </div>
  $interactive_script
</body>
</html>
"""
    ).substitute(
        today=escape(TODAY),
        baseline_dmax=fmt(baseline["dmax_mm"]),
        baseline_dmin=fmt(baseline["dmin_mm"]),
        baseline_alias=fmt(baseline["alias_hz"] / 1000.0),
        main_dmax=fmt(main32["dmax_mm"]),
        main_dmin=fmt(main32["dmin_mm"]),
        main_alias=fmt(main32["alias_hz"] / 1000.0),
        main_title=escape(MAIN_SCHEME.title),
        main_rationale=escape(MAIN_SCHEME.rationale),
        main_strengths=escape(MAIN_SCHEME.strengths),
        main_tradeoffs=escape(MAIN_SCHEME.tradeoffs),
        comparison_table=comparison_summary_table(),
        scheme_metrics_table=scheme_metrics_table(),
        chip_mapping_table=chip_mapping_table(),
        mic_connection_table=mic_connection_table(MAIN_SCHEME),
        mode_table=mode_table(),
        mode_budget_table=mode_budget_table(),
        perf_main32=perf["main32"],
        perf_core16=perf["core16"],
        perf_hf=perf["hf"],
        perf_lf=perf["lf"],
        perf_conclusion=perf["conclusion"],
        short_notes_html=short_notes_html(),
        alt_a_table=coordinate_table_static(ALT_DOUBLE_RING),
        alt_b_table=coordinate_table_static(ALT_SPARSE_PLANE),
        interactive_script=interactive_script,
    )
    return html


def write_csv() -> None:
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    headers = (
        "mic_id",
        "layer",
        "acoustic_x_mm",
        "acoustic_y_mm",
        "default_rotation_deg",
        "package_center_x_mm",
        "package_center_y_mm",
        "package_to_port_dx_local_mm",
        "package_to_port_dy_local_mm",
        "bus",
        "chip",
        "chip_i2c_addr",
        "chip_channel",
        "chip_slot",
        "pdm_pair",
        "pdm_edge",
        "pcmd_pdm_data_pin",
        "pcmd_pdm_clock_pin",
        "board_pdm_data_net",
        "board_pdm_clock_net",
        "tdm_bus_48k",
        "tdm_slot_48k",
        "tdm_bus_192k_core",
        "tdm_slot_192k_core",
        "is_core16",
    )
    with CSV_PATH.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(headers)
        for mic in MAIN_SCHEME.mics:
            pkg_x, pkg_y = package_center(mic)
            writer.writerow(
                (
                    mic.mic_id,
                    layer_of(mic.mic_id),
                    f"{mic.acoustic_x_mm:.2f}",
                    f"{mic.acoustic_y_mm:.2f}",
                    f"{mic.default_rotation_deg:.1f}",
                    f"{pkg_x:.2f}",
                    f"{pkg_y:.2f}",
                    f"{DEFAULT_PORT_OFFSET_X_MM:.2f}",
                    f"{DEFAULT_PORT_OFFSET_Y_MM:.2f}",
                    mic.bus,
                    mic.chip,
                    CHIP_I2C_ADDR[mic.chip],
                    chip_channel_name(mic),
                    mic.chip_slot,
                    pdm_pair_index(mic),
                    pdm_edge_label(mic),
                    pdm_data_pin_name(mic),
                    pdm_clock_pin_name(mic),
                    board_pdm_data_net(mic),
                    board_pdm_clock_net(mic),
                    tdm_bus_name(mic.bus),
                    mic.tdm_slot_48k,
                    "" if mic.tdm_slot_192k_core is None else tdm_bus_name(mic.bus),
                    "" if mic.tdm_slot_192k_core is None else mic.tdm_slot_192k_core,
                    "1" if mic.is_core16 else "0",
                )
            )


def write_html() -> None:
    HTML_PATH.write_text(build_html(), encoding="utf-8", newline="\n")


def write_plot_js() -> None:
    plot_js = build_plot_js()
    PLOT_JS_PATH.write_text(plot_js, encoding="utf-8", newline="\n")
    DOCS_PLOT_JS_PATH.write_text(plot_js, encoding="utf-8", newline="\n")


def main() -> None:
    write_csv()
    write_plot_js()
    write_html()
    baseline = current_baseline_metrics()
    main32 = geometry_summary(MAIN_SCHEME.mics)
    core16 = geometry_summary(tuple(m for m in MAIN_SCHEME.mics if m.is_core16))
    print("Generated:")
    print(" -", CSV_PATH)
    print(" -", HTML_PATH)
    print(" -", PLOT_JS_PATH)
    print(" -", DOCS_PLOT_JS_PATH)
    print()
    print("Current16:", f"Dmax={baseline['dmax_mm']:.2f} mm,", f"Dmin={baseline['dmin_mm']:.2f} mm,", f"f_alias={baseline['alias_hz']/1000.0:.2f} kHz")
    print("Main32:", f"Dmax={main32['dmax_mm']:.2f} mm,", f"Dmin={main32['dmin_mm']:.2f} mm,", f"f_alias={main32['alias_hz']/1000.0:.2f} kHz")
    print("Core16:", f"Dmax={core16['dmax_mm']:.2f} mm,", f"Dmin={core16['dmin_mm']:.2f} mm,", f"f_alias={core16['alias_hz']/1000.0:.2f} kHz")


if __name__ == "__main__":
    main()
