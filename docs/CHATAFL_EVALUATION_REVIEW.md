# ChatAFL 方法评价与结果可信度审查

日期、仓库快照、审计范围与证据索引见 [源码审计](CHATAFL_SOURCE_AUDIT.md) 和 [来源清单](CHATAFL_AUDIT_SOURCES.json)。本文是实验设计与现有数据处理审查；没有执行实验，也没有声称已验证论文方法。

## 评价对象

应将“保留 StateAFL/LLVM18 功能”和“LLM 方法带来收益”分成两个问题。前者需要插桩、共享内存状态、校准、输入表示和回放一致性的工程证据；后者需要固定基线上的重复对照和公平的时间、资源及成本口径。

ChatAFL 的现成版本采用基线→CL1→CL2→完整版的递增关系（`ChatAFL/README.md:20–22`）。它衡量相邻组合的条件增益。若要讨论三项机制各自的独立效果和交互作用，应预先登记因子设计与分析计划；不能仅从该递增关系推导这些结论。

## 共同基线与指标

每个对照应固定服务源码版本、系统工具链、编译及插桩选项、原始语料摘要、字典、随机种子安排、超时、状态推断与校准参数、资源预算和清理条件。重复使用准备产物或缓存时要记录来源，不能使某组享有未计入的准备预算。

主要覆盖率指标宜在同一源码版本、同一独立覆盖率构建上统计；当前 artifact 已有单独 gcov 构建的语料回放收集路径（`ChatAFL/benchmark/subjects/FTP/LightFTP/run.sh:33–44`）。不同 fuzzer 的内部 bitmap 数值不适合直接比较。

| 指标 | 可以回答的问题 | 解释限制 |
|---|---|---|
| 统一源码分支覆盖率及其时间曲线 | 在相同预算下实际覆盖多少代码 | 与 bitmap 边数、协议状态边数区分 |
| 同一 StateAFL 配置内的状态/转移数 | 内存状态探索是否变化 | 需要重复性证据；不能等同于响应码状态数 |
| 状态反馈稳定性、校准行为 | 原有状态推断是否仍可靠 | 状态编号增多也可能来自噪声 |
| 目标执行次数与有效执行时间 | 吞吐变化如何解释 | 不能排除 LLM 等待后单独宣称墙钟加速 |
| 语料规模及消息类型多样性 | 试验起点和输入分布是否改变 | 更多文件不等于更多覆盖 |
| LLM 等待、失败、重试、token 与费用 | 收益对应多少外部成本 | 字节截断不是 token 预算的可靠证明 |
| 运行失败与日志缺失率 | 结果是否存在幸存者偏差 | 不能静默剔除失败 run |

主要时间轴应从整个试验开始计入准备、请求等待、失败重试与退避。可另列从目标开始执行计算的曲线，但必须明确标识，避免混用。预先生成的语料与语法也有成本，缓存回放只能证明固定数据的重复性，不能代替在线模型表现。

模型标识、生成配置、请求/响应摘要、缓存命中、延迟、尝试次数及失败原因应随试验保存。源码里的模型字符串与历史限流描述仅反映当时实现（`ChatAFL/ChatAFL/chat-llm.c:65–69`；`ChatAFL/README.md:177–179`），本轮没有验证当前接口或模型是否可用。

## 现有分析脚本的具体风险

以下依据静态阅读，尚未运行复现；“可能产生偏差”不表示已经证明论文结果有误。

| 编号 | 问题 | 代码证据 | 可能影响 |
|---|---|---|---|
| E01 | 各 run 首条记录作为时间零点 | `ChatAFL/benchmark/scripts/analysis/profuzzbench_plot.py:39–44`；`ChatAFL/benchmark/scripts/analysis/profuzzbench_state.py:36–41` | 初始化时间可能从比较时间轴中消失 |
| E02 | 广泛捕获异常后跳过 run，并按成功读取数量求平均 | `ChatAFL/benchmark/scripts/analysis/profuzzbench_plot.py:49–53`；`ChatAFL/benchmark/scripts/analysis/profuzzbench_state.py:46–50` | 故障与缺失数据可能产生幸存者偏差 |
| E03 | 状态转移数纵轴固定 0–100 | `ChatAFL/benchmark/scripts/analysis/profuzzbench_state.py:68–73` | 真实增长超过 100 时图形会截断 |
| E04 | 固定读取第 12/13 列状态数据 | `ChatAFL/benchmark/scripts/analysis/profuzzbench_generate_csv.sh:65–69` | 目标项目日志 schema 不同时可能读错字段 |
| E05 | run 编号用字典序倒序取最大值 | `ChatAFL/benchmark/scripts/analysis/profuzzbench_generate_all.sh:31–32` | 编号达到 10 以上时可能漏掉运行记录 |
| E06 | 容器执行结果未保留退出状态，收集失败也无严格终止 | `ChatAFL/benchmark/scripts/execution/profuzzbench_exec_common.sh:33`、`:41`、`:49` | 完成提示不能证明运行和归档成功 |
| E07 | 覆盖率时间依赖语料文件 mtime | `ChatAFL/benchmark/subjects/FTP/LightFTP/cov_script.sh:40–42`、`:62–63` | 复制、归档或生成阶段改变时间戳可能改变曲线 |
| E08 | CPU 配额不等于 CPU 独占 | `ChatAFL/benchmark/scripts/execution/profuzzbench_exec_common.sh:21` | 主机竞争仍可能混杂实验；请求限流需另行记录 |

审查结果时应保留计划的全部 run 及状态，明确提前退出、服务不可用、LLM 失败与日志缺失的处理政策。按表头和 schema 核实数据含义，显示完整纵轴范围；从每个 run 的原始曲线到最终图表应可追溯。

## 重复试验与统计解释

按目标分别报告终点覆盖率、时间曲线及其积分、达到预先指定覆盖阈值的时间、执行次数、状态稳定性、失败率和成本。保留每个 run 的原始数据，报告中位数、分布、置信区间及适当效应量。没有达到阈值的运行要明确处理，不要用最后一次采样代替达到时间。

统计单元是独立重复试验，每分钟采样点不是额外独立样本。不要用跨协议的单个平均百分比掩盖目标间差异；相邻消融的百分比差与百分点差也应区分。若需要声称某一机制有效，必须说明对照基线、条件、预算、估计范围和不确定性。

## 当前 LLVM18 证据的用途

`LLM-CLOFuzz/tests/llvm18/run_all.sh:13–33` 展示了现有 IR、Pass 边界、wrapper、运行记录、I/O、状态反馈与有限时检查的覆盖范围。历史归档在 `LLM-CLOFuzz/tests/llvm18/evidence/system-toolchain/README.md:19–23`，不能作为本轮运行结果或 LLM 收益证据。

旧记录还明确保留了将 TCP 接收分块误当作消息导致失败、随后修正 fixture 分帧的历史（`LLM-CLOFuzz/tests/llvm18/evidence/README.md:11–14`）。因此，传输分片一致性与状态重复性必须独立核实，避免把传输层行为误认为新的协议状态。

本轮没有新增测量数据。迁移功能状态为“未实现”，效果状态为“未验证”；后续结果应继续使用这类明确状态，直到出现对应的工程及统计证据。
