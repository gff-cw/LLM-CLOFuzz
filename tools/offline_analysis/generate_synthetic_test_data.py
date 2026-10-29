#!/usr/bin/env python3
"""
generate_synthetic_test_data.py
================================
Synthetic benchmark dataset generator for offline analysis verification.

CRITICAL NOTICE:
All datasets produced by this script are SYNTHETIC and purely intended for
validating the robustness and statistical correctness of offline analysis tools
under edge-case conditions (E01-E08). They MUST NOT be cited or used as empirical
fuzzing performance evidence.

Edge cases generated:
  - Run IDs: 1, 2, 9, 10, 11 (testing numerical vs lexical sort E05)
  - Missing and failed runs:
      * Run 1: SUCCESS (exit_code 0)
      * Run 2: SUCCESS (exit_code 0)
      * Run 9: CRASHED (exit_code 139, SIGSEGV, early termination at t=1200s)
      * Run 10: FAILED (exit_code 1, error log, early termination at t=600s)
      * Run 11: MISSING (corpus / log files completely missing from archive)
  - Delayed first record:
      * Run 1 first record appears at T0 + 450s, while experiment starts at T0 (E01)
  - Permuted and missing columns:
      * Run 1 uses non-standard column ordering and omits optional columns (E04)
  - High state transition counts:
      * State transitions (n_edges) reach 165 and 210, well above 100 (E03)
  - Duplicate and out-of-order timestamps:
      * Injected into Run 1 to verify monotonic sorting and deduplication (E07)
  - Coverage threshold censoring:
      * Run 1 reaches branch threshold (b_abs=80) at t=2100s
      * Run 2 never reaches branch threshold (peaks at 68) -> censored (E08)
  - Separate dataset with UNKNOWN start time:
      * Verifies that missing start baseline is flagged as UNKNOWN (E01)
"""

import os
import json
import random
import argparse
from pathlib import Path


SYNTHETIC_HEADER_COMMENT = "# [SYNTHETIC TEST DATA] Generated purely for offline analysis edge-case validation.\n"


def create_plot_data_row(fields, values):
    return ",".join(str(values.get(f, "")) for f in fields) + "\n"


