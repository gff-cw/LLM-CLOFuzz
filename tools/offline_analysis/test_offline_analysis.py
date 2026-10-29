#!/usr/bin/env python3
"""
test_offline_analysis.py
========================
Comprehensive automated test suite for offline analysis tools and E01–E08 governance.

Tests:
  - TestE01_TimeBaseline: Explicit experiment start time vs UNKNOWN baseline; initial delay.
  - TestE02_SurvivorBias: Planned runs accounting; FAILED, CRASHED, MISSING detection; survivor warning.
  - TestE03_DynamicRange: State transitions > 100; dynamic plot range scaling.
  - TestE04_SchemaAwareness: Column reordering, synonym headers, missing optional fields.
  - TestE05_NumericalRunSorting: Order 1, 2, 9, 10, 11 preserved; no lexical truncation.
  - TestE06_ExitCodesAndDiagnostics: Exit codes (0, 1, 139) and error logs recorded.
  - TestE07_TimestampMonotonicity: Monotonic sorting of out-of-order logs, deduplication.
  - TestE08_StatisticalUnitsAndCensoring: Run-level statistics, threshold censoring.
  - TestEndToEndIntegration: Full synthetic dataset generation and offline analysis pipeline.
"""

import os
import sys
import json
import shutil
import tempfile
import unittest
from pathlib import Path

# Add tool directory to sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from profuzzbench_analyze_offline import (
    OfflineAnalyzer,
    BenchmarkParser,
    RunStatus,
    CohortSummary,
    compute_distribution,
    normalize_column_name,
    generate_evaluation_plots
)
from generate_synthetic_test_data import generate_synthetic_dataset


