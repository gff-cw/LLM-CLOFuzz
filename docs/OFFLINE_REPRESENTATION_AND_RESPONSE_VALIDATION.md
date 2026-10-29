# LLM-CLOFuzz 离线文件格式兼容性、所有权模型与保存响应校验规范

本文档针对 `LLM-CLOFuzz` 项目在融合协议语法与外部生成模型过程中的文件格式、数据边界、内存所有权与离线校验工具进行系统性规范与设计。对应 [CHATAFL_RESEARCH_HANDOFF.md](file:///D:/1.工控协议/LLM-CLOFuzz/LLM-CLOFuzz/docs/CHATAFL_RESEARCH_HANDOFF.md) 中的**步骤 3（所有权明确）、步骤 4（离线表示校验）与步骤 5（保存模型响应校验）**。

---

## 一、三种输入表示的结构定义与消息边界

在基于 StateAFL 与 AFLNet 的工控/网络协议模糊测试架构中，存在三种核心数据表示形态：`replay`、`raw` 与 `region`。各表示的物理存储结构与语法语义如下：

### 1. Replay 表示（Serialized Replay Stream）
* **存储结构**：由多个连续序列化的数据包组成。每个数据包由**4 字节无符号小端整数（`uint32_t`，`<I`）长度前缀**后紧跟**该长度对应字节数的消息负载（Payload）**构成。
  $$\text{Replay} = \left[ L_0 (4\text{B}) \parallel \text{Payload}_0 (L_0\text{B}) \right] \parallel \left[ L_1 (4\text{B}) \parallel \text{Payload}_1 (L_1\text{B}) \right] \parallel \dots \parallel \left[ L_{N-1} (4\text{B}) \parallel \text{Payload}_{N-1} (L_{N-1}\text{B}) \right]$$
* **代码依据**：`convert-pcap-replay-format.py:98-99`、`afl-fuzz.c:3036`、`afl-replay.c`。
* **业务用途**：作为网络回放工具（`aflnet-replay`）和模糊测试初始种子（Seed）的持久化格式，以便在 TCP/UDP 交互中按包还原发送序列。

### 2. Raw 表示（Concatenated Raw Payload Stream）
* **存储结构**：所有消息负载的纯数据流级联，**剥离所有 4 字节长度前缀**。
  $$\text{Raw} = \text{Payload}_0 \parallel \text{Payload}_1 \parallel \dots \parallel \text{Payload}_{N-1}$$
  $$\text{Total Raw Length} = \sum_{i=0}^{N-1} L_i$$
* **代码依据**：`afl-fuzz.c:4696–4736`（`convert_from_replay_to_raw`）、`:4824`。
* **业务用途**：AFL 变异引擎对队列文件（`q->fname`）进行字节级变异时的实际操作对象。

### 3. Region 表示（Boundary Closed Interval Mapping）
* **内存结构**：定义于 `aflnet.h:9–15` 的 `region_t` 结构体：
  ```c
  typedef struct {
    int start_byte;                 /* 消息起始字节偏移（闭区间），未知时为负数 */
    int end_byte;                   /* 消息末尾字节偏移（闭区间），未知时为负数 */
    char modifiable;                /* 是否允许被变异修改 */
    unsigned int *state_sequence;   /* 状态反馈序列（用于状态感知调度） */
    unsigned int state_count;       /* 状态序列中的状态数量 */
  } region_t;
  ```
* **语法语义**：`[start_byte, end_byte]` 为**闭区间（双端包含）**，严格指向 `raw` 字节流中的切片偏移：
  - 第 0 条消息：`start_byte = 0`, `end_byte = L_0 - 1`；
  - 第 $i$ 条消息（$i \ge 1$）：`start_byte = regions[i-1].end_byte + 1`, `end_byte = start_byte + L_i - 1`；
  - 区间长度公式：$$\text{Length} = \text{end\_byte} - \text{start\_byte} + 1 = L_i$$；
  - 覆盖完整性：所有 region 连续排列、无缝隙、无重叠，且最后一项 `regions[N-1].end_byte == len(raw) - 1`。
* **代码依据**：`afl-fuzz.c:3048–3053`。

---

## 二、内存所有权与生命周期矩阵（针对步骤 3）

针对源码审计中发现的内存隐患（例如 `aflnet.h:48-49` 空析构宏泄漏、`afl-fuzz.c:3021-3022` 浅拷贝、`aflnet.c:1570` 覆盖截断风险），建立明确的所有权矩阵：

| 实体对象 | 分配者（Creator） | 持有者（Owner） | 借用者（Borrower） | 释放者（Deallocator） | 失败/异常清理责任 |
|---|---|---|---|---|---|
| **Replay 文件** | 抓包转换脚本 / 持久化生成器 | 磁盘文件系统 | `afl-fuzz` 初始化读取缓冲 | 运行清理脚本 | 检查文件完整性，失败时拒绝加入初始语料 |
| **Raw 队列文件** | `convert_from_replay_to_raw` | AFL 队列目录 | Fuzz 执行子进程 | AFL 队列轮换/清理 | 打开必须使用 `O_TRUNC` 或先 `unlink`（防 S05 尾部残留） |
| **Replay 内存缓冲** | `read_file` / `mmap` | 调用栈帧局部变量 | `get_regions_from_input` | 函数退出前 `ck_free` | 任意长度解析失败前立即释放局部缓冲 |
| **`region_t` 数组** | `ck_alloc` / `ck_realloc` | `queue_entry` | 变异选择算子 | 队列条目销毁函数 | 释放时必须先释放非空的 `state_sequence`，再释放数组 |
| **`state_sequence`** | 状态分析器（Tracer） | 所属 `region_t` 结构体 | 调度打分函数 | 随 `region_t` 显式销毁 | 禁止直接 `memcpy` 结构体导致悬空指针（浅拷贝隐患） |
| **`message_t` 链表** | `lms` 构造函数 | 消息流解析器 | 网络发送驱动 | `destroy_message_list` | 链表头析构宏必须递归 `free(mdata)` 和节点（修复 C08） |
| **Grammar JSON** | `json_tokener_parse` | 语法解析模块 | 模板匹配正则器 | `json_object_put` | 解析出错或字段缺失时提前释放，杜绝泄漏（修复 C03/C08） |
| **哈希表键值** | `strdup`（独立拷贝） | `const_table` | 正则匹配器 | `kh_destroy` 遍历释放 | 严禁借用即将被 put 销毁的 JSON 内部临时指针（修复 C07） |

---

## 三、离线表示校验工具设计 (`validate_representation.py`)

工具源码位于 [validate_representation.py](file:///D:/1.工控协议/LLM-CLOFuzz/LLM-CLOFuzz/tools/offline_validation/validate_representation.py)。

### 1. 核心校验逻辑与异常判定
1. **长度头截断检测（`TRUNCATED_HEADER`）**：
   当剩余字节数处于 `1 <= remaining < 4` 时，判定为长度前缀截断。
2. **过长声明检测（`OVERFLOW_LENGTH`）**：
   读取 4 字节 `msg_len` 后，检查 `msg_len > remaining_payload`，防止越界读取和内存超配。
3. **零长度消息检测（`ZERO_LENGTH_MESSAGE`）**：
   若 `msg_len == 0`，由于 `end_byte = start_byte + 0 - 1` 会导致无符号整数溢出回绕（`underflow`），且工控报文不能为空，判定为严重格式损坏。
4. **尾部残余字节检测（`TRAILING_GARBAGE`）**：
   按声明解析完所有报文后，若游标未完全抵达文件末尾，立即报错归因。
5. **空文件检测（`EMPTY_FILE`）**：
   默认拒绝 0 字节文件（除非显式开启 `--allow-empty`）。
6. **Raw 与 Region 逐字节一致性对比（`RAW_MISMATCH`）**：
   将提取出的每包 Payload 重新拼接为 Raw，逐字节校验其与参考 Raw 文件；同时验证所有 Region 切片与原始报文绝对等价。

### 2. 退出码映射表

| 退出码 | 标识符 | 触发场景与诊断输出 |
|:---:|---|---|
| **0** | `SUCCESS` | 校验通过，提取出合法消息、Raw 及闭区间 Region 列表 |
| **1** | `CLI_ERROR` | 命令行参数缺失、文件不存在或读取权限受阻 |
| **2** | `TRUNCATED_HEADER` | 报文末尾不足 4 字节，无法完整读取长度前缀 |
| **3** | `OVERFLOW_LENGTH` | 报文长度声明超出文件物理剩余容量 |
| **4** | `ZERO_LENGTH_MESSAGE` | 报文声明长度为 0，拒绝退化无效区间 |
| **5** | `TRAILING_GARBAGE` | 报文声明序列解析完成后存在未被涵盖的尾部残余 |
| **6** | `EMPTY_FILE` | 文件物理大小为 0（未带 `--allow-empty`） |
| **7** | `RAW_MISMATCH` | 生成 Raw 与参考文件存在字节不一致或长度不匹配 |
| **8** | `RESOURCE_EXCEEDED` | 文件大小或消息总数超出安全边界上限 |

---

## 四、离线已保存响应校验工具设计 (`validate_saved_responses.py`)

工具源码位于 [validate_saved_responses.py](file:///D:/1.工控协议/LLM-CLOFuzz/LLM-CLOFuzz/tools/offline_validation/validate_saved_responses.py)。

### 1. 防御的源码审计漏洞映射

本校验工具重点防范 [CHATAFL_SOURCE_AUDIT.md](file:///D:/1.工控协议/LLM-CLOFuzz/LLM-CLOFuzz/docs/CHATAFL_SOURCE_AUDIT.md) 记录的如下隐患：
* **C01 防御**：对语法模板（Grammar）中的每个字段长度强制设置上限（默认 4KB），防止超长字段导致正则转换固定数组（4096 字节）越界溢出。
* **C02 防御**：强制校验提取出的字符串非空，防止对空串解引用 `data[0] == '\n'` 导致向前越界读。
* **C03 防御**：逐层校验 `choices`、`message`、`content` 与 `text` 的类型及非空性，拒绝 `null` 值，防止解引用空指针。
* **C06 防御**：语法数组严格要求第 0 项为有效的消息头名称（字母数字及下划线，不超过 128 字符），后续各项必须为字符串，拒绝非字符串键污染一致性哈希表。
* **C10 防御**：检测尾部未闭合的反斜杠（`\`）、截断转义字符以及非法控制字符。
* **DoS 防御**：设置最大文件尺寸（10MB）、最大字符串长度（64KB）、最大嵌套深度（32 层）以及数组元素数量上限，杜绝畸形 JSON 导致递归栈溢出。

### 2. 支持的离线 Schema

1. `api_chat`：标准 OpenAI Chat-Completion 格式（包含 `choices[].message.content`）。
2. `api_completion`：传统 Text-Completion 格式（包含 `choices[].text`）。
3. `grammar`：ChatAFL 消息模板结构（支持模板数组 `[ ["HEADER", "field1", ...], ... ]` 或模板字典 `{"HEADER": [...]}`）。
4. `message_types`：大写协议消息类型列表（`["DESCRIBE", "SETUP", ...]` 或逗号分隔字符串）。
5. `stagnation`：停滞干预建议请求字符串或包含 `"message"`/`"request"` 的字典。
6. `auto`：根据根节点结构与键名自动识别上述 Schema。

### 3. 退出码映射表

| 退出码 | 标识符 | 触发场景 |
|:---:|---|---|
| **0** | `SUCCESS` | JSON 结构、数据类型、字符串长度及转义完全合规 |
| **1** | `CLI_ERROR` | 参数错误或文件无法访问 |
| **2** | `INVALID_JSON_SYNTAX` | 语法解析失败、未闭合符号、非法 UTF-8、嵌入 NUL 字符或损坏转义 |
| **3** | `SCHEMA_VIOLATION` | 缺少必需字段、类型不匹配（例如 `choices` 不是列表） |
| **4** | `NULL_VALUE_ERROR` | 关键字段（`choices`、`message`、`content`、字段项）为 `null` |
| **5** | `CONSTRAINT_VIOLATION` | 数组为空、消息头标识符非法、占位符语法不闭合 |
| **6** | `RESOURCE_EXCEEDED` | 字符串超限、文件过大或嵌套层级过深 |

---

## 五、自动化测试套件与执行证据

测试代码位于 [tests/offline_validation/](file:///D:/1.工控协议/LLM-CLOFuzz/LLM-CLOFuzz/tests/offline_validation/)。

### 1. 测试用例组成清单

```
tests/offline_validation/
├── fixtures/
│   ├── replay_normal/             # 单包、多包 RTSP、二进制载荷、边界报文
│   ├── replay_corrupted/          # 截断头、过长声明、零长报文、空文件、Raw 错位
│   ├── saved_responses_normal/    # ChatCompletion、Completion、Grammar、MessageTypes、Stagnation
│   └── saved_responses_corrupted/ # 语法错误、嵌入 NUL、缺少 choices、null 字段、畸形模板、深度超限
├── generate_fixtures.py           # 确定性样例生成脚本
├── test_validate_representation.py# 表示校验单元测试（16 个测试用例）
├── test_validate_saved_responses.py# 保存响应校验单元测试（30 个测试用例）
└── run_offline_validation_tests.py# 统一自动化测试运行器
```

### 2. 离线执行命令

在工作目录 `D:\1.工控协议\LLM-CLOFuzz\LLM-CLOFuzz` 下执行：

```powershell
# 统一测试套件
python tests/offline_validation/run_offline_validation_tests.py
```

### 3. 测试通过证据（Execution Evidence）

```text
========================================================================
 LLM-CLOFuzz Offline Validation Test Suite
========================================================================
[1/2] Ensuring test fixtures are generated...
      Done.
[2/2] Running offline validation test cases...
test_binary_payloads_normal ... ok
test_cli_failure_exit_code ... ok
test_cli_success ... ok
test_corrupted_empty_file ... ok
test_corrupted_overflow_length ... ok
test_corrupted_raw_mismatch_content ... ok
test_corrupted_raw_mismatch_length ... ok
test_corrupted_truncated_header ... ok
test_corrupted_truncated_mid_header ... ok
test_corrupted_zero_length_message ... ok
test_empty_file_allowed ... ok
test_existing_repository_live555_rtsp_replay ... ok
test_minimal_boundary_normal ... ok
test_multi_message_rtsp_normal ... ok
test_output_raw_and_regions_files ... ok
test_single_message_normal ... ok
test_chat_completion_rtsp_normal ... ok
test_cli_json_report ... ok
test_cli_saved_response_failure ... ok
test_cli_saved_response_success ... ok
test_completion_instruct_normal ... ok
test_grammar_empty_template ... ok
test_grammar_field_not_string ... ok
test_grammar_field_null ... ok
test_grammar_header_empty ... ok
test_grammar_header_not_string ... ok
test_grammar_http_dict_normal ... ok
test_grammar_mismatched_placeholder ... ok
test_grammar_rtsp_array_normal ... ok
test_message_types_list_normal ... ok
test_null_content ... ok
test_null_message ... ok
test_resource_deep_nesting ... ok
test_resource_string_too_long ... ok
test_schema_choice_not_dict ... ok
test_schema_choices_empty ... ok
test_schema_choices_not_list ... ok
test_schema_missing_choices ... ok
test_schema_missing_message ... ok
test_stagnation_intervention_normal ... ok
test_syntax_embedded_null_byte ... ok
test_syntax_escaped_null_byte ... ok
test_syntax_invalid_utf8 ... ok
test_syntax_trailing_backslash ... ok
test_syntax_unclosed_array ... ok
test_wrong_type_content ... ok
------------------------------------------------------------------------
Summary: 46/46 passed, 0 failed, 0 errors in 1.123s
RESULT: ALL OFFLINE VALIDATION TESTS PASSED [OK]
```
