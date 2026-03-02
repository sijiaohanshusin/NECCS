#!/usr/bin/env python3
"""Quick SRP-DOA sanity checks against generated LUT and current runtime math.

This script is offline and non-intrusive. It helps validate:
1) pair/channel coverage
2) coarse grid symmetry
3) expected contrast/quality behavior under ideal single-source model
"""

from __future__ import annotations

import re
from collections import Counter
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
LUT_C = ROOT / "START" / "User" / "Algorithm" / "ai_srp_lut.c"

SPEED_OF_SOUND = 343.0
FS = 48000.0
NFFT = 256.0
DELTA_F = FS / NFFT
BIN_START = 3
BIN_END = 42
FREQ_BINS = np.arange(BIN_START, BIN_END + 1)
OMEGA = 2.0 * np.pi * (FREQ_BINS * DELTA_F)


def parse_float_array(name: str, text: str, expect_len: int) -> np.ndarray:
    m = re.search(rf"const float32_t {name}\[{expect_len}\] = \{{([^}}]*)\}};", text, re.S)
    if not m:
        raise RuntimeError(f"array not found: {name}")
    vals = [float(v.replace("f", "")) for v in re.findall(r"[-+]?\d+\.\d+(?:e[-+]?\d+)?f", m.group(1))]
    if len(vals) != expect_len:
        raise RuntimeError(f"array length mismatch for {name}: {len(vals)} != {expect_len}")
    return np.asarray(vals, dtype=np.float64)


def parse_pair_idx(text: str) -> np.ndarray:
    m = re.search(r"const uint8_t srp_pair_idx\[(\d+)\]\[2\] = \{(.*?)\};", text, re.S)
    if not m:
        raise RuntimeError("srp_pair_idx not found")
    n = int(m.group(1))
    pairs = [tuple(map(int, g)) for g in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", m.group(2))]
    if len(pairs) != n:
        raise RuntimeError(f"pair count mismatch: {len(pairs)} != {n}")
    return np.asarray(pairs, dtype=np.int32)


def score_for_tau(gcc: np.ndarray, tau: np.ndarray) -> float:
    ph = np.outer(tau, OMEGA)
    return float(np.real(gcc * np.exp(-1j * ph)).sum())


def build_coarse_taus(dx: np.ndarray, dy: np.ndarray, coarse: np.ndarray) -> np.ndarray:
    out = []
    for th in coarse:
        sth = np.sin(np.deg2rad(th))
        for ph in coarse:
            cph = np.cos(np.deg2rad(ph))
            sph = np.sin(np.deg2rad(ph))
            out.append((dx * sth * cph + dy * sph) / SPEED_OF_SOUND)
    return np.asarray(out, dtype=np.float64)


def second_max(values: np.ndarray, max_idx: int) -> float:
    if values.size <= 1:
        return -1.0e30
    return float(np.max(np.delete(values, max_idx)))


def main() -> None:
    text = LUT_C.read_text(encoding="utf-8")

    pairs = parse_pair_idx(text)
    dx = parse_float_array("srp_pair_dx", text, pairs.shape[0])
    dy = parse_float_array("srp_pair_dy", text, pairs.shape[0])

    # Parse from actual angle table.
    m_angles = re.search(r"const float32_t coarse_theta_deg\[(\d+)\] = \{([^}]*)\};", text)
    if not m_angles:
        raise RuntimeError("coarse_theta_deg not found")
    n_angles = int(m_angles.group(1))
    coarse = np.asarray([float(v.replace("f", "")) for v in re.findall(r"[-+]?\d+\.\d+f", m_angles.group(2))], dtype=np.float64)

    if coarse.size != n_angles:
        raise RuntimeError("coarse angle parse mismatch")

    print("=== Pair Coverage ===")
    cnt = Counter()
    for a, b in pairs:
        cnt[int(a)] += 1
        cnt[int(b)] += 1
    print("pair_count:", pairs.shape[0])
    print("channel_coverage:", sorted(cnt.items()))
    missing = [ch for ch in range(16) if ch not in cnt]
    print("missing_channels:", missing)

    print("\n=== Coarse Grid ===")
    print("coarse_size:", n_angles, "x", n_angles)
    print("angles_deg:", coarse.tolist())

    coarse_taus = build_coarse_taus(dx, dy, coarse)

    raw_contrast = []
    quality_contrast = []

    # Ideal single-source test grid
    for th in np.linspace(-50.0, 50.0, 31):
        for ph in np.linspace(-40.0, 40.0, 25):
            sth = np.sin(np.deg2rad(th))
            cph = np.cos(np.deg2rad(ph))
            sph = np.sin(np.deg2rad(ph))
            tau_true = (dx * sth * cph + dy * sph) / SPEED_OF_SOUND

            # Ideal GCC so that true direction is perfectly phase-aligned.
            gcc = np.exp(1j * np.outer(tau_true, OMEGA))

            p = np.asarray([score_for_tau(gcc, tau) for tau in coarse_taus], dtype=np.float64)
            max_idx = int(np.argmax(p))
            max_val = float(p[max_idx])

            # Raw contrast
            second_raw = second_max(p, max_idx)
            raw = (max_val - second_raw) / (abs(max_val) + 1e-6)
            raw_contrast.append(raw)

            # Quality contrast: exclude 1-step neighbor in coarse grid
            max_ti = max_idx // n_angles
            max_pi = max_idx % n_angles
            second_q = -1.0e30
            found = False
            for i, val in enumerate(p):
                if i == max_idx:
                    continue
                ti = i // n_angles
                pi = i % n_angles
                if abs(ti - max_ti) <= 1 and abs(pi - max_pi) <= 1:
                    continue
                if val > second_q:
                    second_q = float(val)
                    found = True
            if not found:
                second_q = second_raw
            q = (max_val - second_q) / (abs(max_val) + 1e-6)
            quality_contrast.append(q)

    raw_contrast = np.asarray(raw_contrast)
    quality_contrast = np.asarray(quality_contrast)

    def pct(arr: np.ndarray, p: float) -> float:
        return float(np.percentile(arr, p))

    print("\n=== Idealized Contrast Stats ===")
    print("raw_p50/p90/p95:", pct(raw_contrast, 50), pct(raw_contrast, 90), pct(raw_contrast, 95))
    print("quality_p50/p90/p95:", pct(quality_contrast, 50), pct(quality_contrast, 90), pct(quality_contrast, 95))
    print("raw_ratio_lt_0.03:", float(np.mean(raw_contrast < 0.03)))
    print("quality_ratio_lt_0.03:", float(np.mean(quality_contrast < 0.03)))


if __name__ == "__main__":
    main()
