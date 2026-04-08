#!/usr/bin/env python3
"""Regenerate the 32-channel industrial handheld acoustic array deliverables."""

from __future__ import annotations

import csv
import json
import math
from dataclasses import dataclass
from datetime import date
from html import escape
from itertools import combinations
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = ROOT / "docs"
CSV_PATH = DOCS_DIR / "array_32ch_coords.csv"
HTML_PATH = DOCS_DIR / "array_32ch_design.html"
PLOT_JS_PATH = ROOT / "tools" / "plot_array_32ch.js"

BOARD_HALF_MM = 50.0
KEEPOUT_HALF_MM = 14.0
SPEED_OF_SOUND = 343.0
TODAY = date.today().isoformat()


@dataclass(frozen=True)
class Mic:
    mic_id: str
    x_mm: float
    y_mm: float
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
    algo_decim: int
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


ASSIGNMENTS = {
    "M01": ("A", "U1", 0, 0, 0, True), "M02": ("A", "U1", 1, 1, 1, True),
    "M03": ("A", "U2", 0, 8, 4, True), "M04": ("A", "U2", 1, 9, 5, True),
    "M05": ("B", "U3", 0, 0, 0, True), "M06": ("B", "U3", 1, 1, 1, True),
    "M07": ("B", "U4", 0, 8, 4, True), "M08": ("B", "U4", 1, 9, 5, True),
    "M09": ("A", "U1", 2, 2, 2, True), "M10": ("A", "U1", 3, 3, 3, True),
    "M11": ("A", "U2", 2, 10, 6, True), "M12": ("A", "U2", 3, 11, 7, True),
    "M13": ("B", "U3", 2, 2, 2, True), "M14": ("B", "U3", 3, 3, 3, True),
    "M15": ("B", "U4", 2, 10, 6, True), "M16": ("B", "U4", 3, 11, 7, True),
    "M17": ("A", "U1", 4, 4, None, False), "M18": ("A", "U1", 5, 5, None, False),
    "M19": ("A", "U1", 6, 6, None, False), "M20": ("A", "U1", 7, 7, None, False),
    "M21": ("B", "U4", 4, 12, None, False), "M22": ("B", "U4", 5, 13, None, False),
    "M23": ("B", "U4", 6, 14, None, False), "M24": ("B", "U4", 7, 15, None, False),
    "M25": ("B", "U3", 4, 4, None, False), "M26": ("B", "U3", 5, 5, None, False),
    "M27": ("B", "U3", 6, 6, None, False), "M28": ("B", "U3", 7, 7, None, False),
    "M29": ("A", "U2", 4, 12, None, False), "M30": ("A", "U2", 5, 13, None, False),
    "M31": ("A", "U2", 6, 14, None, False), "M32": ("A", "U2", 7, 15, None, False),
}


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
        mics.append(Mic(mic_id, x_mm, y_mm, bus, chip, chip_slot, slot48, slot192, is_core))
    validate_scheme(tuple(mics))
    return Scheme(key, title, short_label, rationale, strengths, tradeoffs, tuple(mics))


def validate_scheme(mics: tuple[Mic, ...]) -> None:
    if len(mics) != 32:
        raise ValueError("scheme must contain exactly 32 microphones")
    ids = {m.mic_id for m in mics}
    if ids != {f"M{i:02d}" for i in range(1, 33)}:
        raise ValueError("scheme ids must be M01..M32")
    for mic in mics:
        if abs(mic.x_mm) > BOARD_HALF_MM or abs(mic.y_mm) > BOARD_HALF_MM:
            raise ValueError(f"{mic.mic_id} outside board outline")
        if abs(mic.x_mm) < KEEPOUT_HALF_MM and abs(mic.y_mm) < KEEPOUT_HALF_MM:
            raise ValueError(f"{mic.mic_id} violates keepout")
    for chip in ("U1", "U2", "U3", "U4"):
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
    "把阵列拆成高频短基线层、宽频桥接层和大孔径成像层，同时满足 192k 下 4 片都参与、每片只开 4 个核心 mic。",
    "110 mm 级孔径 + 7.64 mm 最小间距，兼顾宽频、旁瓣控制和 192k 带宽现实。",
    "50~80 kHz 仍主要是近场热点确认；20~50 m 只能承诺强异常检测，不承诺精细热图。",
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
    "alt_double_ring", "备选 A：非均匀双环阵列", "备选 A",
    "角向覆盖整齐，解释直观，适合当对照组。", "最小间距更小，混叠起点高。", "重复基线更多，规则副瓣风险更大。",
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
    "alt_sparse_plane", "备选 B：边缘加权稀疏平面阵列", "备选 B",
    "中低频和远距离检测潜力更强。", "平面填充度更高，中低频更稳。", "Core16 抽取不如主方案优雅。",
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
HF_CLEAN_IDS = ("M01", "M02", "M03", "M04", "M05", "M06", "M07", "M08", "M09", "M11", "M13", "M15")

