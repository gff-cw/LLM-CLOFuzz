#!/usr/bin/env python3
"""
profuzzbench_analyze_offline.py
================================
Independent, reproducible, schema-aware offline analysis tool for protocol fuzzing benchmarks.
Directly addresses and governs ChatAFL / ProFuzzBench historical evaluation flaws (E01–E08):

  - E01: Explicit experiment start time baseline required; missing baseline marked UNKNOWN.
         Initial setup/startup latency is tracked rather than masked.
  - E02: Strict run accounting preserving all planned runs (SUCCESS, FAILED, TIMEOUT, MISSING, CRASHED).
         Survivor bias audit separates complete-case vs intention-to-treat analyses.
  - E03: Adaptive dynamic range for all plots; no arbitrary [0, 100] clipping.
  - E04: Schema-aware header parsing supporting column reordering, renaming, and missing optional fields.
  - E05: Numerical run sorting (1, 2, ..., 9, 10, 11) avoiding ASCII/lexical truncation.
  - E06: Container/process exit_code, signal, and stderr diagnostics recorded for every run.
  - E07: Strict structured log timestamps, monotonic ordering and deduplication; rejects file mtime.
  - E08: Individual run is the statistical unit (not minute-by-minute samples); distinct reporting
         of source branch/line coverage, bitmap edges, state transitions, state nodes, and throughput.
         Proper censoring for runs not reaching coverage thresholds.
"""

import os
import sys
import re
import csv
import json
import math
import argparse
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Any, Tuple


# ==============================================================================
# Domain Models & Dataclasses
# ==============================================================================

class RunStatus:
    SUCCESS = "SUCCESS"
    FAILED = "FAILED"
    TIMEOUT = "TIMEOUT"
    MISSING = "MISSING"
    CRASHED = "CRASHED"
    UNKNOWN = "UNKNOWN"

    ALL_STATUSES = {SUCCESS, FAILED, TIMEOUT, MISSING, CRASHED, UNKNOWN}


@dataclass
class MetricTimeSeriesPoint:
    timestamp: float
    relative_time_sec: Optional[float]
    metrics: Dict[str, float]


@dataclass
class SingleRunResult:
    run_id: int
    status: str
    exit_code: Optional[int] = None
    error_diagnostic: Optional[str] = None
    time_baseline: str = "UNKNOWN"  # "EXPLICIT" or "UNKNOWN"
    experiment_start_time: Optional[float] = None
    first_log_time: Optional[float] = None
    initial_delay_sec: Optional[float] = None
    time_series: List[MetricTimeSeriesPoint] = field(default_factory=list)
    endpoint_metrics: Dict[str, Optional[float]] = field(default_factory=dict)
    auc_metrics: Dict[str, Optional[float]] = field(default_factory=dict)
    time_to_threshold: Dict[str, Optional[float]] = field(default_factory=dict)
    threshold_reached: Dict[str, bool] = field(default_factory=dict)


@dataclass
class CohortSummary:
    total_planned_runs: int
    successful_runs: int
    failed_runs: int
    crashed_runs: int
    missing_runs: int
    timeout_runs: int
    success_rate: float
    failure_rate: float
    survivor_bias_warning: Optional[str] = None


@dataclass
class MetricDistribution:
    count: int
    mean: Optional[float] = None
    std_dev: Optional[float] = None
    median: Optional[float] = None
    q1: Optional[float] = None
    q3: Optional[float] = None
    iqr: Optional[float] = None
    min_val: Optional[float] = None
    max_val: Optional[float] = None


# ==============================================================================
# Schema Mapping & Normalization (E04)
# ==============================================================================

COLUMN_SYNONYMS = {
    "timestamp": ["time", "unix_time", "timestamp", "epoch", "sec", "seconds", "# time", "# unix_time"],
    "b_abs": ["b_abs", "branch_abs", "branches", "branch_count", "covered_branches"],
    "b_per": ["b_per", "branch_per", "branch_percent", "branch_%"],
    "l_abs": ["l_abs", "line_abs", "lines", "line_count", "covered_lines"],
    "l_per": ["l_per", "line_per", "line_percent", "line_%"],
    "paths_total": ["paths_total", "bitmap_cvg", "bitmap_edges", "edges_total", "total_paths"],
    "map_size": ["map_size", "bitmap_size"],
    "n_edges": ["n_edges", "edges", "transitions", "state_transitions", "trans", "mem_edges"],
    "n_nodes": ["n_nodes", "nodes", "state_nodes", "states", "mem_nodes"],
    "execs_per_sec": ["execs_per_sec", "execs/s", "speed", "throughput", "eps"],
    "cur_path": ["cur_path", "current_path"],
    "pending_total": ["pending_total", "pending_paths"],
    "pending_favs": ["pending_favs", "pending_favorites"]
}


