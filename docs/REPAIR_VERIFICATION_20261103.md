# 恢复与原生验证修复记录（2026-11-03）

基线为 `engineering/chatafl-offline-fixes` 的 `42bf625fa1d264b24601e21bedd065b8d20fe3f6`。修改位于独立分支 `fix/replay-recovery-integrity-20261103`，不合并 `main`。本轮由三个 agent 分别实现原生恢复测试、网络与 prompt 修复、独立格式工具修复；主 agent 实现恢复接入与最终编译，另经独立 agent 复核恢复失败路径。

## 修改内容

| 已确认缺口 | 本次修改 | 直接验证 |
| --- | --- | --- |
| 直接导入 replayable-queue 被当成 raw | 区分目录角色，新增 `-I auto/raw/replay`，规范化输入目录和重复斜杠、`.` 路径 | 原生导入、严格格式拒绝及路径边界用例 |
| 同名同长旧 companion 可被接纳 | 完整解码后逐字节比较当前 raw，排除输入自身；逐个尝试有效候选 | 同长异内容拒绝、坏候选后找到有效候选 |
| `.regions` 无法证明 payload 身份 | 不再把旧文本作为恢复权威输入，删除不安全的文本恢复解析器 | 只有 regions / 损坏 replay 加 regions 均拒绝 |
| 无界读取独立 companion | 分配前限制文件类型、宽整数尺寸、总 payload、消息数；避免尺寸先窄化 | 超限 sparse companion、边界参数用例 |
| 复制/序列化失败可能继续并清理原件 | checked 同目录临时写、短写/EINTR 处理、fsync/close/rename 检查，失败时不清理恢复快照 | helper 故障注入及真实 pivot 中 metadata 发布失败的子进程用例 |
| 恢复清理中断遗留旧 sidecar | 移动前拒绝无 raw backup 的孤立 companion backup；检查目录类型和清理结果 | 两类孤立 backup、新 staging、已有 raw backup 续接 |
| 协议重解析丢失 replay 空记录 | 验证后的 replay regions 优先，raw 才交给协议解析器 | 真实 pivot 配置故意返回单 region 的协议 stub，验证其未覆盖 5 条含空消息边界 |
| 零长 UDP 仅保留逻辑槽位 | 真正发送单条零长 datagram，错误/超时返回失败；普通 UDP 同样保持单报文 | 真实 loopback 接收零 UDP、普通 UDP、TCP 和本地满队列测试 |
| prompt 剩余分配失败未处理 | 检查每次 JSON 对象/字符串创建和插入、序列化和复制；补 stall history 的 JSON/strdup 检查 | 原生 prompt 逐点故障注入与 json-c 结构/转义检查；history caller 检查经编译和源码复核 |
| 测试只覆盖独立模块，T15 未进 I/O 分支 | 增加 include 实际 afl-fuzz.c 的原生 harness；修复 T15；测试检查始终生效，计数准确 | 实际 native 函数、链接符号检查、真实 libc I/O 故障、NDEBUG 负控 |

严格格式也适用于扩充种子：replay 模式输出 replay，raw 模式输出 raw；初始扩充读取与原生导入共用 decoder。恢复已有 queue 时跳过重复的初始扩充，避免向恢复快照插入无 companion 的文件。

## 已执行的验证

代码提交为 `731beb1`（恢复与原生回归）、`013d59c`（网络与 prompt）、`799351e`（独立测试工具）。实际编译源与这些提交的源码一致，摘要和执行结果保存在 evidence 中。

| 项目 | 结果 |
| --- | --- |
| 全量 native `make -B -j2` | 退出 0，编译全部主工具，CC wrapper/覆盖差异自检通过 |
| LLVM 18 全量重建及最终自检 | 全量 `make -B -C llvm_mode -j2` 通过；最终 `make -C llvm_mode -j2` 退出 0，插桩覆盖差异自检通过 |
| 原生恢复 harness | 普通 80/80；ASan+UBSan 80/80，均退出 0 |
| 原生网络 | 10 组，普通 / NDEBUG / ASan+UBSan / ASan+UBSan+NDEBUG 均通过 |
| 原生 prompt | 18 例，同上四个构建变体均通过 |
| 独立 codec/checker | 普通和 NDEBUG 各 28/28；NDEBUG 故意失败负控正确退出 1 |
| CLI 和提交检查 | `-I` 出现在 help 中；`git diff --check` 通过 |

未发现 sanitizer 错误。构建保留原有 const/volatile qualifier、未使用变量等警告，日志保留完整输出；本轮没有将整个历史代码库改成零警告。LLVM 构建的可选 32 位 runtime 不可用，64 位主构建和插桩自检通过。

[原生与构建 evidence](../tests/evidence/recovery-integrity-20261103/README.md) 保存实际日志、退出码、命令、源文件和二进制摘要。[独立工具 evidence](../tools/format_audit/evidence/README.md) 单独标明覆盖范围。二进制和大规模中间产物不上传，可按命令重建。

## 复现

在本仓库目录运行，编译临时目录也保留在当前工作区：

```bash
mkdir -p tests/out/build/tmp
export TMPDIR="$PWD/tests/out/build/tmp"
make -B -j2
make -B -C llvm_mode -j2
python3 tests/run_native_recovery.py
bash tests/run_native_net_send.sh
NATIVE_NDEBUG=1 bash tests/run_native_net_send.sh
NATIVE_SANITIZE=address,undefined bash tests/run_native_net_send.sh
NATIVE_SANITIZE=address,undefined NATIVE_NDEBUG=1 bash tests/run_native_net_send.sh
bash tests/run_native_prompt.sh
NATIVE_NDEBUG=1 bash tests/run_native_prompt.sh
NATIVE_SANITIZE=address,undefined bash tests/run_native_prompt.sh
NATIVE_SANITIZE=address,undefined NATIVE_NDEBUG=1 bash tests/run_native_prompt.sh
bash tools/format_audit/run_all_tests.sh
```

原生恢复测试默认执行普通和 ASan/UBSan 两种构建，每个 case 独立进程和目录。通过链接器垃圾回收排除 fuzzer main 与模型调用路径；网络测试只连接本机 fixture；prompt 测试使用真实 json-c 加定点故障注入。无需 LLM 凭据或在线模型。

## 兼容范围与后续实验

- AFLNet 原有 native-u32 replay 布局和 StateAFL LLVM 18 接口保持兼容。
- 无类型初始目录的 `auto` 仍保留历史探测歧义；已知格式的实验使用显式 `-I`。
- 只有 raw + `.regions` 的旧归档不能证明原边界，恢复时需要找回 replay，或显式选择重新解析 raw。
- 相同 payload 的不同消息划分不能由 raw 自行辨认；应指定期望的 replay。
- TCP 空记录不会产生可观察的空网络消息；LLM 的 C 字符串接口仍不能表达任意二进制 payload。
- 文件写入错误会保留恢复原件，但本次没有实现整个目录的跨文件断电事务。
- 本次没有进行目标 fuzz campaign 或在线 LLM 实验。编译与定点回归验证了这些修复路径；论文三项方法的覆盖率、停滞恢复效果和成本仍需独立对照实验。
