# 四项运行时修复的验证证据

实际代码提交：`6ef95bd`（同步快照、生命周期、停滞计数与回归）和 `939c173`（语法构建、JSON 提取与回归）。完整源码提交为 `939c173c0826785f17395664cf4a13ee6154e21d`，基线为 `43b9b41`。测试先在工作区执行、后提交；用源文件摘要确认已提交代码与测试时的内容一致。后续文档提交不改变这些生产源码。

- `summary.json`：源码提交、源文件摘要、聚合结果与运行范围。
- `build/`：全量 native 构建和 LLVM18 自检的命令、实际日志、退出状态、编译器版本和产物摘要。
- `native-sync-stall/`：普通及 ASan/UBSan 各32项，实际生产 helper、执行回调与模型替身、定点 I/O 故障注入。
- `native-recovery/`：普通及 ASan/UBSan 各80项旧有原生回归。
- `native-grammar/`：四种构建各31项，真实 json-c/PCRE2、长模板、IPv6/引号/转义、字面匹配、多个捕获组和编译失败传播。
- `sync-baseline/`：旧二进制真实运行失败对照。`baseline_control` 绑定旧二进制和此前归档摘要；报告中 working-tree 源码摘要指当时尚未编译的修复，不能用来标注旧二进制源码。
- `sync-fixed/`：修复后二进制的45秒本地运行。完成455次执行、保存11条queue、导入1条peer样本；raw与三条replay记录逐字节匹配，checkpoint为1，peer未变，正常退出。
- `grammar-before-after/`：实际C探针、编译参数、返回状态与ASan输出；基线解析失败/栈越界，对应修复版均退出0。
- `SHA256SUMS`：归档文件和当前测试源码的校验清单，路径相对仓库根目录。

原生 harness 直接 include 实际 `afl-fuzz.c`，链接真实 `aflnet.c`/`chat-llm.c`。runner 使用 `nm` 确认未保留 fuzzer main/外部模型调用；归档 `symbol-proof.json` 记录排除结果和原始符号表摘要，完整符号表仍在本地忽略的测试输出目录。每个case的实际stdout/stderr已聚合到 `cases.log`。

同步真实运行通过一个活动 fuzzer 读取磁盘预置peer，覆盖完整 dry run、同步执行、保存和后续变异；没有启动第二个并发写入peer的fuzzer。它没有启用 `-P` 或 `-E`，因此不是在线LLM测试或多实例长期压力证明。

基线语法探针的ASan关闭符号化以避免诊断器延迟，并关闭泄漏检查以聚焦旧栈越界；修复版正式语法suite仍启用ASan/UBSan和泄漏检查。基线退出非0是预期负控，不应算作修复版测试失败。

从仓库根目录校验：

```bash
sha256sum -c tests/evidence/runtime-defects-20261105/SHA256SUMS
```

复现命令及兼容范围见 [修复报告](../../../docs/RUNTIME_DEFECT_FIXES_20261105.md)。旧版 `recovery-integrity-20261103` 中的源码校验清单绑定旧提交，不应针对当前源码要求相同摘要。