def normalize_column_name(col_raw: str) -> Optional[str]:
    c = col_raw.strip().lower()
    if c.startswith("#"):
        c = c.lstrip("#").strip()
    for canonical, synonyms in COLUMN_SYNONYMS.items():
        if c == canonical or c in synonyms:
            return canonical
    return None


def calculate_quantiles(values: List[float]) -> Tuple[float, float, float]:
    """Calculates Q1 (25th), Median (50th), and Q3 (75th) using standard linear interpolation."""
    if not values:
        return 0.0, 0.0, 0.0
    sorted_v = sorted(values)
    n = len(sorted_v)
    if n == 1:
        return sorted_v[0], sorted_v[0], sorted_v[0]

    def quantile(p):
        idx = p * (n - 1)
        low = int(math.floor(idx))
        high = int(math.ceil(idx))
        weight = idx - low
        return sorted_v[low] * (1.0 - weight) + sorted_v[high] * weight

    return quantile(0.25), quantile(0.50), quantile(0.75)


def compute_distribution(values: List[float]) -> MetricDistribution:
    if not values:
        return MetricDistribution(count=0)
    count = len(values)
    mean = sum(values) / count
    variance = sum((x - mean) ** 2 for x in values) / (count - 1) if count > 1 else 0.0
    std_dev = math.sqrt(variance)
    q1, median, q3 = calculate_quantiles(values)
    return MetricDistribution(
        count=count,
        mean=round(mean, 3),
        std_dev=round(std_dev, 3),
        median=round(median, 3),
        q1=round(q1, 3),
        q3=round(q3, 3),
        iqr=round(q3 - q1, 3),
        min_val=round(min(values), 3),
        max_val=round(max(values), 3)
    )


# ==============================================================================
# CSV & File Parsers
# ==============================================================================

class BenchmarkParser:
    """Parses individual run CSV and log files with schema tolerance and timestamp validation."""

    @staticmethod
    def parse_csv_file(file_path: Path) -> List[Dict[str, float]]:
        if not file_path.exists() or file_path.stat().st_size == 0:
            return []

        rows: List[Dict[str, float]] = []
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            reader = csv.reader(f)
            header = None
            col_map: Dict[int, str] = {}

            for line in reader:
                if not line or not any(field.strip() for field in line):
                    continue
                # Skip synthetic or general comments
                first_cell = line[0].strip()
                if first_cell.startswith("#") and not any(term in first_cell.lower() for term in ["time", "unix"]):
                    continue

                if header is None:
                    header = [c.strip() for c in line]
                    for idx, col in enumerate(header):
                        canonical = normalize_column_name(col)
                        if canonical:
                            col_map[idx] = canonical
                    continue

                # Parse row values
                row_dict: Dict[str, float] = {}
                for idx, cell in enumerate(line):
                    if idx in col_map:
                        canonical_name = col_map[idx]
                        cleaned_cell = cell.strip()
                        try:
                            val = float(cleaned_cell)
                            row_dict[canonical_name] = val
                        except ValueError:
                            continue

                if "timestamp" in row_dict:
                    rows.append(row_dict)

        return rows

    @staticmethod
    def clean_and_sort_time_series(raw_rows: List[Dict[str, float]]) -> List[Dict[str, float]]:
        """Governs E07: monotonic timestamp sorting and duplicate timestamp handling."""
        if not raw_rows:
            return []

        # Sort chronologically by timestamp
        sorted_rows = sorted(raw_rows, key=lambda r: r["timestamp"])

        # Handle duplicates: merge or retain latest values
        deduped: List[Dict[str, float]] = []
        for row in sorted_rows:
            if not deduped:
                deduped.append(row)
            else:
                last_row = deduped[-1]
                if row["timestamp"] == last_row["timestamp"]:
                    # Overwrite/update with latest point for this timestamp
                    last_row.update(row)
                else:
                    deduped.append(row)

        return deduped


# ==============================================================================
# Core Offline Analysis Engine
# ==============================================================================