def generate_synthetic_dataset(output_dir: Path, include_explicit_baseline: bool = True):
    """
    Generates a synthetic experiment directory structure with manifest.json and run subdirectories.
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    
    exp_start_epoch = 1700000000.0 if include_explicit_baseline else None
    cut_off_seconds = 3600
    subject = "lightftp"
    fuzzer = "stateafl_mock"

    manifest = {
        "IS_SYNTHETIC": True,
        "SYNTHETIC_DATASET_NOTICE": (
            "WARNING: This dataset is artificially generated for offline analysis verification "
            "and edge-case testing. It must NOT be used as fuzzing evaluation results or empirical evidence."
        ),
        "schema_version": "1.0",
        "subject": subject,
        "fuzzer": fuzzer,
        "experiment_start_time": exp_start_epoch,
        "cut_off_seconds": cut_off_seconds,
        "coverage_thresholds": {
            "branch_abs": 80,
            "state_transitions": 100
        },
        "planned_runs": [1, 2, 9, 10, 11],
        "runs": [
            {
                "run_id": 1,
                "status": "SUCCESS",
                "exit_code": 0,
                "error": None,
                "description": "Completed run with delayed initial log (T0+450s), permuted columns, transitions>100, reaching threshold"
            },
            {
                "run_id": 2,
                "status": "SUCCESS",
                "exit_code": 0,
                "error": None,
                "description": "Completed run with normal start, standard columns, transitions>200, below threshold (censored)"
            },
            {
                "run_id": 9,
                "status": "CRASHED",
                "exit_code": 139,
                "error": "Process terminated by signal 11 (SIGSEGV) at t=1200s",
                "description": "Container crashed midway"
            },
            {
                "run_id": 10,
                "status": "FAILED",
                "exit_code": 1,
                "error": "Network socket bind error: Address already in use",
                "description": "Fuzzer startup failed or aborted early at t=600s"
            },
            {
                "run_id": 11,
                "status": "MISSING",
                "exit_code": None,
                "error": "Output archive missing from container or docker cp failed",
                "description": "No log or plot data available"
            }
        ]
    }

    manifest_path = output_dir / "manifest.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    # Base time
    t0 = 1700000000.0

    # -------------------------------------------------------------
    # RUN 1: Delayed start, column permutation, duplicate/out-of-order, transitions > 100
    # -------------------------------------------------------------
    run1_dir = output_dir / f"run_1"
    run1_dir.mkdir(parents=True, exist_ok=True)

    # Column order permuted: time, n_edges, execs_per_sec, b_abs, n_nodes, cur_path, paths_total, l_abs
    run1_fields = ["time", "n_edges", "execs_per_sec", "b_abs", "n_nodes", "cur_path", "paths_total", "l_abs", "b_per", "l_per"]
    
    run1_points = []
    # Delayed start: first record at t0 + 450s
    timestamps = [450, 500, 600, 750, 900, 1200, 1500, 1800, 2100, 2400, 3000, 3600]
    # Inject out-of-order and duplicate timestamps
    timestamps_with_anomalies = [450, 500, 750, 600, 900, 1200, 1200, 1500, 1800, 2100, 2400, 3000, 3600]

    for rel_t in timestamps_with_anomalies:
        epoch_t = t0 + rel_t
        # Progress metrics
        progress = min(1.0, rel_t / 3600.0)
        # Reaches 80+ at t=2100 (progress=0.583 -> b_abs = 20 + 110*0.583 = 84)
        b_abs = int(20 + 110 * progress)
        l_abs = int(100 + 400 * progress)
        b_per = round(min(100.0, b_abs / 120.0 * 100.0), 2)
        l_per = round(min(100.0, l_abs / 600.0 * 100.0), 2)
        n_edges = int(10 + 155 * progress)  # reaches 165 (> 100)
        n_nodes = int(5 + 35 * progress)
        paths_total = int(50 + 450 * progress)
        cur_path = int(paths_total * 0.8)
        execs_per_sec = round(85.0 + 15.0 * random.random(), 2)

        row_data = {
            "time": int(epoch_t),
            "n_edges": n_edges,
            "execs_per_sec": execs_per_sec,
            "b_abs": b_abs,
            "n_nodes": n_nodes,
            "cur_path": cur_path,
            "paths_total": paths_total,
            "l_abs": l_abs,
            "b_per": b_per,
            "l_per": l_per
        }
        run1_points.append(row_data)

    run1_csv_path = run1_dir / "plot_data.csv"
    with open(run1_csv_path, "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write(",".join(run1_fields) + "\n")
        for pt in run1_points:
            f.write(create_plot_data_row(run1_fields, pt))

    # Also generate independent cov_over_time.csv
    run1_cov_path = run1_dir / "cov_over_time.csv"
    with open(run1_cov_path, "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("time,l_per,l_abs,b_per,b_abs\n")
        for pt in run1_points:
            f.write(f"{pt['time']},{pt['l_per']},{pt['l_abs']},{pt['b_per']},{pt['b_abs']}\n")

    # -------------------------------------------------------------
    # RUN 2: Standard column order, normal start, transitions reach 210, below branch threshold (censored)
    # -------------------------------------------------------------
    run2_dir = output_dir / f"run_2"
    run2_dir.mkdir(parents=True, exist_ok=True)

    # Standard AFLNet / ChatAFL column layout
    run2_fields = [
        "unix_time", "cycles_done", "cur_path", "paths_total", "pending_total",
        "pending_favs", "map_size", "unique_crashes", "unique_hangs", "max_depth",
        "execs_per_sec", "n_nodes", "n_edges", "chat_times"
    ]
    run2_points = []
    run2_cov_points = []
    rel_times_run2 = list(range(60, 3600, 300)) + [3600]
    for rel_t in rel_times_run2:
        epoch_t = t0 + rel_t
        progress = min(1.0, rel_t / 3600.0)
        # Branch coverage only reaches 65 (below threshold 80!)
        b_abs = int(15 + 50 * progress)
        l_abs = int(80 + 280 * progress)
        b_per = round(min(100.0, b_abs / 120.0 * 100.0), 2)
        l_per = round(min(100.0, l_abs / 600.0 * 100.0), 2)
        n_edges = int(12 + 208 * progress)  # reaches 220 (> 200)
        n_nodes = int(6 + 45 * progress)
        paths_total = int(40 + 380 * progress)
        cur_path = int(paths_total * 0.75)
        execs_per_sec = round(92.0 + 10.0 * random.random(), 2)

        row_data = {
            "unix_time": int(epoch_t),
            "cycles_done": int(progress * 5),
            "cur_path": cur_path,
            "paths_total": paths_total,
            "pending_total": 20,
            "pending_favs": 5,
            "map_size": 2048,
            "unique_crashes": 0,
            "unique_hangs": 0,
            "max_depth": 10,
            "execs_per_sec": execs_per_sec,
            "n_nodes": n_nodes,
            "n_edges": n_edges,
            "chat_times": 0
        }
        run2_points.append(row_data)
        run2_cov_points.append({
            "time": int(epoch_t),
            "l_per": l_per,
            "l_abs": l_abs,
            "b_per": b_per,
            "b_abs": b_abs
        })

    with open(run2_dir / "plot_data", "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("# " + ", ".join(run2_fields) + "\n")
        for pt in run2_points:
            f.write(", ".join(str(pt[k]) for k in run2_fields) + "\n")

    with open(run2_dir / "cov_over_time.csv", "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("time,l_per,l_abs,b_per,b_abs\n")
        for pt in run2_cov_points:
            f.write(f"{pt['time']},{pt['l_per']},{pt['l_abs']},{pt['b_per']},{pt['b_abs']}\n")

    # -------------------------------------------------------------
    # RUN 9: CRASHED at t=1200s (exit_code 139)
    # -------------------------------------------------------------
    run9_dir = output_dir / f"run_9"
    run9_dir.mkdir(parents=True, exist_ok=True)

    run9_points = []
    run9_cov_points = []
    for rel_t in range(60, 1201, 300):
        epoch_t = t0 + rel_t
        progress = min(1.0, rel_t / 3600.0)
        b_abs = int(15 + 40 * progress)
        l_abs = int(80 + 200 * progress)
        b_per = round(min(100.0, b_abs / 120.0 * 100.0), 2)
        l_per = round(min(100.0, l_abs / 600.0 * 100.0), 2)
        n_edges = int(10 + 60 * progress)
        n_nodes = int(5 + 15 * progress)
        paths_total = int(40 + 150 * progress)

        row_data = {
            "unix_time": int(epoch_t),
            "cycles_done": 0,
            "cur_path": int(paths_total * 0.5),
            "paths_total": paths_total,
            "pending_total": 10,
            "pending_favs": 2,
            "map_size": 1024,
            "unique_crashes": 1,
            "unique_hangs": 0,
            "max_depth": 5,
            "execs_per_sec": 75.0,
            "n_nodes": n_nodes,
            "n_edges": n_edges,
            "chat_times": 0
        }
        run9_points.append(row_data)
        run9_cov_points.append({
            "time": int(epoch_t),
            "l_per": l_per,
            "l_abs": l_abs,
            "b_per": b_per,
            "b_abs": b_abs
        })

    with open(run9_dir / "plot_data", "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("# " + ", ".join(run2_fields) + "\n")
        for pt in run9_points:
            f.write(", ".join(str(pt[k]) for k in run2_fields) + "\n")

    with open(run9_dir / "cov_over_time.csv", "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("time,l_per,l_abs,b_per,b_abs\n")
        for pt in run9_cov_points:
            f.write(f"{pt['time']},{pt['l_per']},{pt['l_abs']},{pt['b_per']},{pt['b_abs']}\n")

    with open(run9_dir / "error.log", "w", encoding="utf-8") as f:
        f.write("[FATAL] Process 9821 terminated with SIGSEGV (segmentation fault) at ip 0x00007fffa9123\n")

    # -------------------------------------------------------------
    # RUN 10: FAILED at t=600s (exit_code 1)
    # -------------------------------------------------------------
    run10_dir = output_dir / f"run_10"
    run10_dir.mkdir(parents=True, exist_ok=True)

    run10_points = []
    run10_cov_points = []
    for rel_t in [60, 300, 600]:
        epoch_t = t0 + rel_t
        row_data = {
            "unix_time": int(epoch_t),
            "cycles_done": 0,
            "cur_path": 5,
            "paths_total": 15,
            "pending_total": 5,
            "pending_favs": 1,
            "map_size": 512,
            "unique_crashes": 0,
            "unique_hangs": 0,
            "max_depth": 2,
            "execs_per_sec": 30.0,
            "n_nodes": 4,
            "n_edges": 8,
            "chat_times": 0
        }
        run10_points.append(row_data)
        run10_cov_points.append({
            "time": int(epoch_t),
            "l_per": 5.0,
            "l_abs": 30,
            "b_per": 4.0,
            "b_abs": 10
        })

    with open(run10_dir / "plot_data", "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("# " + ", ".join(run2_fields) + "\n")
        for pt in run10_points:
            f.write(", ".join(str(pt[k]) for k in run2_fields) + "\n")

    with open(run10_dir / "cov_over_time.csv", "w", encoding="utf-8") as f:
        f.write(SYNTHETIC_HEADER_COMMENT)
        f.write("time,l_per,l_abs,b_per,b_abs\n")
        for pt in run10_cov_points:
            f.write(f"{pt['time']},{pt['l_per']},{pt['l_abs']},{pt['b_per']},{pt['b_abs']}\n")

    with open(run10_dir / "error.log", "w", encoding="utf-8") as f:
        f.write("[ERROR] bind(21) failed: Address already in use. Aborting campaign.\n")

    # -------------------------------------------------------------
    # RUN 11: MISSING (directory is completely empty or absent)
    # -------------------------------------------------------------
    # Deliberately do NOT create run_11 directory or files to verify MISSING detection.


def main():
    parser = argparse.ArgumentParser(description="Generate synthetic fuzzing datasets for offline analysis validation.")
    parser.add_argument("-o", "--output-dir", type=str, default="./synthetic_dataset",
                        help="Path to generate synthetic data directory")
    parser.add_argument("--unknown-baseline", action="store_true",
                        help="Generate dataset with UNKNOWN start time baseline")
    args = parser.parse_args()

    out_path = Path(args.output_dir)
    generate_synthetic_dataset(out_path, include_explicit_baseline=(not args.unknown_baseline))
    print(f"[SYNTHETIC GENERATOR] Created synthetic benchmark dataset at: {out_path.resolve()}")


if __name__ == "__main__":
    main()
