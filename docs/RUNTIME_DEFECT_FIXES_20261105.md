# 四项运行时缺陷修复与验证（2026-11-05）

基线：`43b9b41ece4a5db00b4149e6301f3491f6a6b10b`。代码提交：`6ef95bd`（同步/停滞）和 `939c173`（语法/JSON）。修复分支：`fix/chatafl-runtime-defects-20261105`。本轮修改不合并到 `main`。三个 agent 分别负责语法处理与测试、同步/停滞原生测试、同步设计复核与真实本地运行脚本；主 agent 实现核心同步和计数修复，并完成编译、最终验证与归档。

## 修复结果

| 已确认缺陷 | 修改后的行为 | 直接验证 |
| --- | --- | --- |
| 同步调用已释放消息链表 | 每次导入读取 raw 和匹配 replay 的稳定内存快照，构建独立消息链表，保持到执行、保存、状态重跑及校准结束。所有全局链表释放同时清空 M2 指针。 | 原生事务测试；相同真实本地同步场景旧二进制 SIGSEGV，修复版完成导入与后续变异 |
| 长模板超过 128/1024 字节栈缓冲区 | 使用动态、有总长度上限的模式缓冲区；超限或编译失败返回失败，两个模式指针均保持 NULL。普通文字被正确转义，只有 `<<...>>` 产生捕获组。 | 基线 123 字节头模板触发 ASan stack-buffer-overflow；修复版通过；超过 1024 字节的累计字段正常编译和匹配 |
| JSON 字符串里的 `]` 截断数组 | 提取时识别字符串、反斜线转义和数组深度，再用 JSON 解析器校验数组与字符串字段。 | 基线拒绝包含 `Host: [::1]` 的有效模板，修复版接受并匹配真实 CRLF 字节 |
| 空历史消耗全部停滞额度 | 历史和 prompt 有效时才在 `chat_with_llm` 调用前计数。空历史、prompt 构造失败不扣额度，也不清零停滞计数。 | 重复无效尝试后仍可发起第一次请求；64 次逻辑请求上限、成功和失败调用入口计数检查 |

同步导入独立于初始 `-I` 设置：例如以 `-I replay` 启动，peer queue 仍按 raw 加 companion 读取。执行快照中的完整消息数量在保存期间固定；即使各轮实际 `messages_sent` 不同，保存的 raw、replay 和队列 region 仍属于同一完整快照。实际发送计数继续供响应和状态注释使用。

companion 缺失、被截断或内容不匹配时推迟导入。同步进度会停在本轮最小的未完成 ID，避免无序目录扫描越过它；checkpoint 通过同目录临时文件原子发布。较大 ID 可能因前面存在待重试项而在下一轮重复执行，但不会因此丢失待导入样本。

新测试还直接触发了通用 replay 序列化中的未对齐 `u32` 写入；现已改为 `memcpy`，保留原有 native-u32 格式。原生测试单条编译允许 300 秒，避免对完整 fuzzer 源码进行 sanitizer 编译时出现 120 秒的工具超时。

## 实际运行结果

| 检查 | 结果 |
| --- | --- |
| 全量主工具 `make -B -j2` | 退出 0，包含 CC wrapper/覆盖差异自检 |
| LLVM 18 `make -C llvm_mode -j2` | 退出 0，插桩覆盖差异自检通过 |
| 新增同步与停滞原生测试 | 普通 32/32；ASan/UBSan 32/32 |
| 原有恢复原生回归 | 普通 80/80；ASan/UBSan 80/80 |
| 新增语法原生测试 | 普通、NDEBUG、ASan/UBSan、ASan/UBSan+NDEBUG 各 31/31 |
| 同步真实运行对照 | 基线 2.11 秒后 SIGSEGV，未导入；修复版运行 45.03 秒，正常退出 |
| 修复版同步结果 | 455 次执行，11 条 queue，导入 1 条；导入 raw 逐字节匹配，replay 恰好 3 条消息，checkpoint 为 1，peer 原件未变 |
| 长头模板对照 | 基线 ASan stack-buffer-overflow / 退出 1；修复版退出 0 |
| IPv6 模板对照 | 基线有效语法数为 0 / 退出 2；修复版有效语法数为 1 / 退出 0 |

固定版本的 sanitizer 检查没有报告错误；基线负控的 ASan 错误是预期结果。编译保留既有 const/volatile 限定符和未使用变量等警告，本轮未声称全仓库零警告。

真实同步运行使用本地固定 `network_server.c`，初始语料只有 `A\n`，预置 peer 为 `A\n`、`B\n`、`C\n` 三条消息。通过 `-S local` 和 `AFL_IMPORT_FIRST=1` 直接覆盖 dry run 清理后进入同步的原始缺陷路径。只有一个活动 fuzzer，peer 是磁盘 fixture；运行不带 `-P` 或 `-E`，没有调用在线模型。进程禁用 ASLR，所有文件与临时输出保留在 GPTFuzz 工作区。

基线二进制由上一轮已归档摘要确认，来源为 `43b9b41`；其报告中的 working-tree 源码摘要描述当时尚未编译的修复，不代表基线二进制源码。修复版报告的源码与二进制摘要对应本轮实际构建结果。

`chat_times` 计数逻辑上的停滞模型调用尝试，不是供应商计费请求数，也不是成功次数；一次调用内的 HTTP 重试不额外计数，已经发起但失败的调用仍占一次额度。

## 复现

从本仓库运行：

```bash
mkdir -p tests/out/build/tmp
export TMPDIR="$PWD/tests/out/build/tmp"
make -B -j2
make -C llvm_mode -j2
python3 tests/run_native_sync_stall.py
python3 tests/run_native_recovery.py
bash tests/run_native_grammar.sh
NATIVE_NDEBUG=1 bash tests/run_native_grammar.sh
NATIVE_SANITIZE=address,undefined bash tests/run_native_grammar.sh
NATIVE_SANITIZE=address,undefined NATIVE_NDEBUG=1 bash tests/run_native_grammar.sh
python3 tests/run_sync_smoke.py
```

原生同步与恢复 runner 默认同时执行普通和 ASan/UBSan 构建。语法测试使用真实 json-c/PCRE2；同步与停滞原生测试使用执行回调、模型替身及定点 I/O 故障注入，以固定输入测试生产 helper，未复制它们的实现。真实同步 smoke 则运行实际 fuzzer 主程序、两个 forkserver 和本地目标。

基线语法探针的 C 源码、编译参数、运行状态和 ASan 输出也已归档；可从 Git 提取基线 `chat-llm.c` 重建，无需 API Key。

[归档证据与摘要](../tests/evidence/runtime-defects-20261105/README.md) 包含实际命令、日志、状态、源文件与二进制哈希。前一轮 recovery-integrity 归档绑定旧源码提交；其旧源码校验清单不应被当作本轮清单。

本轮确认上述四项缺陷修复和定点运行结果；没有进行在线 LLM 效果实验，也没有完成长时间多实例并发压力测试。多消息 M2 的逐条语法覆盖、任意二进制模型接口等此前记录的范围限制没有包含在这四项修复中。