MODES = (
    Mode("32 路工业宽孔径主模式", 48_000, 256, 9, 5, 3, 4, 96, 1_500, 14_000,
         tuple(f"M{i:02d}" for i in range(1, 33)), "U1~U4（双 TDM16）",
         "Outer16 长中基线为主，叠加 Core16 纠错",
         "SRP-PHAT + 频带自适应加权 + Top-3 粗细搜索",
         "工业巡检主模式，兼顾检测、定位、热图", "16 kHz 以上不应承诺稳定高质量热图", "定位 + 成像", "实时"),
    Mode("16 路核心高采样模式", 192_000, 512, 11, 5, 3, 8, 48, 6_000, 40_000,
         tuple(f"M{i:02d}" for i in range(1, 17)), "U1~U4（双 TDM8，4 片都开）",
         "4 片同时参与，每片只开 4 个核心 mic",
         "SRP-PHAT + ROI 内 MVDR/MUSIC 精修",
         "适合近场高频、泄漏、局放、摩擦异常", "高质量模式，不宜默认连续运行", "检测 + 近场定位", "低帧率高质量"),
    Mode("12 路高频低旁瓣模式", 192_000, 512, 11, 5, 2, 8, 28, 12_000, 30_000,
         HF_CLEAN_IDS, "U1~U4（逻辑 12 路，采集仍为 16 路 core）",
         "只保留短基线和低重复基线组合", "SRP-PHAT + 空间加权 + 高频门控",
         "热点更干净，假峰更少", "角分辨率不如全 32 路", "成像 + 热点确认", "实时"),
    Mode("8 路超高频热点确认模式", 192_000, 512, 13, 5, 2, 10, 12, 25_000, 80_000,
         INNER8_IDS, "U1~U4（采 16 路 core，处理 Inner8）",
         "只用最短短基线，拒绝假高分辨", "GCC-PHAT + 局部 SRP/MUSIC",
         "适合 40~80 kHz 研究级近场热点确认", "不是高质量全场成像模式", "检测 + 方位估计", "研究模式"),
    Mode("中频主成像模式", 48_000, 256, 9, 5, 3, 4, 112, 2_000, 8_000,
         tuple(f"M{i:02d}" for i in range(1, 33)), "U1~U4",
         "中长基线偏置", "SRP-PHAT + 多帧融合",
         "最稳妥的比赛主画面频段", "对超高频异常不敏感", "成像", "实时"),
    Mode("低频增强模式", 48_000, 256, 9, 5, 3, 6, 120, 800, 4_000,
         tuple(f"M{i:02d}" for i in range(1, 33)), "U1~U4",
         "长中基线全开，多帧补偿", "SRP-PHAT + 超指向后滤波 + IMU 微运动补偿",
         "尽量榨干 100 mm 阵列的低频能力", "低频仍以粗定位和热点增强为主", "检测 + 粗定位", "低帧率高质量"),
    Mode("远距离强源检测模式", 48_000, 256, 9, 5, 2, 5, 56, 2_000, 10_000,
         tuple(f"M{i:02d}" for i in range(1, 33)), "U1~U4",
         "Outer16 最长基线优先，Core16 只作置信度校验", "SRP-PHAT + 长基线稀疏配对",
         "对中远距离强异常源更敏感", "热图更尖锐、更挑环境", "检测 + 定位", "实时"),
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


def pairwise_distances(mics: tuple[Mic, ...]) -> list[tuple[float, str, str]]:
    distances: list[tuple[float, str, str]] = []
    for left, right in combinations(mics, 2):
        distances.append((math.hypot(left.x_mm - right.x_mm, left.y_mm - right.y_mm), left.mic_id, right.mic_id))
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
    for dist_mm, _, _ in pairwise_distances(mics):
        bucket = round(dist_mm / bin_mm) * bin_mm
        buckets[bucket] = buckets.get(bucket, 0) + 1
    duplicate_count = sum(count - 1 for count in buckets.values() if count > 1)
    total = len(mics) * (len(mics) - 1) // 2
    return 100.0 * duplicate_count / max(1, total)


def angle_resolution_deg(diameter_mm: float, freq_hz: int) -> float:
    return math.degrees(SPEED_OF_SOUND / ((diameter_mm / 1000.0) * freq_hz))


def fraunhofer_distance_m(diameter_mm: float, freq_hz: int) -> float:
    d_m = diameter_mm / 1000.0
    return 2.0 * d_m * d_m * freq_hz / SPEED_OF_SOUND


def current_baseline_metrics() -> dict[str, float]:
    distances = [math.hypot(x1 - x2, y1 - y2) for (x1, y1), (x2, y2) in combinations(CURRENT_BASELINE_16_MM, 2)]
    dmin = min(distances)
    dmax = max(distances)
    return {"dmin_mm": dmin, "dmax_mm": dmax, "alias_hz": SPEED_OF_SOUND / (2.0 * (dmin / 1000.0))}


def fmt(value: float, digits: int = 2) -> str:
    return f"{value:.{digits}f}"


def compress_mic_ids(mic_ids: tuple[str, ...]) -> str:
    numbers = sorted(int(item[1:]) for item in mic_ids)
    if not numbers:
        return ""
    ranges: list[str] = []
    start = prev = numbers[0]
    for number in numbers[1:]:
        if number == prev + 1:
            prev = number
            continue
        ranges.append(f"M{start:02d}" if start == prev else f"M{start:02d}-M{prev:02d}")
        start = prev = number
    ranges.append(f"M{start:02d}" if start == prev else f"M{start:02d}-M{prev:02d}")
    return ", ".join(ranges)


def html_table(headers: tuple[str, ...], rows: list[list[str]]) -> str:
    head = "".join(f"<th>{escape(h)}</th>" for h in headers)
    body = "".join("<tr>" + "".join(f"<td>{cell}</td>" for cell in row) + "</tr>" for row in rows)
    return f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>"


def scheme_metrics_table() -> str:
    rows: list[list[str]] = []
    for scheme in SCHEMES:
        all_metrics = geometry_summary(scheme.mics)
        core_metrics = geometry_summary(tuple(m for m in scheme.mics if m.is_core16))
        rows.append([
            escape(scheme.short_label),
            fmt(all_metrics["dmax_mm"]),
            fmt(all_metrics["dmin_mm"]),
            fmt(all_metrics["alias_hz"] / 1000.0),
            fmt(duplicate_baseline_ratio(scheme.mics), 1),
            fmt(core_metrics["dmax_mm"]),
            fmt(core_metrics["dmin_mm"]),
            fmt(core_metrics["alias_hz"] / 1000.0),
            escape(scheme.strengths),
        ])
    return html_table(
        ("方案", "32 路最大孔径(mm)", "32 路最小间距(mm)", "32 路混叠起点(kHz)", "重复基线率(0.2mm,%)", "Core16 孔径(mm)", "Core16 最小间距(mm)", "Core16 混叠起点(kHz)", "一句话判断"),
        rows,
    )


def coordinate_table(scheme: Scheme) -> str:
    rows = []
    for mic in scheme.mics:
        rows.append([
            mic.mic_id,
            fmt(mic.x_mm),
            fmt(mic.y_mm),
            layer_of(mic.mic_id),
            mic.bus,
            mic.chip,
            str(mic.chip_slot),
            str(mic.tdm_slot_48k),
            "-" if mic.tdm_slot_192k_core is None else str(mic.tdm_slot_192k_core),
            "是" if mic.is_core16 else "否",
        ])
    return html_table(("Mic", "x(mm)", "y(mm)", "尺度层", "Bus", "Chip", "Chip Slot", "48k Slot", "192k Core Slot", "Core16"), rows)


def core16_table(scheme: Scheme) -> str:
    return coordinate_table(Scheme(scheme.key, scheme.title, scheme.short_label, scheme.rationale, scheme.strengths, scheme.tradeoffs, tuple(m for m in scheme.mics if m.is_core16)))


def chip_mapping_table() -> str:
    rows = []
    chip_text = {
        "U1": ("右上象限混合子阵", "48k: slot0~7；192k: 只开 core slot0~3。"),
        "U2": ("左上象限混合子阵", "48k: slot8~15；192k: 只开 core slot4~7。"),
        "U3": ("左下象限混合子阵", "48k: slot0~7；192k: 只开 core slot0~3。"),
        "U4": ("右下象限混合子阵", "48k: slot8~15；192k: 只开 core slot4~7。"),
    }
    for chip in ("U1", "U2", "U3", "U4"):
        mic_list = sorted((m for m in MAIN_SCHEME.mics if m.chip == chip), key=lambda item: item.chip_slot)
        role, route = chip_text[chip]
        core_slots = [m.tdm_slot_192k_core for m in mic_list if m.tdm_slot_192k_core is not None]
        rows.append([
            chip,
            mic_list[0].bus,
            f"slot {mic_list[0].tdm_slot_48k}~{mic_list[-1].tdm_slot_48k}",
            f"slot {core_slots[0]}~{core_slots[-1]}",
            ", ".join(m.mic_id for m in mic_list),
            role,
            route + " 每片负责 2 个 Inner8 + 2 个 Transition8 + 4 个 Outer16。",
        ])
    return html_table(("PCMD3180", "Bus", "48k TDM Slot", "192k Core Slot", "负责 Mic", "子阵角色", "推荐布线逻辑"), rows)


def comparison_summary_table() -> str:
    rows = [
        ["为什么主方案更优", "在孔径、最小间距、重复基线率和 192k 带宽现实之间最平衡。"],
        ["哪个低频更强", "备选 B 更强，但 Core16 抽取和工程叙事不如主方案。"],
        ["哪个高频旁瓣更低", "主方案整体更稳；备选 A 混叠起点更高，但规则副瓣风险更大。"],
        ["哪个对子阵模式更友好", "主方案最友好，因为 192k 模式由 4 片 PCMD3180 均匀分担、每片只开 4 个核心 mic。"],
        ["哪个更适合 STM32N6 实时实现", "主方案更适合，因为 48k/192k 双模式的采集结构都更清晰。"],
    ]
    return html_table(("问题", "结论"), rows)


def mode_table() -> str:
    rows = []
    for mode in MODES:
        rows.append([
            escape(mode.name),
            escape(compress_mic_ids(mode.mic_ids)),
            escape(mode.chips),
            f"{mode.band_low_hz/1000:.1f} ~ {mode.band_high_hz/1000:.1f} kHz",
            escape(mode.algorithm),
            escape(mode.advantages),
            escape(mode.limitations),
            escape(mode.suited_for),
            escape(mode.cadence_class),
        ])
    return html_table(("模式", "Mic", "芯片/总线", "推荐频带", "算法", "优势", "短板", "更适合", "运行档位"), rows)


def mode_budget(mode: Mode) -> dict[str, float | int]:
    logical_channels = len(mode.mic_ids)
    capture_channels = 16 if mode.sample_rate >= 192_000 else logical_channels
    slots_per_bus = 8 if mode.sample_rate >= 192_000 else 16
    active_buses = 2 if mode.sample_rate >= 192_000 else len({m.bus for m in subset_for_ids(MAIN_SCHEME, mode.mic_ids)})
    delta_f = mode.sample_rate / mode.nfft
    bin_start = math.ceil(mode.band_low_hz / delta_f)
    bin_end = math.floor(mode.band_high_hz / delta_f)
    bin_count = max(0, bin_end - bin_start + 1)
    grid_points = mode.coarse_grid * mode.coarse_grid + mode.fine_top_k * mode.fine_grid * mode.fine_grid
    algo_fps = (mode.sample_rate / mode.nfft) / mode.algo_decim
    return {
        "logical_channels": logical_channels,
        "capture_channels": capture_channels,
        "raw_pcm_mb_s": capture_channels * mode.sample_rate * 2.0 / 1_000_000.0,
        "bclk_mhz": 16 * slots_per_bus * mode.sample_rate / 1_000_000.0,
        "active_buses": active_buses,
        "bin_count": bin_count,
        "coarse_tdoa_kb": mode.coarse_grid * mode.coarse_grid * mode.pair_budget * 4.0 / 1024.0,
        "full_steer_mb": grid_points * mode.pair_budget * bin_count * 8.0 / (1024.0 * 1024.0),
        "algo_fps": algo_fps,
        "search_macc": mode.pair_budget * bin_count * grid_points * algo_fps / 1_000_000.0,
    }


def mode_budget_table() -> str:
    rows = []
    for mode in MODES:
        budget = mode_budget(mode)
        rows.append([
            escape(mode.name),
            f"{budget['logical_channels']} / {budget['capture_channels']}",
            f"{mode.sample_rate/1000:.0f} kHz / {mode.nfft}",
            str(mode.pair_budget),
            f"{mode.coarse_grid}x{mode.coarse_grid} + {mode.fine_top_k}x{mode.fine_grid}x{mode.fine_grid}",
            str(budget["bin_count"]),
            fmt(budget["raw_pcm_mb_s"]),
            f"{fmt(budget['bclk_mhz'])} MHz x {budget['active_buses']} bus",
            f"{fmt(budget['coarse_tdoa_kb'])} KB",
            f"{fmt(budget['full_steer_mb'])} MB",
            f"{fmt(budget['algo_fps'])} fps",
            f"{fmt(budget['search_macc'])} Macc/s",
        ])
    return html_table(
        ("模式", "逻辑/采集通道", "采样/FFT", "Pair", "搜索网格", "频点数", "原始 PCM 带宽", "单总线 BCLK", "粗搜 TDOA LUT", "若预存全 steering", "建议算法帧率", "搜索乘加量级"),
        rows,
    )


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
        ["空间混叠起点", f"{fmt(main32['alias_hz']/1000.0)} kHz"],
        ["推荐主频带", "1.5 kHz ~ 14 kHz"],
        ["理论角分辨率", ", ".join(f"{f//1000} kHz: {fmt(angle_resolution_deg(main32['dmax_mm'], f), 1)}°" for f in (4_000, 8_000, 12_000, 16_000))],
        ["近/远场边界", ", ".join(f"{f//1000} kHz: {fmt(fraunhofer_distance_m(main32['dmax_mm'], f), 2)} m" for f in (4_000, 8_000, 12_000))],
        ["现实检测距离", "0.3 m ~ 20 m；安静环境下强异常源可向 20~50 m 试探"],
        ["现实定位距离", "0.3 m ~ 8 m"],
        ["现实成像距离", "0.25 m ~ 4 m"],
        ["旁瓣风险代理指标", f"重复基线率 {fmt(main_dup,1)}%，低于双环备选的 {fmt(alt_dup,1)}%"],
        ["相对当前 16 路提升", f"孔径 +{fmt((main32['dmax_mm']/baseline['dmax_mm']-1.0)*100.0)}%，混叠起点 +{fmt((main32['alias_hz']/baseline['alias_hz']-1.0)*100.0)}%"],
    ]
    rows_core = [
        ["采集结构", "4 片 PCMD3180 同时工作，每片只开 4 个核心 mic；双总线 TDM8 同步"],
        ["最大有效孔径", f"{fmt(core16['dmax_mm'])} mm"],
        ["最小阵元间距", f"{fmt(core16['dmin_mm'])} mm"],
        ["空间混叠起点", f"{fmt(core16['alias_hz']/1000.0)} kHz"],
        ["推荐主频带", "6 kHz ~ 25 kHz 稳妥；25 kHz ~ 40 kHz 研究模式"],
        ["理论角分辨率", ", ".join(f"{f//1000} kHz: {fmt(angle_resolution_deg(core16['dmax_mm'], f), 1)}°" for f in (10_000, 20_000, 40_000, 60_000))],
        ["近/远场边界", ", ".join(f"{f//1000} kHz: {fmt(fraunhofer_distance_m(core16['dmax_mm'], f), 2)} m" for f in (10_000, 20_000, 40_000))],
        ["现实检测距离", "0.1 m ~ 3 m；强高频异常源可试探到 5 m"],
        ["现实定位距离", "0.1 m ~ 2 m"],
        ["现实成像距离", "0.1 m ~ 1 m"],
    ]
    rows_hf = [
        ["Inner8 指标", f"孔径 {fmt(inner8['dmax_mm'])} mm，最小间距 {fmt(inner8['dmin_mm'])} mm，混叠起点 {fmt(inner8['alias_hz']/1000.0)} kHz"],
        ["50 kHz 以上的意义", "可以形成有意义的近场空间判别，但更像热点确认、方位估计和二次验证。"],
        ["建议距离范围", "20~40 kHz：0.1~2 m；40~80 kHz：0.1~0.8 m"],
        ["主要限制因素", "阵元间距、MEMS 带宽、空气衰减、跨芯片相位误差、外壳散射和假峰"],
        ["改善手段", "切到 Inner8 / Core16、裁掉长基线、频带门控、做宽带标定和前壳声学设计"],
    ]
    rows_lf = [
        ["低频现实", "0.8~2 kHz 主要是粗定位和热点增强，2~4 kHz 才逐渐进入可用成像频段"],
        ["值得投入的增强", "多帧融合、超指向后滤波、IMU 微运动合成孔径都值得做"],
        ["不要硬承诺", "低频高清热图、百米级低频高分辨成像"],
    ]
    rows_conclusion = [
        ["可以承诺", "0.3~8 m 的工业级检测/定位/成像主能力，0.3~20 m 的强异常检测能力"],
        ["可以研究但不应轻承诺", "20~40 kHz 高频精扫、40~80 kHz 近场热点确认、20~50 m 强异常检测"],
        ["物理上基本不现实", "在 100 mm 级 32 路手持阵列上承诺 50~80 kHz 远距离高质量热图"],
    ]
    return {
        "main32": html_table(("指标", "结论"), rows_32),
        "core16": html_table(("指标", "结论"), rows_core),
        "hf": html_table(("项目", "结论"), rows_hf),
        "lf": html_table(("项目", "结论"), rows_lf),
        "conclusion": html_table(("层级", "结论"), rows_conclusion),
    }


