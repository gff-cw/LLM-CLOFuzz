# ChatAFL 论文与源码审计

审计日期：2026-10-28。目标基线：`41400acbe415043859045c1bfacdf5611b747141`；ChatAFL 快照：`dcf109e18a423e9e27766b4bf8292349db05f49e`。

本轮完成论文阅读、源码静态审计、兼容性分析和评价设计。**三项功能尚未迁移，也没有完成有效性实验。** 本轮没有修改 C 源码、编译器插件、构建规则、运行配置或已有测试证据，没有运行 fuzzer、协议服务、benchmark 或 LLM 请求。源码中的风险尚未动态复现，不能据此断言可利用性。

工作由主审计与三个并行审计任务共同完成，分别覆盖 ChatAFL 实现、StateAFL/LLVM18 兼容性和实验评价。所有本地文件操作均限于用户指定的 `GPTFuzz` 目录。论文从作者公开页面读取。

文中 `ChatAFL/...` 和 `LLM-CLOFuzz/...` 均以该工作目录为根；行号针对上面的固定提交。文件摘要和行数见 [来源清单](CHATAFL_AUDIT_SOURCES.json)，实验评价另见 [评价说明](CHATAFL_EVALUATION_REVIEW.md)。

## 论文依据及结论边界

Meng、Mirchev、Böhme、Roychoudhury 的 [Large Language Model guided Protocol Fuzzing，NDSS 2024](https://mengrj.github.io/pdfs/chatafl.pdf) 中，IV-A、IV-B、IV-C 分别讨论语法辅助变异、初始语料丰富、覆盖率停滞干预；V–VI 说明实验与消融，附录描述缩小的复现实验。

论文的目标是公开规范的文本协议；正文评价覆盖六个服务实现，采用十次、每次二十四小时的重复试验。其消融是逐层启用三项策略。AFLNet 上的结果不能直接证明它们在使用内存状态推断的 StateAFL 上同样有效。这是需要另行验证的假设。[论文 V–VI、附录](https://mengrj.github.io/pdfs/chatafl.pdf)

以下具体实现结论独立依据本地代码得出，区分论文思路、该代码快照的实际行为和未验证的迁移假设。

## 三项机制的实际含义

| 机制 | 本地实现的概念行为 | 需要纠正的理解 | 源码证据 |
|---|---|---|---|
| 语法辅助变异 | 启动时向 LLM 获取模板，再用模板标记普通随机变异的范围 | 不是每次变异都向 LLM 询问字段和值；没有匹配模板时可以退化为整段可变 | `ChatAFL/ChatAFL/afl-fuzz.c:434`、`:589–594`、`:8166–8183` |
| 初始语料丰富 | 使用已有消息序列和消息类型信息增加语料多样性 | 扩展的是已有序列；“生成了文本”并不证明其能被具体服务接受 | `ChatAFL/ChatAFL/afl-fuzz.c:531–536`、`:2627–2767` |
| 停滞干预 | 无进展时，结合已有交互历史请求后续消息建议 | 无进展计数来自 `save_if_interesting` 的结果，不能直接等同于内存新状态计数 | `ChatAFL/ChatAFL/afl-fuzz.c:6293–6301`；`ChatAFL/ChatAFL/chat-llm.c:150–165` |

初始语料丰富路径有两个容易误读之处：`enrich_testcases` 中独立询问消息类型的代码已被注释（`ChatAFL/ChatAFL/afl-fuzz.c:2775–2780`）；实际类型集合来自语法处理。`MAX_ENRICHMENT_CORPUS_SIZE` 在 `:2708` 限制每条种子的候选缺失消息类型数量，不能按名称解释成输入文件总数上限。

扩展结果直接写入种子输入目录（`ChatAFL/ChatAFL/afl-fuzz.c:2743–2755`）。评价时必须保留原始语料的只读基线与摘要，否则一次准备工作会影响后续试验的起点。

## 消融版本不能仅凭函数存在与否判断

以下指选择协议并进入对应协议模式时的配置；语法与语料准备调用受 `protocol_selected` 条件控制（`ChatAFL/ChatAFL/afl-fuzz.c:10681`）。

| 源码版本 | 语法辅助变异 | 初始语料丰富 | 停滞干预 |
|---|---|---|---|
| `ChatAFL/ChatAFL-CL1` | 开启 | 关闭 | 关闭 |
| `ChatAFL/ChatAFL-CL2` | 开启 | 开启 | 关闭 |
| `ChatAFL/ChatAFL` | 开启 | 开启 | 开启 |

依据为 `ChatAFL/README.md:20–22`，并已核对实际源码：CL1 在 `ChatAFL/ChatAFL-CL1/afl-fuzz.c:10687` 注释语料丰富调用；停滞功能通过各版本 `config.h:77` 的 `CHATTING_THRESHOLD` 控制，完整版为 64，CL1/CL2 为 0。三个版本的 `chat-llm.c/.h` 内容相同，CL2 与完整版的 `afl-fuzz.c` 仅有空白差异，行为差异依赖配置。

这组对照衡量“在已经开启前一项的基础上，再增加一项”的条件增益；它没有单独估计后两项的独立效应与所有交互作用。

## 与 LLM-CLOFuzz 的兼容性约束

### 状态语义

LLM-CLOFuzz 的核心是长期内存快照、TLSH/MVPTree 与共享内存状态反馈（`LLM-CLOFuzz/README.md:5–13`；`LLM-CLOFuzz/afl-fuzz.c:2955–2962`）。提供 `-P` 时，响应解析路径仍使用 StateAFL tracer，原协议解析器留作交叉核对（`:10344–10348`）。

因此，LLM 的协议语义判断只能被视为不保证正确的外部信息，不能作为替换 StateAFL 内存状态的依据。响应码状态数、内存聚类状态数和语义协议状态数是不同指标，不能比较绝对值便声称改善。

### 文件表示、消息边界与所有权

初始输入使用四字节长度前缀的 replay 格式（`LLM-CLOFuzz/afl-fuzz.c:3030–3054`）；读取后转成 raw 队列文件（`:4824–4827`）。`region_t` 的闭区间偏移指向 raw 数据（`LLM-CLOFuzz/aflnet.h:9–15`）。文本串、replay 文件、raw 队列以及消息边界不能混用。TCP 的一次收发也不等于一个协议消息。

`message_t` 只有数据与长度字段（`LLM-CLOFuzz/aflnet.h:17–20`）；显式释放消息数据和对象的逻辑在 `LLM-CLOFuzz/aflnet.c:1535–1550`，链表默认析构宏为空（`LLM-CLOFuzz/aflnet.h:48–49`）。`region_t` 还包含状态序列指针，代码存在结构体浅拷贝（`LLM-CLOFuzz/afl-fuzz.c:3021–3022`）。审查任何附加元数据时，都要区分借用、转移所有权与独立副本，防止泄漏、重复释放或悬空引用。

### 执行生命周期与进展指标

现有默认双 forkserver 分别承担覆盖率与状态分析（`LLM-CLOFuzz/afl-fuzz.c:10516–10528`）。每次执行前重置状态序列，状态分析具有额外等待（`:1492–1506`）；状态复验还保存和恢复全局响应缓冲（`:830–930`）。外部等待若混入目标执行计时，会同时影响吞吐、超时和重复性判断。

队列保留先依据覆盖率新位，再进行状态复验（`LLM-CLOFuzz/afl-fuzz.c:4945–4975`）。新增队列、稳定的新内存状态和有趣输入不是同一个事件；仅统计队列文件数无法验证三个假设。

### LLVM18 已有成果

现有构建使用系统工具链并校验 LLVM/Clang 主版本，保留 StateAFL 插件和 C/C++ tracer（`LLM-CLOFuzz/llvm_mode/Makefile:27–30`、`:94–116`、`:130–148`）。ChatAFL 的根构建另外依赖 libcurl、json-c、PCRE2（`ChatAFL/ChatAFL/Makefile:72–73`）；构建依赖可见并不等于 ABI、状态推断或输入语义兼容。

已有测试对共享结构大小、状态复用、分叉和校准作了断言（`LLM-CLOFuzz/tests/llvm18/state_feedback.py:12–21`、`:78–101`）。现有系统工具链归档记录 IR 21/21、Pass 边界 11/11、功能 40/40、七轮状态反馈通过；45 秒检查记录 443 次执行、13 个队列条目、状态 0–8、稳定性 100%（`LLM-CLOFuzz/tests/llvm18/evidence/system-toolchain/README.md:19–23`）。**这些是历史记录，本轮未重跑，更不能视为 LLM 功能效果证据。**

## ChatAFL 的 C 代码可靠性发现

以下全部是静态判断，没有提供触发载荷，也没有验证可利用性。需要先用边界明确、完全离线的数据处理测试核实；不能把原实现不加审查地视为可复用组件。

| 编号 | 发现 | 证据 | 需要保证的性质 |
|---|---|---|---|
| C01 | 模板转正则时，固定数组接受无容量约束的拼接 | `ChatAFL/ChatAFL/chat-llm.c:409–455`、`:465–466`、`:496–536` | 输出增长有上限、溢出可诊断；不依赖 LLM 自觉限制长度 |
| C02 | 短串与裁剪为空的字符串存在向前越界读取路径 | `ChatAFL/ChatAFL/chat-llm.c:742`、`:763` | 长度为零和短于尾部检查长度时仍定义良好 |
| C03 | JSON 响应字段存在不等于类型、数组长度和字符串均有效 | `ChatAFL/ChatAFL/chat-llm.c:95–118` | 每层类型及空值经过校验后才能解引用 |
| C04 | 回调无响应容量上限与尺寸运算保护，直接覆盖 `realloc` 原指针 | `ChatAFL/ChatAFL/chat-llm.c:27–40` | 内存增长受限；分配失败保留原所有权并可靠清理 |
| C05 | 请求未配置连接/总超时，未区分传输成功与 HTTP 成功 | `ChatAFL/ChatAFL/chat-llm.c:87–93` | 失败可归因、等待有界，实验时间完整记录 |
| C06 | 语法 JSON 解析结果未经验证便入表，后续首项直接用作键 | `ChatAFL/ChatAFL/chat-llm.c:379–404`；`ChatAFL/ChatAFL/afl-fuzz.c:478–484` | 非法数据被拒绝，不污染有效模板与类型集合 |
| C07 | 哈希键借用 JSON 字符串，但列表默认不析构 JSON；简单补释放可能产生悬空键 | `ChatAFL/ChatAFL/chat-llm.h:46`；`ChatAFL/ChatAFL/afl-fuzz.c:484–495` | 键与 JSON 的寿命关系明确，整体释放顺序可证明 |
| C08 | 临时解析缓冲及语料丰富路径存在清理缺口 | `ChatAFL/ChatAFL/chat-llm.c:394–401`；`ChatAFL/ChatAFL/afl-fuzz.c:2627–2767` | 正常、失败和提前退出路径均释放所属资源 |
| C09 | 哈希查找结果在未检查结束哨兵前用于 `kh_exist` | `ChatAFL/ChatAFL/afl-fuzz.c:2690–2691`；`ChatAFL/ChatAFL/khash.h:230–242`、`:499` | 不把“未找到”当作有效桶位置 |
| C10 | 被称为 token 预算的限制实际按字符串长度裁剪 | `ChatAFL/ChatAFL/chat-llm.c:958–964`；`ChatAFL/ChatAFL/afl-fuzz.c:6920–6944` | 字节、字符与 token 指标分开；转义及编码仍完整 |

## LLM-CLOFuzz 既有可靠性风险

这些问题不是本轮引入，也不代表 LLVM18 迁移失败。本轮记录它们，是为了避免在更长消息和更多外部数据的讨论中忽略原有容量、表示和文件一致性约束。

| 编号 | 静态发现 | 证据 |
|---|---|---|
| S01 | `shmat` 使用空指针检查，不能识别其失败哨兵 | `LLM-CLOFuzz/afl-fuzz.c:2747–2775` |
| S02 | 固定状态序列容量与相关读写未见完整局部边界保护 | `LLM-CLOFuzz/llvm_mode/state-tracer.h:18–26`；`LLM-CLOFuzz/llvm_mode/afl-llvm-rt-state-tracer.o.c:1678–1683`、`:1800–1806`；`LLM-CLOFuzz/afl-fuzz.c:2957–2962` |
| S03 | 校准参考/距离数组的写入边界和 `<= ref_len` 条件需要核查 | `LLM-CLOFuzz/llvm_mode/state-tracer.h:31–49`；`LLM-CLOFuzz/llvm_mode/afl-llvm-rt-state-tracer.o.c:1363–1381` |
| S04 | replay 长度与偏移相加后再比较，需要覆盖溢出、零长和偏移表示 | `LLM-CLOFuzz/afl-fuzz.c:3032–3054` |
| S05 | 输出重写未带 `O_TRUNC`，较短内容覆盖已有文件可能遗留尾部 | `LLM-CLOFuzz/aflnet.c:1570` |

## 本轮交付与尚待验证事项

已交付的是基于固定快照的论文理解、源码证据、可靠性风险和实验评价约束。后续可独立开展离线数据处理可靠性验证、既有 LLVM18 证据完整性核查和实验结果统计审查。

尚未交付三项自动化协议模糊增强的实现、可运行迁移、在线服务兼容性确认或收益结论；本报告也不包含将这些能力接入目标的代码接线方案。自动协议输入生成、定向变异和自主交互增强超出本轮可提供的范围。不能使用“编译通过”“文档已推送”或既有短时测试，替代这些尚未完成的功能与有效性验证。
