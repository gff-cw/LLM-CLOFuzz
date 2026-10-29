# 离线结果分析工具与评价问题（E01–E08）治理方案

本目录提供面向协议模糊测试（如 StateAFL、AFLNet、ChatAFL 等基线）的独立、可追溯离线结果分析与统计验证工具。
直接治理 ChatAFL 原始脚本与 ProFuzzBench 历史评价缺陷（E01–E08）。

---

## 一、问题背景与缺陷审查（E01–E08 治理对照）

| 缺陷编号 | 原始脚本缺陷 | 代码位置证据 | 治理实现与约束 |
|---|---|---|---|
| **E01** | 首条日志记录作为时间零点，掩盖初始延迟 | `profuzzbench_plot.py:41`、`profuzzbench_state.py:38` (`start = df2.iloc[0, 0]`) | 必须使用显式 `experiment_start_time` 基准；缺少时标为 `UNKNOWN`，严禁用首条日志或文件 mtime 代替，并显式报告各 run 初始启动延迟 `initial_delay_sec`。 |
| **E02** | 捕获全部异常后跳过 run，并仅按幸存 run 平均 | `profuzzbench_plot.py:49-53`、`profuzzbench_state.py:46-50` (`except Exception: ... cov_total / max(run_count, 1)`) | 保留全部计划 run，显式区分 `SUCCESS`, `FAILED`, `TIMEOUT`, `MISSING`, `CRASHED`，严禁静默剔除；输出幸存者偏差警示（Survivor Bias Audit）。 |
| **E03** | 状态转移数纵轴固定 0–100，高状态协议被截断 | `profuzzbench_state.py:71` (`axes[1].set_ylim([0,100])`) | 自适应动态范围缩放（Adaptive Dynamic Range），基于实际最大值动态计算上限，支持超过 100、200+ 状态转移。 |
| **E04** | 固定读取第 12/13 列状态数据，列重排即错读 | `profuzzbench_generate_csv.sh:66-67` (`cut -d',' -f12`, `f13`) | 基于 header / schema 字段名与同义词映射读取，支持列顺序任意重排、不同 fuzzer 的字段变体与非关键字段缺失容错。 |
| **E05** | run 编号用字典序倒序取最大值，导致 10 以上漏掉 | `profuzzbench_generate_all.sh:32` (`sort -r\|head -1`) | 采用数值型排序与解析 run 编号（如 1, 2, ..., 9, 10, 11），严禁字典序截断。 |
| **E06** | 容器/进程执行未保留退出状态，静默失败 | `profuzzbench_exec_common.sh:33, 41, 49` (`docker wait ... > /dev/null`, `I am done!`) | 记录真实 `exit_code`、信号（如 139 为 SIGSEGV，137 为 SIGKILL/OOM）及 stderr 失败诊断日志，显式标记异常终止。 |
| **E07** | 覆盖率时间依赖语料文件 mtime，易受文件操作污染 | `LightFTP/cov_script.sh:41, 63` (`stat -c %Y $f`) | 采用结构化日志显式时间戳，提供时间戳单调升序排序、时间倒流修正与重复秒级时间戳合并，拒绝依赖文件系统 mtime。 |
| **E08** | 统计单元混淆，将每分钟采样点当作独立样本 | `profuzzbench_plot.py:53`；`CHATAFL_EVALUATION_REVIEW.md:50-52` | 统计单元严格为**独立 run**（而非时间序列采样点）；分开报告源码分支、源码行、bitmap 边、状态转移、状态节点和吞吐量；覆盖阈值未达到时作显式截断（Censored），严禁偷换为终止时间。 |

---

## 二、工具架构与核心文件

- **`profuzzbench_analyze_offline.py`**：
  核心离线分析引擎。支持单 run 深度分析、多 run 队列聚合、异常 run 归因、动态坐标轴绘图、结构化 JSON 报表及 Markdown 摘要导出。
- **`generate_synthetic_test_data.py`**：
  合成基准测试数据生成器（显著标明 `SYNTHETIC`），生成涵盖 1/2/9/10/11 run 编号、SIGSEGV 崩溃、失败退出、缺失文件、启动延迟 450s、字段重排、状态转移突破 200、乱序时间戳与覆盖未达阈值（右截断）的全套边界条件。
- **`test_offline_analysis.py`**：
  自动化单元与集成验收测试套件，全面覆盖 E01–E08 治理项，确保工具在各类边缘条件下的鲁棒性。

---

## 三、命令行使用说明

### 1. 生成合成边缘测试数据
```bash
python tools/offline_analysis/generate_synthetic_test_data.py -o ./synthetic_dataset
```
或生成缺少时间基准的数据集（测试 E01 UNKNOWN 机制）：
```bash
python tools/offline_analysis/generate_synthetic_test_data.py -o ./synthetic_dataset_unknown --unknown-baseline
```

### 2. 执行离线结果分析
通过 manifest 配置文件分析：
```bash
python tools/offline_analysis/profuzzbench_analyze_offline.py \
    -m ./synthetic_dataset/manifest.json \
    -o ./analysis_report.json \
    --out-plot ./analysis_plot.png \
    --summary-txt ./summary.txt
```

通过目录扫描与 CLI 参数分析：
```bash
python tools/offline_analysis/profuzzbench_analyze_offline.py \
    -d ./synthetic_dataset \
    --subject lightftp \
    --fuzzers stateafl \
    --runs 1,2,9,10,11 \
    --start-time 1700000000.0 \
    --threshold b_abs=80 \
    --threshold n_edges=100 \
    -o ./analysis_report.json
```

若实验开始时间缺失：
```bash
python tools/offline_analysis/profuzzbench_analyze_offline.py \
    -d ./legacy_dataset \
    --subject lightftp \
    --fuzzers aflnet \
    --unknown-start-time \
    -o ./legacy_report.json
```

### 3. 运行自动化验收测试
```bash
python tools/offline_analysis/test_offline_analysis.py
```

---

## 四、声明与可信度约束

1. **测试数据性质**：本目录下 `generate_synthetic_test_data.py` 生成的数据仅用于离线分析工具的边界测试与鲁棒性验证，**绝不构成模糊测试效果评估证据**。
2. **三项机制说明**：本工具属于工程质量与实验分析评价治理（步骤 6），不代表 ChatAFL 论文的三项探索机制已在 StateAFL 上完成有效性验证。