def short_notes_html() -> str:
    return """
<div class="grid2">
  <div class="card small">
    <h3>给硬件团队看的简版说明</h3>
    <ul>
      <li>32@48k：4 片 PCMD3180 全开，每片 8 mic，两条同步 TDM16。</li>
      <li>16@192k：4 片也都要工作，但每片只开 4 个核心 mic，建议双总线 TDM8。</li>
      <li>4 片按象限分区，每片负责 2 个 Inner8 + 2 个 Transition8 + 4 个 Outer16。</li>
      <li>192k 模式真正的难点是前端时钟、PDM 质量、跨芯片一致性和前壳散射。</li>
    </ul>
  </div>
  <div class="card small">
    <h3>给算法团队看的简版说明</h3>
    <ul>
      <li>主流程仍是 SRP-PHAT，但 Pair set 必须模式化，不能默认全对穷举。</li>
      <li>32@48k 是主展示模式；16@192k 是高频质量模式，不是默认主画面。</li>
      <li>40~80 kHz 只能作为近场热点确认模式，不要讲成全场高质量成像。</li>
      <li>MVDR/MUSIC 更适合作为 ROI 精修工具，不要替代主流程。</li>
    </ul>
  </div>
</div>
"""


def build_plot_js() -> str:
    data = {
        "board_half_mm": BOARD_HALF_MM,
        "keepout_half_mm": KEEPOUT_HALF_MM,
        "schemes": {
            scheme.key: {
                "title": scheme.title,
                "mics": [{"id": m.mic_id, "x_mm": m.x_mm, "y_mm": m.y_mm, "core16": m.is_core16, "layer": layer_of(m.mic_id)} for m in scheme.mics],
            }
            for scheme in SCHEMES
        },
    }
    return f"""(function () {{
  const DATA = {json.dumps(data, ensure_ascii=False)};
  function renderArrayScheme(canvasId, schemeKey, opts) {{
    const canvas = typeof canvasId === "string" ? document.getElementById(canvasId) : canvasId;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const scheme = DATA.schemes[schemeKey];
    if (!scheme) return;
    const width = canvas.width, height = canvas.height;
    const half = DATA.board_half_mm, keepout = DATA.keepout_half_mm;
    const scale = Math.min(width, height) / (2 * half + 12);
    const px = (x) => width / 2 + x * scale;
    const py = (y) => height / 2 - y * scale;
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#fbfdff"; ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = "#d1d9e6"; ctx.strokeRect(px(-half), py(half), 2 * half * scale, 2 * half * scale);
    ctx.strokeStyle = "#dc2626"; ctx.strokeRect(px(-keepout), py(keepout), 2 * keepout * scale, 2 * keepout * scale);
    scheme.mics.forEach((mic) => {{
      const x = px(mic.x_mm), y = py(mic.y_mm);
      ctx.beginPath();
      ctx.fillStyle = mic.layer === "Outer16" ? "#2563eb" : mic.layer === "Transition8" ? "#f59e0b" : "#0f766e";
      ctx.arc(x, y, mic.core16 ? 5.2 : 4.8, 0, Math.PI * 2);
      ctx.fill(); ctx.strokeStyle = "#0f172a"; ctx.stroke();
      ctx.font = "10px 'Segoe UI', sans-serif"; ctx.fillStyle = "#0f172a"; ctx.fillText(mic.id, x + 6, y - 6);
    }});
    ctx.font = "bold 13px 'Segoe UI', sans-serif"; ctx.fillText((opts && opts.title) || scheme.title, 12, 18);
  }}
  if (typeof window !== "undefined") {{ window.renderNECCSArrayScheme = renderArrayScheme; }}
}})();
"""


