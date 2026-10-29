# LLM-CLOFuzz / StateAFL / AFLNet 格式契约与数据表示规范

## 1. 规范背景与范围

在 LLM-CLOFuzz 对协议消息进行 LLM 增强与变异的过程中，存在多种不同的数据表示层次：
- **Replay 格式文件**：AFLNet / StateAFL 的测试用例保存格式，每条消息前面带有 4 字节的长度前缀（`uint32_t`，本机字节序，x86_64 上为小端）。
- **Raw 字节流**：进入 Fuzz 队列或由解析器解包后的原始数据，移除了 4 字节的 replay 封装。
- **Region 元数据**：用于切分各条消息的坐标结构（`region_t`），记录闭区间 `[start_byte, end_byte]`。
- **网络 Payload**：发往目标服务的实际应用层报文，与 replay 封装完全隔离。

为避免在不同组件间混淆上述表示，本规范固定数据结构、编解码规则及生命周期管理契约。

---

## 2. 数据对象契约

### 2.1 Replay 容器格式 (Replay Container)
- **长度字段**：每条消息前置 4 字节无符号整数（`uint32_t` / `u32`），采用本机字节序（Little-Endian）。
- **Payload 字节**：长度字段后紧跟实际二进制字节，长度完全由前置字段指定，支持任意二进制数据（含 `\0`、不可打印字符及非 UTF-8 序列）。
- **合法性判定**：
  - 合法容器的总字节数必须精确等于：`∑ (4 + len_i)`。
  - 末尾不允许有多余未解释的尾部字节（trailing garbage）。
  - 若文件剩余字节小于 4 字节，或声明长度超过文件剩余字节，判定为截断格式错误。
- **零长消息策略**：支持零长记录（长度为 0，紧跟 0 字节 payload），其闭区间坐标表示为 `end_byte = start_byte - 1`。

### 2.2 Raw 缓冲 (Raw Buffer)
- **有效边界**：必须具有显式的 `length` 和 `capacity`，不能依赖 NUL 终止符计算长度。
- **不可逆性声明**：多个消息拼接后的 Raw 缓冲无法在无外部元数据的情况下唯一反推消息边界。必须由外部 `region_t` 元数据显式记录划分。

### 2.3 区域元数据 (Region Metadata)
- **定义**：`region_t { int start_byte; int end_byte; ... }`
- **坐标系**：闭区间 `[start_byte, end_byte]`。当长度为 `L` 时，`end_byte = start_byte + L - 1`；当 `L = 0` 时，`end_byte = start_byte - 1`。
- **缓冲关联性**：区域坐标必须严格绑定到其所属的具体缓冲对象及其版本/哈希；任何对缓冲的扩容、重分配、数据修改均会导致旧区域元数据失效，禁止跨缓冲或跨轮次无条件复用。

---

## 3. 生产者与消费者审计表

| 模块 / 函数 | 源码位置 | 输入表示 | 输出表示 | 坐标基准 | 长度/截断策略 | 所有权与资源管理 |
|---|---|---|---|---|---|---|
| `read_testcases` | `afl-fuzz.c:2329` | Replay 文件 (带 4B 头) | 内存 Replay 缓冲与 Raw 区域 | Raw (由解包逻辑决定) | 严格按 4B 头切分，非法头报 FATAL | 队列拥有缓冲，生命周期随 testcase |
| `extract_requests_generic` | `afl-fuzz.c:3215` | 输入字节缓冲 (Replay/Raw) | `region_t*` 数组 | Raw 坐标序列 | 安全减法防溢出，支持零长 | 调用方负责 `ck_free(regions)` |
| `enrich_testcases` | `afl-fuzz.c:3340` | Initial seed (Replay 格式) | 抽取各消息并重组 | 原实现错用 Raw 索引 Replay 缓冲 | 原实现转为 C 字符串导致截断 | 临时缓冲，函数退出释放 |
| `write_new_seeds` | `chat-llm.c:998` | 抽取格式化后的字符串 | 生成文件 | 纯文本 | 原实现未加 4B 封装，破坏 Replay 格式 | 文件句柄在写入后关闭 |
| `chat_with_llm` | `chat-llm.c:251` | JSON/文本 Prompt | 模型响应字符串 | N/A | 受 `max_tokens` 限制 | 堆分配，调用方释放返回指针 |
| `save_responses_to_file` | `afl-fuzz.c:9630` | 内存响应数据 | 文件 | N/A | 二进制写入 | 临时文件记录 |

---

## 4. 核心问题复核结论 (F2-A ~ F3-F)

### 问题 2：表示不一致复核 (F2-A ~ F2-F)
- **F2-A (`afl-fuzz.c:3341`)**：确认存在。直接读取 initial seed 文件后按字符串处理，未解开 4 字节 replay 封装。
- **F2-B (`afl-fuzz.c:3233`)**：确认存在。`extract_requests_generic` 计算的 `cur_start` 是 Raw 累加坐标，未对输入缓冲做就地解包。
- **F2-C (`afl-fuzz.c:3353`)**：确认存在。用 Raw 坐标索引仍包含 4 字节长度头的 `nl_file_content`，造成数据错位。
- **F2-D (`chat-llm.c:1230`)**：确认存在。将可能包含 NUL 字节的二进制数据视作 C 字符串，遇到 0x00 提前截断。
- **F2-E (`chat-llm.c:998`)**：确认存在。`write_new_seeds` 以纯字符串写入，缺少 4 字节 replay 封装头，导致 AFLNet 后续读取失败。
- **F2-F (`afl-fuzz.c:11235`)**：确认存在。AFLNet 主循环读取期望严格的 replay 封装，写入纯文本种子导致不兼容。

### 问题 3：元数据关联缺口复核 (F3-A ~ F3-F)
- **F3-A (`afl-fuzz.c:11081`)**：确认存在。运行期解包函数被重定向为 generic。
- **F3-B (`afl-fuzz.c:3222`)**：确认存在。仅凭全局指针 `mutated_regions != NULL` 且数量大于 0 即直接复用，未校验与当前输入缓冲的哈希或地址关联。
- **F3-C (`afl-fuzz.c:3235`)**：确认存在。缺少全局区域时，输入被假设为具有 4 字节长度前缀。
- **F3-D (`afl-fuzz.c:7614`)**：确认存在。在 LLM 变异分支处理字符串时，未维护回放封装和变异区域。
- **F3-E (`afl-fuzz.c:7705`)**：确认存在。变异区域分配在分支逻辑之后。
- **F3-F (`afl-fuzz.c:9611`)**：确认存在。正常轮次结束释放区域指针并置空。