class TestE01_TimeBaseline(unittest.TestCase):
    """E01: Explicit experiment start time baseline vs UNKNOWN baseline; initial delay."""

    def test_explicit_baseline_and_initial_delay(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            t0 = 1700000000.0
            t_first = 1700000450.0  # 450s delay

            run_dir = tmp_path / "run_1"
            run_dir.mkdir()
            with open(run_dir / "plot_data.csv", "w", encoding="utf-8") as f:
                f.write("time,b_abs,n_edges\n")
                f.write(f"{int(t_first)},50,20\n")
                f.write(f"{int(t_first + 300)},60,30\n")

            analyzer = OfflineAnalyzer(
                data_dir=tmp_path,
                subject="test_prog",
                fuzzer="test_fuzzer",
                planned_runs=[1],
                experiment_start_time=t0
            )

            res = analyzer.analyze_single_run(1)
            self.assertEqual(res.time_baseline, "EXPLICIT")
            self.assertEqual(res.experiment_start_time, t0)
            self.assertEqual(res.first_log_time, t_first)
            self.assertEqual(res.initial_delay_sec, 450.0)
            self.assertEqual(res.time_series[0].relative_time_sec, 450.0)

    def test_unknown_baseline_refuses_to_guess(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            run_dir = tmp_path / "run_1"
            run_dir.mkdir()
            with open(run_dir / "plot_data.csv", "w", encoding="utf-8") as f:
                f.write("time,b_abs,n_edges\n")
                f.write("1700000500,50,20\n")

            # Missing start time
            analyzer = OfflineAnalyzer(
                data_dir=tmp_path,
                subject="test_prog",
                fuzzer="test_fuzzer",
                planned_runs=[1],
                experiment_start_time=None
            )

            res = analyzer.analyze_single_run(1)
            self.assertEqual(res.time_baseline, "UNKNOWN")
            self.assertIsNone(res.experiment_start_time)
            self.assertIsNone(res.initial_delay_sec)
            self.assertIsNone(res.time_series[0].relative_time_sec)


class TestE02_SurvivorBias(unittest.TestCase):
    """E02: Accounting of all planned runs; explicit statuses; survivor bias audit."""

    def test_cohort_accounting_and_survivor_warning(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            generate_synthetic_dataset(tmp_path, include_explicit_baseline=True)

            manifest_path = tmp_path / "manifest.json"
            analyzer = OfflineAnalyzer(manifest_path=manifest_path)
            run_results, cohort_summary, agg_stats = analyzer.analyze_all_runs()

            # Verify planned runs 1, 2, 9, 10, 11 are all accounted for
            self.assertEqual(cohort_summary.total_planned_runs, 5)
            self.assertEqual(cohort_summary.successful_runs, 2)
            self.assertEqual(cohort_summary.crashed_runs, 1)  # run 9
            self.assertEqual(cohort_summary.failed_runs, 1)   # run 10
            self.assertEqual(cohort_summary.missing_runs, 1)  # run 11
            self.assertAlmostEqual(cohort_summary.failure_rate, 0.60, places=2)
            self.assertIsNotNone(cohort_summary.survivor_bias_warning)
            self.assertIn("SURVIVOR BIAS WARNING", cohort_summary.survivor_bias_warning)

            # Check individual statuses
            status_map = {r.run_id: r.status for r in run_results}
            self.assertEqual(status_map[1], RunStatus.SUCCESS)
            self.assertEqual(status_map[2], RunStatus.SUCCESS)
            self.assertEqual(status_map[9], RunStatus.CRASHED)
            self.assertEqual(status_map[10], RunStatus.FAILED)
            self.assertEqual(status_map[11], RunStatus.MISSING)


class TestE03_DynamicRange(unittest.TestCase):
    """E03: State transitions > 100 dynamically scaled, not clamped to [0, 100]."""

    def test_transitions_exceed_100(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            generate_synthetic_dataset(tmp_path, include_explicit_baseline=True)

            manifest_path = tmp_path / "manifest.json"
            analyzer = OfflineAnalyzer(manifest_path=manifest_path)
            run_results, cohort_summary, _ = analyzer.analyze_all_runs()

            res1 = next(r for r in run_results if r.run_id == 1)
            res2 = next(r for r in run_results if r.run_id == 2)

            self.assertGreater(res1.endpoint_metrics["n_edges"], 100)
            self.assertGreater(res2.endpoint_metrics["n_edges"], 200)

            # Test plot generation dynamic scaling
            plot_path = tmp_path / "eval_plot.png"
            success = generate_evaluation_plots(run_results, cohort_summary, "lightftp", "stateafl", plot_path)
            if success:
                self.assertTrue(plot_path.exists())
                self.assertGreater(plot_path.stat().st_size, 1000)


class TestE04_SchemaAwareness(unittest.TestCase):
    """E04: Column reordering, synonym matching, and optional missing fields."""

    def test_reordered_and_synonym_columns(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            csv_path = tmp_path / "custom.csv"
            # Header has reordered columns, synonym headers (# unix_time, transitions, branches)
            with open(csv_path, "w", encoding="utf-8") as f:
                f.write("# unix_time, transitions, speed, branches, states\n")
                f.write("1700000100, 142, 95.5, 78, 25\n")
                f.write("1700000200, 155, 96.0, 84, 27\n")

            rows = BenchmarkParser.parse_csv_file(csv_path)
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]["timestamp"], 1700000100.0)
            self.assertEqual(rows[0]["n_edges"], 142.0)
            self.assertEqual(rows[0]["execs_per_sec"], 95.5)
            self.assertEqual(rows[0]["b_abs"], 78.0)
            self.assertEqual(rows[0]["n_nodes"], 25.0)

    def test_column_normalizer(self):
        self.assertEqual(normalize_column_name("b_abs"), "b_abs")
        self.assertEqual(normalize_column_name("branch_abs"), "b_abs")
        self.assertEqual(normalize_column_name("transitions"), "n_edges")
        self.assertEqual(normalize_column_name("speed"), "execs_per_sec")
        self.assertEqual(normalize_column_name("# unix_time"), "timestamp")
        self.assertIsNone(normalize_column_name("unknown_bogus_col"))


class TestE05_NumericalRunSorting(unittest.TestCase):
    """E05: Numerical run sorting (1, 2, 9, 10, 11) avoiding ASCII truncation."""

    def test_numerical_sorting_discovers_10_and_11(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            # Create directories: run_1, run_2, run_9, run_10, run_11
            for r in [1, 2, 9, 10, 11]:
                r_dir = tmp_path / f"run_{r}"
                r_dir.mkdir()
                with open(r_dir / "plot_data.csv", "w", encoding="utf-8") as f:
                    f.write("time,b_abs\n")
                    f.write(f"17000000{r}0,50\n")

            analyzer = OfflineAnalyzer(data_dir=tmp_path)
            self.assertEqual(analyzer.planned_runs, [1, 2, 9, 10, 11])
            # Ensure 10 and 11 are not truncated or ordered lexically (e.g. [1, 10, 11, 2, 9])
            self.assertEqual(analyzer.planned_runs[3], 10)
            self.assertEqual(analyzer.planned_runs[4], 11)


class TestE06_ExitCodesAndDiagnostics(unittest.TestCase):
    """E06: Process exit codes and failure diagnostics recorded and surfaced."""

    def test_exit_codes_and_error_logs(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            generate_synthetic_dataset(tmp_path, include_explicit_baseline=True)

            manifest_path = tmp_path / "manifest.json"
            analyzer = OfflineAnalyzer(manifest_path=manifest_path)
            run_results, _, _ = analyzer.analyze_all_runs()

            r9 = next(r for r in run_results if r.run_id == 9)
            self.assertEqual(r9.exit_code, 139)
            self.assertEqual(r9.status, RunStatus.CRASHED)
            self.assertIn("SIGSEGV", r9.error_diagnostic)

            r10 = next(r for r in run_results if r.run_id == 10)
            self.assertEqual(r10.exit_code, 1)
            self.assertEqual(r10.status, RunStatus.FAILED)
            self.assertIn("Address already in use", r10.error_diagnostic)


class TestE07_TimestampMonotonicity(unittest.TestCase):
    """E07: Monotonic sorting of out-of-order logs, deduplication, no file mtime."""

    def test_out_of_order_and_duplicate_timestamps(self):
        raw_rows = [
            {"timestamp": 100.0, "val": 1.0},
            {"timestamp": 300.0, "val": 3.0},
            {"timestamp": 200.0, "val": 2.0},  # out-of-order
            {"timestamp": 300.0, "val": 3.5},  # duplicate timestamp
            {"timestamp": 400.0, "val": 4.0}
        ]
        cleaned = BenchmarkParser.clean_and_sort_time_series(raw_rows)
        timestamps = [r["timestamp"] for r in cleaned]
        self.assertEqual(timestamps, [100.0, 200.0, 300.0, 400.0])
        # Duplicate merged
        self.assertEqual(cleaned[2]["val"], 3.5)


class TestE08_StatisticalUnitsAndCensoring(unittest.TestCase):
    """E08: Run-level statistical units, threshold censoring, multi-metric reporting."""

    def test_threshold_censoring(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            generate_synthetic_dataset(tmp_path, include_explicit_baseline=True)

            manifest_path = tmp_path / "manifest.json"
            analyzer = OfflineAnalyzer(manifest_path=manifest_path)
            run_results, _, agg_stats = analyzer.analyze_all_runs()

            r1 = next(r for r in run_results if r.run_id == 1)
            r2 = next(r for r in run_results if r.run_id == 2)

            # Target threshold for branch coverage is 80
            # Run 1 reached threshold around t=2100s
            self.assertTrue(r1.threshold_reached["branch_abs"])
            self.assertIsNotNone(r1.time_to_threshold["branch_abs"])
            self.assertGreaterEqual(r1.time_to_threshold["branch_abs"], 2000.0)

            # Run 2 peaked at 68, never reached 80 -> censored!
            self.assertFalse(r2.threshold_reached["branch_abs"])
            self.assertIsNone(r2.time_to_threshold["branch_abs"])  # MUST NOT substitute last sample!

            # Check aggregate threshold stats
            th_stats = agg_stats["threshold_achievement_summary"]["branch_abs"]
            self.assertEqual(th_stats["reached_count"], 1)
            self.assertEqual(th_stats["total_planned_runs"], 5)
            self.assertEqual(th_stats["achievement_rate"], 0.20)
            self.assertEqual(th_stats["censored_runs_count"], 4)

    def test_compute_distribution(self):
        vals = [10.0, 20.0, 30.0, 40.0, 50.0]
        dist = compute_distribution(vals)
        self.assertEqual(dist.count, 5)
        self.assertEqual(dist.median, 30.0)
        self.assertEqual(dist.min_val, 10.0)
        self.assertEqual(dist.max_val, 50.0)
        self.assertEqual(dist.mean, 30.0)
        self.assertEqual(dist.q1, 20.0)
        self.assertEqual(dist.q3, 40.0)
        self.assertEqual(dist.iqr, 20.0)


class TestEndToEndIntegration(unittest.TestCase):
    """Full integration pipeline test."""

    def test_full_pipeline(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            dataset_dir = tmp_path / "synthetic_run"
            generate_synthetic_dataset(dataset_dir, include_explicit_baseline=True)

            out_json = tmp_path / "report.json"
            out_plot = tmp_path / "plot.png"
            out_summary = tmp_path / "summary.txt"

            manifest = dataset_dir / "manifest.json"
            analyzer = OfflineAnalyzer(manifest_path=manifest)
            run_results, cohort_summary, agg_stats = analyzer.analyze_all_runs()

            self.assertEqual(len(run_results), 5)
            self.assertEqual(cohort_summary.total_planned_runs, 5)

            # Verify JSON generation
            report_data = {
                "cohort_summary": cohort_summary.__dict__,
                "aggregated_statistics": agg_stats,
                "runs": [r.__dict__ for r in run_results]
            }
            with open(out_json, "w", encoding="utf-8") as f:
                json.dump(report_data, f, default=str)
            self.assertTrue(out_json.exists())


if __name__ == "__main__":
    unittest.main()