def build_html() -> str:
    baseline = current_baseline_metrics()
    main32 = geometry_summary(MAIN_SCHEME.mics)
    perf = performance_tables()
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NECCS 32 路工业化声学阵列方案</title>
  <style>
    body {{ margin: 0; background: #f3f6fb; color: #172033; font: 15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif; }}
    .wrap {{ max-width: 1280px; margin: 0 auto; padding: 28px; }}
    .hero {{ background: linear-gradient(135deg,#0f766e,#0b3a57 55%,#111827); color: #fff; border-radius: 20px; padding: 28px 32px; }}
    .hero h1 {{ margin: 0 0 8px; font-size: 32px; }}
    .tags span {{ display: inline-block; margin: 6px 8px 0 0; padding: 4px 10px; border-radius: 999px; border: 1px solid rgba(255,255,255,.2); font-size: 12px; }}
    h2 {{ margin: 28px 0 12px; }} h3 {{ margin: 0 0 8px; }}
    .card {{ background: #fff; border: 1px solid #d9e0ea; border-radius: 16px; padding: 18px 20px; box-shadow: 0 10px 28px rgba(15,23,42,.06); }}
    .small h3 {{ margin-top: 0; }}
    .grid2 {{ display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 16px; }}
    .grid3 {{ display: grid; grid-template-columns: repeat(3,minmax(0,1fr)); gap: 16px; }}
    table {{ width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 13px; }}
    th, td {{ border: 1px solid #d9e0ea; padding: 8px 10px; vertical-align: top; }} th {{ background: #eff6ff; }}
    canvas {{ width: 100%; max-width: 100%; background: #fff; border: 1px solid #d9e0ea; border-radius: 14px; }}
    ul {{ margin: 8px 0 0 18px; }}
    @media (max-width: 1100px) {{ .grid2, .grid3 {{ grid-template-columns: 1fr; }} }}
  </style>
  <script src="../tools/plot_array_32ch.js"></script>
</head>
<body>
  <div class="wrap">
    <section class="hero">
      <h1>NECCS 32 路工业化声学阵列方案</h1>
      <p>恢复生成时间：{escape(TODAY)}。当前版本按 4 × PCMD3180、32@48k 双 TDM16、16@192k 双 TDM8 恢复。</p>
      <div class="tags"><span>32 物理麦</span><span>4 × PCMD3180</span><span>32@48k 主模式</span><span>16@192k 4 芯片分担</span><span>Inner8 + Transition8 + Outer16</span></div>
    </section>

    <h2>执行摘要</h2>
    <div class="grid2">
      <div class="card">
        <p>主方案采用工业化三尺度结构，而不是规则方阵或规则双环。它把高频短基线、宽频桥接和大孔径成像拆成三个层级，同时满足 192k 模式下 4 片 PCMD3180 都参与、每片只开 4 个核心 mic 的带宽现实。</p>
        <p>与当前 16 路基线相比，最大孔径从 <strong>{fmt(baseline['dmax_mm'])} mm</strong> 提升到 <strong>{fmt(main32['dmax_mm'])} mm</strong>，最小间距从 <strong>{fmt(baseline['dmin_mm'])} mm</strong> 压到 <strong>{fmt(main32['dmin_mm'])} mm</strong>，混叠起点提升到 <strong>{fmt(main32['alias_hz']/1000.0)} kHz</strong>。</p>
      </div>
      <div class="card">
        <ul>
          <li>可承诺：0.3~8 m 的工业级检测/定位/成像主能力。</li>
          <li>可研究：20~40 kHz 高频精扫，40~80 kHz 近场热点确认。</li>
          <li>不应承诺：50~80 kHz 远距离高质量热图。</li>
          <li>16@192k 的可信前提：4 片 PCMD3180 同时工作，每片只开 4 个核心 mic。</li>
        </ul>
      </div>
    </div>

    <h2>现有工程约束复述</h2>
    <div class="card"><ul>
      <li>当前平台：STM32H743，目标迁移到 STM32N6。</li>
      <li>当前链路：16 路 PDM + 2 × PCMD3180 + SAI DMA + 256 点 RFFT + SRP-PHAT + 粗细搜索。</li>
      <li>当前真实基线：最大孔径 {fmt(baseline['dmax_mm'])} mm，最小间距 {fmt(baseline['dmin_mm'])} mm，混叠起点 {fmt(baseline['alias_hz']/1000.0)} kHz。</li>
      <li>延续工程风格：坐标驱动 + LUT + Pair set + coarse-to-fine。</li>
    </ul></div>

    <h2>新阵列设计目标</h2>
    <div class="grid3">
      <div class="card"><strong>物理目标</strong><br>100 mm × 100 mm 平面阵列，中心 28 mm × 28 mm keepout。</div>
      <div class="card"><strong>工作模式</strong><br>32@48k 为主模式；16@192k 为专项高频模式。</div>
      <div class="card"><strong>工业化目标</strong><br>强调工业异常检测、可答辩的工程实现和 STM32N6 迁移可信度。</div>
    </div>

    <h2>推荐主方案</h2>
    <div class="card">
      <p><strong>主方案：</strong>{escape(MAIN_SCHEME.title)}</p>
      <p><strong>设计逻辑：</strong>{escape(MAIN_SCHEME.rationale)}</p>
      <p><strong>优势：</strong>{escape(MAIN_SCHEME.strengths)}</p>
      <p><strong>代价：</strong>{escape(MAIN_SCHEME.tradeoffs)}</p>
      <p><strong>布线建议：</strong>4 片 PCMD3180 按象限分区，每片负责 2 个 Inner8 + 2 个 Transition8 + 4 个 Outer16。48k 模式下每片占满 8 个 slot；192k 模式下每片只开 4 个核心 mic，并把每总线压缩到 TDM8。</p>
    </div>
    <div class="grid3">
      <canvas id="plot-main" width="360" height="360"></canvas>
      <canvas id="plot-alt-a" width="360" height="360"></canvas>
      <canvas id="plot-alt-b" width="360" height="360"></canvas>
    </div>

    <h2>备选方案对比</h2><div class="card">{comparison_summary_table()}{scheme_metrics_table()}</div>
    <h2>32 路坐标表</h2><div class="card">{coordinate_table(MAIN_SCHEME)}</div>
    <h2>16 路核心子阵表</h2><div class="card">{core16_table(MAIN_SCHEME)}</div>
    <h2>PCMD3180 分组映射表</h2><div class="card">{chip_mapping_table()}</div>
    <h2>多模式使用策略</h2><div class="card">{mode_table()}</div>

    <h2>嵌入式实现建议（面向 STM32N6）</h2>
    <div class="card">
      <ul>
        <li>32@48k：4 片全开，双 TDM16，单总线 BCLK 与当前 16@48k 同级。</li>
        <li>16@192k：4 片都参与，但每片只开 4 个核心 mic；每总线压到 TDM8 后，单总线 BCLK 约 24.576 MHz。</li>
        <li>预计算内容：pair_idx、pair_dx/dy、coarse TDOA LUT、频带权重、标定表。</li>
        <li>不要预存全 steering；Fine search 继续实时计算。</li>
        <li>192k 模式更适合触发式、ROI 式、低帧率运行。</li>
      </ul>
      {mode_budget_table()}
    </div>

    <h2>新阵列性能评估</h2>
    <div class="card"><h3>5.1 对 32 路 @ 48 kHz 模式</h3>{perf['main32']}</div>
    <div class="card"><h3>5.2 对 16 路 @ 192 kHz 模式</h3>{perf['core16']}</div>
    <div class="card"><h3>5.3 高频 / 超声潜力评估</h3>{perf['hf']}</div>
    <div class="card"><h3>5.4 低频能力评估</h3>{perf['lf']}</div>
    <div class="card"><h3>5.5 结论必须明确</h3>{perf['conclusion']}</div>

    <h2>风险点与工程注意事项</h2>
    <div class="card"><ul>
      <li>192k 模式首先是前端时钟与芯片带宽问题，不是 MCU 算力问题。</li>
      <li>跨芯片一致性必须流程化标定，至少包含增益、整采样延迟、分数延迟和宽带相位斜率。</li>
      <li>高频能力高度依赖外壳前盖、开孔、倒角、吸声和防散射设计。</li>
    </ul></div>

    <h2>最终建议：哪些值得冲，哪些不要硬冲</h2>
    <div class="grid2">
      <div class="card small"><h3>值得冲</h3><ul><li>32@48k 主模式的高质量工业热图。</li><li>16@192k 的近场高频精扫。</li><li>多模式 Pair set、频带加权、多帧融合和 IMU 微运动补偿。</li></ul></div>
      <div class="card small"><h3>不要硬冲</h3><ul><li>50~80 kHz 的远距离高质量全场热图。</li><li>在 100 mm 手持阵列上承诺百米级低频高分辨成像。</li><li>把 MUSIC/MVDR 当作全流程主算法。</li></ul></div>
    </div>

    {short_notes_html()}
    <h2>备选 A 坐标表</h2><div class="card">{coordinate_table(ALT_DOUBLE_RING)}</div>
    <h2>备选 B 坐标表</h2><div class="card">{coordinate_table(ALT_SPARSE_PLANE)}</div>
  </div>
  <script>
    document.addEventListener('DOMContentLoaded', function () {{
      if (!window.renderNECCSArrayScheme) return;
      window.renderNECCSArrayScheme('plot-main', 'main', {{ title: '主方案：三尺度工业阵列' }});
      window.renderNECCSArrayScheme('plot-alt-a', 'alt_double_ring', {{ title: '备选 A：非均匀双环' }});
      window.renderNECCSArrayScheme('plot-alt-b', 'alt_sparse_plane', {{ title: '备选 B：稀疏平面' }});
    }});
  </script>
</body>
</html>
"""


def write_csv() -> None:
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    with CSV_PATH.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(("mic_id", "x_mm", "y_mm", "bus", "chip", "chip_slot", "tdm_slot_48k", "tdm_slot_192k_core", "is_core16"))
        for mic in MAIN_SCHEME.mics:
            writer.writerow((mic.mic_id, f"{mic.x_mm:.2f}", f"{mic.y_mm:.2f}", mic.bus, mic.chip, mic.chip_slot, mic.tdm_slot_48k, "" if mic.tdm_slot_192k_core is None else mic.tdm_slot_192k_core, "1" if mic.is_core16 else "0"))


def write_html() -> None:
    HTML_PATH.write_text(build_html(), encoding="utf-8", newline="\n")


def write_plot_js() -> None:
    PLOT_JS_PATH.write_text(build_plot_js(), encoding="utf-8", newline="\n")


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
    print()
    print("Current16:", f"Dmax={baseline['dmax_mm']:.2f} mm,", f"Dmin={baseline['dmin_mm']:.2f} mm,", f"f_alias={baseline['alias_hz']/1000.0:.2f} kHz")
    print("Main32:", f"Dmax={main32['dmax_mm']:.2f} mm,", f"Dmin={main32['dmin_mm']:.2f} mm,", f"f_alias={main32['alias_hz']/1000.0:.2f} kHz")
    print("Core16:", f"Dmax={core16['dmax_mm']:.2f} mm,", f"Dmin={core16['dmin_mm']:.2f} mm,", f"f_alias={core16['alias_hz']/1000.0:.2f} kHz")


if __name__ == "__main__":
    main()