class OfflineAnalyzer:
    def __init__(self,
                 manifest_path: Optional[Path] = None,
                 data_dir: Optional[Path] = None,
                 subject: Optional[str] = None,
                 fuzzer: Optional[str] = None,
                 planned_runs: Optional[List[int]] = None,
                 experiment_start_time: Optional[float] = None,
                 cut_off_seconds: Optional[float] = None,
                 thresholds: Optional[Dict[str, float]] = None):
        self.manifest_path = manifest_path
        self.data_dir = data_dir or (manifest_path.parent if manifest_path else Path("."))
        self.manifest_data: Dict[str, Any] = {}
        if manifest_path and manifest_path.exists():
            with open(manifest_path, "r", encoding="utf-8") as f:
                self.manifest_data = json.load(f)

        self.is_synthetic = self.manifest_data.get("IS_SYNTHETIC", False)
        self.synthetic_notice = self.manifest_data.get("SYNTHETIC_DATASET_NOTICE", "")

        self.subject = subject or self.manifest_data.get("subject", "unknown_subject")
        self.fuzzer = fuzzer or self.manifest_data.get("fuzzer", "unknown_fuzzer")

        # Explicit experiment start baseline (E01)
        m_start = self.manifest_data.get("experiment_start_time")
        if experiment_start_time is not None:
            self.experiment_start_time = experiment_start_time
            self.time_baseline = "EXPLICIT"
        elif m_start is not None:
            self.experiment_start_time = float(m_start)
            self.time_baseline = "EXPLICIT"
        else:
            self.experiment_start_time = None
            self.time_baseline = "UNKNOWN"

        self.cut_off_seconds = (
            cut_off_seconds
            if cut_off_seconds is not None
            else self.manifest_data.get("cut_off_seconds", 3600.0)
        )

        # Coverage / state thresholds for time-to-threshold analysis (E08)
        self.thresholds = thresholds or self.manifest_data.get("coverage_thresholds", {"b_abs": 80.0, "n_edges": 100.0})

        # Planned runs resolution (E02, E05: numerical order!)
        self.planned_runs = self._resolve_planned_runs(planned_runs)

    def _resolve_planned_runs(self, cli_runs: Optional[List[int]]) -> List[int]:
        """Resolves planned run IDs in numerical order, preventing lexical sort omissions (E05)."""
        if cli_runs:
            return sorted(list(set(cli_runs)))

        if "planned_runs" in self.manifest_data:
            return sorted([int(r) for r in self.manifest_data["planned_runs"]])

        if "runs" in self.manifest_data and isinstance(self.manifest_data["runs"], list):
            ids = [int(r["run_id"]) for r in self.manifest_data["runs"] if "run_id" in r]
            if ids:
                return sorted(list(set(ids)))

        # Fallback: scan data directory for run directories using regex
        discovered = set()
        for p in self.data_dir.iterdir():
            if p.is_dir():
                m = re.search(r'(?:run|out)[-_](\d+)$', p.name, re.IGNORECASE)
                if m:
                    discovered.add(int(m.group(1)))
            elif p.is_file() and p.name.endswith(".tar.gz"):
                m = re.search(r'[-_](\d+)\.tar\.gz$', p.name, re.IGNORECASE)
                if m:
                    discovered.add(int(m.group(1)))

        return sorted(list(discovered)) if discovered else [1]

    def _find_run_files(self, run_id: int) -> Tuple[Optional[Path], Optional[Path], Optional[Path]]:
        """Locates plot_data, cov_over_time.csv, and error.log for a given run ID."""
        run_candidates = [
            self.data_dir / f"run_{run_id}",
            self.data_dir / f"run-{run_id}",
            self.data_dir / f"out-{self.subject}-{self.fuzzer}-{run_id}",
            self.data_dir / f"out-{self.subject}-{self.fuzzer}_{run_id}"
        ]

        run_dir = None
        for cand in run_candidates:
            if cand.exists() and cand.is_dir():
                run_dir = cand
                break

        if not run_dir:
            return None, None, None

        plot_file = None
        for p_name in ["plot_data.csv", "plot_data", "plot.csv"]:
            p_cand = run_dir / p_name
            if p_cand.exists():
                plot_file = p_cand
                break

        cov_file = None
        for c_name in ["cov_over_time.csv", "coverage.csv"]:
            c_cand = run_dir / c_name
            if c_cand.exists():
                cov_file = c_cand
                break

        err_file = None
        for e_name in ["error.log", "stderr.log", "container.log"]:
            e_cand = run_dir / e_name
            if e_cand.exists():
                err_file = e_cand
                break

        return plot_file, cov_file, err_file

    def analyze_single_run(self, run_id: int) -> SingleRunResult:
        """Analyzes a single run according to E01, E02, E04, E06, E07, E08."""
        manifest_runs_meta = {
            int(r.get("run_id", -1)): r for r in self.manifest_data.get("runs", [])
        }
        meta = manifest_runs_meta.get(run_id, {})

        plot_file, cov_file, err_file = self._find_run_files(run_id)

        # Determine status and diagnostics (E02, E06)
        exit_code = meta.get("exit_code")
        error_msg = meta.get("error")

        if err_file and err_file.exists():
            try:
                err_content = err_file.read_text(encoding="utf-8", errors="replace").strip()
                if err_content and not error_msg:
                    error_msg = err_content.splitlines()[-1]
            except Exception:
                pass

        # Check for missing files
        if not plot_file and not cov_file:
            status = meta.get("status", RunStatus.MISSING)
            return SingleRunResult(
                run_id=run_id,
                status=status if status in RunStatus.ALL_STATUSES else RunStatus.MISSING,
                exit_code=exit_code,
                error_diagnostic=error_msg or "Run files not found in archive or directory",
                time_baseline=self.time_baseline,
                experiment_start_time=self.experiment_start_time
            )

        # Parse CSV logs
        plot_rows = BenchmarkParser.parse_csv_file(plot_file) if plot_file else []
        cov_rows = BenchmarkParser.parse_csv_file(cov_file) if cov_file else []

        # Merge plot_rows and cov_rows by timestamp
        combined_dict: Dict[float, Dict[str, float]] = {}
        for r in plot_rows:
            t = r["timestamp"]
            combined_dict.setdefault(t, {})["timestamp"] = t
            combined_dict[t].update(r)

        for r in cov_rows:
            t = r["timestamp"]
            combined_dict.setdefault(t, {})["timestamp"] = t
            combined_dict[t].update(r)

        merged_raw = list(combined_dict.values())
        cleaned_series = BenchmarkParser.clean_and_sort_time_series(merged_raw)

        if not cleaned_series:
            status = meta.get("status", RunStatus.FAILED)
            return SingleRunResult(
                run_id=run_id,
                status=status,
                exit_code=exit_code,
                error_diagnostic=error_msg or "CSV files present but contained no valid data rows",
                time_baseline=self.time_baseline,
                experiment_start_time=self.experiment_start_time
            )

        # First log timestamp and initial delay analysis (E01)
        first_log_time = cleaned_series[0]["timestamp"]
        initial_delay_sec = None
        if self.time_baseline == "EXPLICIT" and self.experiment_start_time is not None:
            initial_delay_sec = max(0.0, first_log_time - self.experiment_start_time)

        # Construct time series with relative time (E01)
        time_series_points: List[MetricTimeSeriesPoint] = []
        for pt in cleaned_series:
            t_epoch = pt["timestamp"]
            rel_t = None
            if self.time_baseline == "EXPLICIT" and self.experiment_start_time is not None:
                rel_t = max(0.0, t_epoch - self.experiment_start_time)

            metrics_only = {k: v for k, v in pt.items() if k != "timestamp"}
            time_series_points.append(
                MetricTimeSeriesPoint(
                    timestamp=t_epoch,
                    relative_time_sec=rel_t,
                    metrics=metrics_only
                )
            )

        # Determine run status
        declared_status = meta.get("status")
        if declared_status in RunStatus.ALL_STATUSES:
            run_status = declared_status
        elif exit_code is not None:
            if exit_code == 0:
                run_status = RunStatus.SUCCESS
            elif exit_code in [139, 137]:
                run_status = RunStatus.CRASHED
            elif exit_code == 124:
                run_status = RunStatus.TIMEOUT
            else:
                run_status = RunStatus.FAILED
        else:
            run_status = RunStatus.SUCCESS

        # Endpoint metrics (E08)
        last_metrics = time_series_points[-1].metrics if time_series_points else {}
        endpoint_metrics = {k: last_metrics.get(k) for k in [
            "b_abs", "b_per", "l_abs", "l_per", "paths_total", "n_edges", "n_nodes", "execs_per_sec"
        ] if k in last_metrics}

        # Area Under Curve (AUC) using trapezoidal rule (E08)
        auc_metrics = {}
        if self.time_baseline == "EXPLICIT" and len(time_series_points) >= 2:
            for m_key in ["b_abs", "n_edges", "paths_total"]:
                auc_val = 0.0
                for i in range(1, len(time_series_points)):
                    t_prev = time_series_points[i - 1].relative_time_sec
                    t_curr = time_series_points[i].relative_time_sec
                    v_prev = time_series_points[i - 1].metrics.get(m_key, 0.0)
                    v_curr = time_series_points[i].metrics.get(m_key, 0.0)
                    if t_prev is not None and t_curr is not None:
                        dt = t_curr - t_prev
                        if dt > 0:
                            auc_val += 0.5 * (v_prev + v_curr) * dt
                auc_metrics[m_key] = round(auc_val, 2)

        # Time to threshold calculation with proper censoring (E08)
        time_to_thresh: Dict[str, Optional[float]] = {}
        thresh_reached: Dict[str, bool] = {}

        for th_key, th_val in self.thresholds.items():
            th_canonical = normalize_column_name(th_key) or th_key
            reached = False
            t_reach = None
            for pt in time_series_points:
                val = pt.metrics.get(th_canonical)
                if val is None:
                    val = pt.metrics.get(th_key)
                if val is not None and val >= th_val:
                    reached = True
                    t_reach = pt.relative_time_sec
                    break

            thresh_reached[th_key] = reached
            time_to_thresh[th_key] = t_reach  # Remains None if NOT reached (no substitution!)

        return SingleRunResult(
            run_id=run_id,
            status=run_status,
            exit_code=exit_code,
            error_diagnostic=error_msg,
            time_baseline=self.time_baseline,
            experiment_start_time=self.experiment_start_time,
            first_log_time=first_log_time,
            initial_delay_sec=initial_delay_sec,
            time_series=time_series_points,
            endpoint_metrics=endpoint_metrics,
            auc_metrics=auc_metrics,
            time_to_threshold=time_to_thresh,
            threshold_reached=thresh_reached
        )

    def analyze_all_runs(self) -> Tuple[List[SingleRunResult], CohortSummary, Dict[str, Any]]:
        """Aggregates all planned runs, enforcing E02 survivor bias accounting and E08 statistics."""
        run_results: List[SingleRunResult] = []
        for r_id in self.planned_runs:
            res = self.analyze_single_run(r_id)
            run_results.append(res)

        total_planned = len(self.planned_runs)
        successes = sum(1 for r in run_results if r.status == RunStatus.SUCCESS)
        failed = sum(1 for r in run_results if r.status == RunStatus.FAILED)
        crashed = sum(1 for r in run_results if r.status == RunStatus.CRASHED)
        missing = sum(1 for r in run_results if r.status == RunStatus.MISSING)
        timeouts = sum(1 for r in run_results if r.status == RunStatus.TIMEOUT)

        success_rate = round(successes / total_planned, 4) if total_planned > 0 else 0.0
        failure_rate = round((total_planned - successes) / total_planned, 4) if total_planned > 0 else 0.0

        survivor_warning = None
        if failure_rate > 0:
            survivor_warning = (
                f"SURVIVOR BIAS WARNING: {total_planned - successes} of {total_planned} planned runs "
                f"({failure_rate * 100:.1f}%) failed, crashed, timed out, or were missing. "
                f"Averaging only surviving runs will artificially inflate reported effectiveness!"
            )

        cohort_summary = CohortSummary(
            total_planned_runs=total_planned,
            successful_runs=successes,
            failed_runs=failed,
            crashed_runs=crashed,
            missing_runs=missing,
            timeout_runs=timeouts,
            success_rate=success_rate,
            failure_rate=failure_rate,
            survivor_bias_warning=survivor_warning
        )

        # Metric distributions across successful runs (complete cases)
        metric_keys = ["b_abs", "b_per", "l_abs", "l_per", "paths_total", "n_edges", "n_nodes", "execs_per_sec"]
        complete_case_dist: Dict[str, Any] = {}
        for k in metric_keys:
            vals = [
                r.endpoint_metrics[k] for r in run_results
                if r.status == RunStatus.SUCCESS and k in r.endpoint_metrics and r.endpoint_metrics[k] is not None
            ]
            complete_case_dist[k] = asdict(compute_distribution(vals))

        # Intention-to-treat / Threshold achievement summary (E08)
        threshold_summary: Dict[str, Any] = {}
        for th_key, th_val in self.thresholds.items():
            reached_count = sum(1 for r in run_results if r.threshold_reached.get(th_key, False))
            reached_times = [
                r.time_to_threshold[th_key] for r in run_results
                if r.threshold_reached.get(th_key, False) and r.time_to_threshold.get(th_key) is not None
            ]
            threshold_summary[th_key] = {
                "target_threshold_value": th_val,
                "total_planned_runs": total_planned,
                "reached_count": reached_count,
                "achievement_rate": round(reached_count / total_planned, 4) if total_planned > 0 else 0.0,
                "censored_runs_count": total_planned - reached_count,
                "time_to_reach_distribution_seconds": asdict(compute_distribution(reached_times)),
                "note": "Censored runs (those never reaching threshold) are NOT assigned arbitrary completion timestamps."
            }

        # Multi-metric distinct reporting (E08)
        aggregated_stats = {
            "complete_case_endpoint_distributions": complete_case_dist,
            "threshold_achievement_summary": threshold_summary,
            "survivor_bias_audit": asdict(cohort_summary)
        }

        return run_results, cohort_summary, aggregated_stats


