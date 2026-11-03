# 当前输入格式与恢复契约

本文件描述 `fix/replay-recovery-integrity-20261103` 的原生接入行为。旧版文档中的缺陷编号是历史分析，不代表当前缺陷状态；本文件替代原先按旧行号排列的结论。

## 文件与内存表示

- Replay 文件使用 AFLNet/StateAFL 原有格式：每条消息由本机字节序的 4 字节 `u32` 长度和对应 payload 组成。没有新增魔数或更换格式。x86_64 上长度是小端。
- 输出 `queue` 保存拼接后的 raw payload；输出 `replayable-queue` 保存 payload 和完整消息划分。
- `region_t` 使用闭区间 `[start_byte, end_byte]`。空消息使用 `end_byte = start_byte - 1`，连续空消息各保留一个 region。
- 多条消息拼接后的 raw 字节不能唯一确定边界。恢复必须同时有有效 replay，或由用户明确选择重新解析 raw。
- `.regions` 是供调试查看的文本，不作为自动恢复的权威输入。仅校验几何和长度不能证明它属于当前 payload。

## 输入方式

| 选项或路径 | 行为 |
| --- | --- |
| `-I replay` | 所有初始种子必须完整符合 replay 格式，非法或截断文件报错 |
| `-I raw` | 所有初始文件按 raw 解释；使用协议解析器或单消息回退 |
| `-I auto`，父目录 `queue` / `_resume` | 按 raw 恢复；要求至少一个解码后与 raw 逐字节相同的 replay companion |
| `-I auto`，父目录 `replayable-queue` / `_resume_replayable-queue` | 严格按 replay 解码，不将输入自身误认为 raw 的伴随文件 |
| `-I auto`，其他初始目录 | 保留旧版自动探测：完整符合 replay 则解包，否则按 raw |

`auto` 在无类型的初始目录仍有固有歧义：某段 raw 恰好符合 replay 字节结构时会被解包。已知格式的实验应显式指定 `-I raw` 或 `-I replay`。启动时规范化输入目录路径，消除尾部斜杠、`.`、`..` 和目录软链接对目录角色判断的影响。

导入以前的输出根目录时，程序先选择其 `queue`。若要直接导入 replay，应指定 `-i OLD/replayable-queue`。`-i -` 的常规恢复使用默认 `auto`，不要将已有 raw queue 声明为 `replay`。

## 伴随文件与边界优先级

对 raw queue 的每个文件，依次尝试同名的 `../_resume_replayable-queue`、`../replayable-queue` 和 `replayable-queue` 候选。排除与输入同 inode 的文件。每个候选都必须完整解码，并同时匹配 payload 长度和每个 payload 字节。损坏、过期或超限的高优先级候选不会遮住后续有效候选。

验证通过的 replay 边界优先于协议解析器，因此 `-P` 不会重新划分该种子并丢失空记录。输出 replay 总是由实际 raw 和最终 regions 重新生成，不直接复制未验证的旧文件。

字节相等建立的是 payload 与边界的兼容性，不是历史版本认证。两份 replay 若 payload 相同而消息划分不同，raw 本身不能判定原本是哪一份；需要指定保留的 replay 输入。此补丁不新增签名、版本清单或跨机器字节序转换。

旧语料只有 raw 和 `.regions` 时，自动恢复会拒绝并保留原件。建议找回对应 replay；如果愿意重新划分消息，可将 raw 文件复制到独立初始目录并显式 `-I raw`。这会依赖协议解析器或单消息回退，无法承诺恢复原边界。

## 文件边界与失败行为

payload 总长限制为 `MAX_FILE`（当前 1 MiB）；replay 最多 `MAX_FILE` 条记录，总文件上限为 `MAX_FILE + 4 * MAX_FILE`（当前 5 MiB）。读取前检查文件类型和宽整数文件尺寸，之后才转换为 `u32` 并分配；解码也单独限制总 payload 和记录数。

恢复写入使用同目录临时文件，处理短写和 `EINTR`，检查 `fsync`、`close` 和 `rename`。写入失败不会替换已有目标文件；临时文件会清理。无法迁移原目录、验证输入或保存任一输出时，启动在清理 `_resume*` 前停止，原恢复数据保留。已有备份目录即使为空，也不会被新目录覆盖。

如果清理被中断，可能出现 `_resume` 已不存在、却仍有旧 companion 备份的情况。下次恢复会在任何目录移动之前拒绝这个状态，避免将旧边界配给新 raw 并删除当前匹配的 companion；保留这些目录后再人工整理备份。

这些是运行时 I/O 错误保证；没有对整个输出目录实现跨文件事务或断电原子性。重新运行时会重新验证 companion。

## LLM 与网络层

初始扩充共用相同的受限输入解码器。启用严格 replay 输入，或直接导入 replayable 目录时，生成的种子也保存为 replay；显式 raw 模式保存 raw。自动恢复已有 queue 时跳过重复的初始扩充，不向恢复快照加入无 companion 的新文件。LLM 文本构造仍以 C 字符串为接口，不能据此宣称模型可无损生成任意二进制协议数据。

零长 UDP 消息实际发送一条零长数据报；超时和发送错误返回失败。普通 UDP 消息也保持单个数据报，不将短发送拆成另一条消息。零长 TCP 记录作为逻辑槽位保留，但 TCP 没有独立可观察的空消息，网络层不发送字节。

本次未改变 StateAFL 的 LLVM 18 插桩接口、状态推断算法或 replay 的二进制布局。验证命令与覆盖范围见 [修复验证报告](REPAIR_VERIFICATION_20261103.md)。