# ==============================================================================
# Visualization with Adaptive Dynamic Range (E03)
# ==============================================================================

def generate_evaluation_plots(run_results: List[SingleRunResult],
                              cohort_summary: CohortSummary,
                              subject: str,
                              fuzzer: str,
                              out_file: Path) -> bool:
    """Generates multi-panel evaluation plots with adaptive dynamic axes (governing E03)."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("[WARNING] matplotlib is not installed. Plot generation skipped.", file=sys.stderr)
        return False

    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    fig.suptitle(f"Fuzzing Benchmark Analysis: {subject} - {fuzzer}\n"
                 f"[Planned: {cohort_summary.total_planned_runs}, Success: {cohort_summary.successful_runs}, "
                 f"Failed/Missing: {cohort_summary.total_planned_runs - cohort_summary.successful_runs}]",
                 fontsize=14, fontweight="bold")

    # Plot metrics:
    # (0, 0): Source Branch Coverage (b_abs)
    # (0, 1): State Transitions (n_edges) -> Governs E03 (dynamic range > 100)
    # (1, 0): Bitmap Edge Coverage (paths_total)
    # (1, 1): State Nodes (n_nodes)

    plot_configs = [
        (axes[0, 0], "b_abs", "Source Branch Coverage (#branches)", "Branch Coverage (b_abs)"),
        (axes[0, 1], "n_edges", "State Transitions (#transitions) [Adaptive Range]", "Transitions (n_edges)"),
        (axes[1, 0], "paths_total", "Fuzzer Bitmap Paths (#paths)", "Bitmap Coverage"),
        (axes[1, 1], "n_nodes", "Discovered State Nodes (#nodes)", "State Nodes (n_nodes)")
    ]

    for ax, metric_key, title, ylabel in plot_configs:
        ax.set_title(title, fontsize=11, fontweight="semibold")
        ax.set_xlabel("Relative Time (minutes)")
        ax.set_ylabel(ylabel)
        ax.grid(True, linestyle="--", alpha=0.6)

        max_val_observed = 0.0

        for r in run_results:
            if not r.time_series:
                continue
            
            x_vals = []
            y_vals = []
            for pt in r.time_series:
                t = pt.relative_time_sec if pt.relative_time_sec is not None else (pt.timestamp - r.time_series[0].timestamp)
                val = pt.metrics.get(metric_key)
                if val is not None:
                    x_vals.append(t / 60.0)  # Convert seconds to minutes
                    y_vals.append(val)
                    if val > max_val_observed:
                        max_val_observed = val

            line_style = "-" if r.status == RunStatus.SUCCESS else "--"
            alpha = 0.85 if r.status == RunStatus.SUCCESS else 0.5
            label = f"Run {r.run_id} ({r.status})"
            ax.plot(x_vals, y_vals, line_style, alpha=alpha, label=label, linewidth=1.5)

            # Mark crashes / failures on the plot
            if r.status != RunStatus.SUCCESS and x_vals:
                ax.scatter([x_vals[-1]], [y_vals[-1]], color="red", marker="x", s=60, zorder=5)

        # Dynamic y-axis scaling (E03): Adaptive range, never fixed to [0, 100]
        y_upper = max(max_val_observed * 1.15, 10.0)
        ax.set_ylim(bottom=0.0, top=y_upper)
        ax.legend(loc="upper left", fontsize=8)

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    out_file.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out_file, dpi=200)
    plt.close(fig)
    print(f"[PLOT] Evaluation figures successfully saved with adaptive dynamic axes to: {out_file.resolve()}")
    return True


# ==============================================================================
# Reporting & Output Formatting
# ==============================================================================

def format_text_summary(subject: str,
                        fuzzer: str,
                        time_baseline: str,
                        cohort_summary: CohortSummary,
                        aggregated_stats: Dict[str, Any],
                        run_results: List[SingleRunResult],
                        is_synthetic: bool) -> str:
    lines = []
    lines.append("=" * 78)
    if is_synthetic:
        lines.append("!!! [SYNTHETIC TEST DATASET - NOT EMPIRICAL FUZZING EVIDENCE] !!!")
    lines.append(f"BENCHMARK OFFLINE ANALYSIS REPORT: {subject} | {fuzzer}")
    lines.append("=" * 78)
    lines.append(f"Time Baseline Status: {time_baseline}")
    if time_baseline == "UNKNOWN":
        lines.append("  [NOTICE] Experiment start baseline is UNKNOWN. Relative time is uncalibrated.")
    lines.append(f"Total Planned Runs:   {cohort_summary.total_planned_runs}")
    lines.append(f"Successful Runs:      {cohort_summary.successful_runs} ({cohort_summary.success_rate * 100:.1f}%)")
    lines.append(f"Failed / Crashed:     {cohort_summary.failed_runs + cohort_summary.crashed_runs}")
    lines.append(f"Missing Runs:         {cohort_summary.missing_runs}")
    lines.append(f"Timeout Runs:         {cohort_summary.timeout_runs}")
    lines.append(f"Failure Rate:         {cohort_summary.failure_rate * 100:.1f}%")

    if cohort_summary.survivor_bias_warning:
        lines.append("-" * 78)
        lines.append(cohort_summary.survivor_bias_warning)
        lines.append("-" * 78)

    lines.append("\n" + "=" * 32 + " RUN REGISTRY " + "=" * 32)
    lines.append(f"{'Run ID':<8} {'Status':<10} {'ExitCode':<10} {'InitDelay(s)':<14} {'Transitions':<14} {'Branches':<12}")
    lines.append("-" * 78)
    for r in run_results:
        init_delay_str = f"{r.initial_delay_sec:.1f}" if r.initial_delay_sec is not None else "N/A"
        trans_str = str(r.endpoint_metrics.get("n_edges", "N/A"))
        branch_str = str(r.endpoint_metrics.get("b_abs", "N/A"))
        exit_code_str = str(r.exit_code) if r.exit_code is not None else "N/A"
        lines.append(f"{r.run_id:<8} {r.status:<10} {exit_code_str:<10} {init_delay_str:<14} {trans_str:<14} {branch_str:<12}")
        if r.error_diagnostic:
            lines.append(f"   -> Diagnostic: {r.error_diagnostic}")

    lines.append("\n" + "=" * 25 + " METRIC DISTRIBUTIONS (Complete Cases) " + "=" * 25)
    cc_dist = aggregated_stats["complete_case_endpoint_distributions"]
    lines.append(f"{'Metric':<16} {'Count':<6} {'Median':<10} {'IQR':<10} {'Mean':<10} {'StdDev':<10} {'Max':<10}")
    lines.append("-" * 78)
    for m_key, dist in cc_dist.items():
        if dist["count"] > 0:
            lines.append(f"{m_key:<16} {dist['count']:<6} {dist['median']:<10} {dist['iqr']:<10} "
                         f"{dist['mean']:<10} {dist['std_dev']:<10} {dist['max_val']:<10}")

    lines.append("\n" + "=" * 25 + " THRESHOLD ACHIEVEMENT & CENSORING " + "=" * 25)
    th_summary = aggregated_stats["threshold_achievement_summary"]
    for th_key, th_info in th_summary.items():
        lines.append(f"Target [{th_key} >= {th_info['target_threshold_value']}]:")
        lines.append(f"  Reached: {th_info['reached_count']}/{th_info['total_planned_runs']} runs "
                     f"({th_info['achievement_rate'] * 100:.1f}%) | Censored: {th_info['censored_runs_count']} runs")
        t_dist = th_info["time_to_reach_distribution_seconds"]
        if t_dist["count"] > 0:
            lines.append(f"  Time-to-Reach (sec): Median={t_dist['median']}, IQR={t_dist['iqr']}, "
                         f"Min={t_dist['min_val']}, Max={t_dist['max_val']}")
        else:
            lines.append("  Time-to-Reach: No runs reached threshold.")

    lines.append("=" * 78)
    return "\n".join(lines)


# ==============================================================================
# CLI Entry Point
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Independent, reproducible, schema-aware offline analysis tool for protocol fuzzing benchmarks."
    )
    parser.add_argument("-m", "--manifest", type=str, help="Path to experiment manifest.json")
    parser.add_argument("-d", "--data-dir", type=str, help="Directory containing run results")
    parser.add_argument("-p", "--put", "--subject", type=str, dest="subject", help="Target PUT name")
    parser.add_argument("-f", "--fuzzers", type=str, help="Fuzzer name")
    parser.add_argument("-r", "--runs", type=str, help="Planned run IDs, e.g. '1,2,9,10,11'")
    parser.add_argument("-s", "--start-time", type=float, help="Explicit experiment start epoch timestamp")
    parser.add_argument("--unknown-start-time", action="store_true", help="Explicitly mark start time as UNKNOWN")
    parser.add_argument("-c", "--cut-off", type=float, help="Cut-off duration in seconds")
    parser.add_argument("-o", "--out-json", type=str, help="Output JSON report file path")
    parser.add_argument("--out-plot", type=str, help="Output plot image file path (e.g. .png)")
    parser.add_argument("--summary-txt", type=str, help="Output text summary file path")
    parser.add_argument("--threshold", action="append", help="Threshold specification, e.g. 'b_abs=80' or 'n_edges=100'")
    parser.add_argument("--strict", action="store_true", help="Exit with code 1 if any run failed or crashed")

    args = parser.parse_args()

    manifest_path = Path(args.manifest) if args.manifest else None
    data_dir = Path(args.data_dir) if args.data_dir else None

    # Parse thresholds
    thresholds = {}
    if args.threshold:
        for th in args.threshold:
            if "=" in th:
                k, v = th.split("=", 1)
                thresholds[k.strip()] = float(v.strip())

    # Parse runs
    planned_runs = None
    if args.runs:
        planned_runs = [int(r.strip()) for r in args.runs.split(",") if r.strip().isdigit()]

    exp_start = None
    if not args.unknown_start_time and args.start_time is not None:
        exp_start = args.start_time

    analyzer = OfflineAnalyzer(
        manifest_path=manifest_path,
        data_dir=data_dir,
        subject=args.subject,
        fuzzer=args.fuzzers,
        planned_runs=planned_runs,
        experiment_start_time=exp_start,
        cut_off_seconds=args.cut_off,
        thresholds=thresholds if thresholds else None
    )

    run_results, cohort_summary, agg_stats = analyzer.analyze_all_runs()

    # Generate JSON report
    report_dict = {
        "analysis_tool": "profuzzbench_analyze_offline",
        "tool_version": "2.0-governed",
        "is_synthetic_dataset": analyzer.is_synthetic,
        "synthetic_notice": analyzer.synthetic_notice,
        "subject": analyzer.subject,
        "fuzzer": analyzer.fuzzer,
        "time_baseline": analyzer.time_baseline,
        "experiment_start_time": analyzer.experiment_start_time,
        "cut_off_seconds": analyzer.cut_off_seconds,
        "coverage_thresholds": analyzer.thresholds,
        "cohort_summary": asdict(cohort_summary),
        "aggregated_statistics": agg_stats,
        "runs": [
            {
                "run_id": r.run_id,
                "status": r.status,
                "exit_code": r.exit_code,
                "error_diagnostic": r.error_diagnostic,
                "time_baseline": r.time_baseline,
                "initial_delay_sec": r.initial_delay_sec,
                "endpoint_metrics": r.endpoint_metrics,
                "auc_metrics": r.auc_metrics,
                "threshold_reached": r.threshold_reached,
                "time_to_threshold": r.time_to_threshold,
                "sample_count": len(r.time_series)
            }
            for r in run_results
        ]
    }

    if args.out_json:
        out_json_path = Path(args.out_json)
        out_json_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_json_path, "w", encoding="utf-8") as f:
            json.dump(report_dict, f, indent=2)
        print(f"[JSON REPORT] Saved offline analysis report to: {out_json_path.resolve()}")

    # Generate text summary
    summary_text = format_text_summary(
        subject=analyzer.subject,
        fuzzer=analyzer.fuzzer,
        time_baseline=analyzer.time_baseline,
        cohort_summary=cohort_summary,
        aggregated_stats=agg_stats,
        run_results=run_results,
        is_synthetic=analyzer.is_synthetic
    )

    print(summary_text)

    if args.summary_txt:
        sum_path = Path(args.summary_txt)
        sum_path.parent.mkdir(parents=True, exist_ok=True)
        sum_path.write_text(summary_text, encoding="utf-8")
        print(f"[SUMMARY] Saved text summary to: {sum_path.resolve()}")

    # Generate plots if requested
    if args.out_plot:
        generate_evaluation_plots(
            run_results=run_results,
            cohort_summary=cohort_summary,
            subject=analyzer.subject,
            fuzzer=analyzer.fuzzer,
            out_file=Path(args.out_plot)
        )

    if args.strict and cohort_summary.failure_rate > 0:
        print("[STRICT MODE] Aborting with exit code 1 due to non-zero run failures.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
