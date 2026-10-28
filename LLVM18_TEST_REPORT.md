# LLVM 18 迁移测试报告

以下首先保留首次迁移的历史实测记录；最新系统命令复验见文末“系统工具链复验”，当前构建不再使用临时工具链或环境脚本。

测试日期：2026-10-23。基线 `598effa072eef993aa134a5f7ebb5518da8fc289`，分支 `llvm18-preserve`。最终干净构建成功，统一脚本 `bash tests/llvm18/run_all.sh tests/llvm18/out/verified-suite` **退出 0，所有组通过**。

归档证据位于 [tests/llvm18/evidence](tests/llvm18/evidence/README.md)，含日志、实际 argv、IR、状态序列、DOT、统计及 SHA256SUMS。完整生成产物保留在本机 `tests/llvm18/out/verified-suite/`，未提交二进制/庞大的临时输出。归档文本只移除终端颜色码及行末空白，原始输出仍保留。

## 环境与构建

实际为 Ubuntu 24.04.4 LTS x86_64。初始 LLVM 开发工具已装，但 Clang、CMake、Graphviz 开发包缺失且无免密 sudo；使用 Ubuntu 官方 deb 解包至 `/tmp/stateafl-deps`，没有声称已系统安装。`source /tmp/stateafl-env.sh` 设置本次会话的 CC/CXX/CLANG/CLANGXX、LLVM_CONFIG 以及解包头文件/库路径。另一台机器按 README 安装系统包后无需这些临时路径。

| 测试项目 | 实际执行命令 | 实际结果 | 证据 | 状态 |
|---|---|---|---|---|
| 系统与工具版本 | `cat /etc/os-release; llvm-config-18 --version; clang-18 --version; clang++-18 --version; opt-18 --version; llvm-dis-18 --version; cmake --version`，使用本次解包 PATH | Ubuntu 24.04.4；LLVM/Clang/opt/dis 均18.1.3；CMake3.28.3 | [toolchain.log](tests/llvm18/evidence/toolchain.log) | 通过 |
| 原始构建基线 | 在 `/tmp/stateafl-baseline` 对原源码执行 make | Clang18拒绝隐式int探针；原Pass缺已移除的PassManagerBuilder.h | [根基线](tests/llvm18/evidence/baseline-root-clang18.log)、[Pass基线](tests/llvm18/evidence/baseline-pass-clang18.log) | 基线失败，已修复 |
| 干净根构建 | `make clean`，随后 `make -j2 CC="$CC" all` | 所有原工具及传统汇编插桩自检通过 | [clean](tests/llvm18/evidence/final-clean.log)、[root build](tests/llvm18/evidence/final-root-build.log) | 通过 |
| LLVM18完整构建 | `make -C llvm_mode -j2 all` | New PM Pass、覆盖率及两种状态运行时、源码依赖全部构建；原showmap自检通过 | [LLVM build](tests/llvm18/evidence/final-llvm-build.log) | 通过 |
| 固定依赖重建 | 同上，Containers/TLSH/MVPTree均在`.build`编译 | 不使用基线旧archive；TLSH单行return补丁在源码副本应用 | 同上及[固定版本](tests/llvm18/evidence/toolchain.log) | 通过 |
| 暂存安装 | `make install DESTDIR=/tmp/stateafl-install`；以安装的wrapper编译并showmap运行 | 插件和C/C++ tracer均被安装，AFL_PATH定位有效，链接/运行成功 | [install.log](tests/llvm18/evidence/install.log) | 通过 |
| C++对象恢复 | 删除单独的`afl-llvm-rt-state-tracer.o.cpp.o`后`make -C llvm_mode all` | 真正重建副产物，SHA256与删除前相同 | [cpp-recovery.log](tests/llvm18/evidence/cpp-recovery.log) | 通过 |
| 原可选trace-pc模式 | `make -C llvm_mode AFL_TRACE_PC=1 all` | 替换LLVM18已删除backend选项后，构建及原coverage自检通过；随后恢复默认StateAFL Pass并重建 | [首次失败](tests/llvm18/evidence/optional-tracepc-build.log)、[修复后](tests/llvm18/evidence/optional-tracepc-fixed-build.log) | 通过，仅覆盖率可选模式 |
| 32位运行时尝试 | `clang-18 -m32 -fPIC -c llvm_mode/afl-llvm-rt.o.c ...` | 缺32位libc头，无法构建 | [实际错误](tests/llvm18/evidence/optional-32-failure.log) | 环境失败，不算通过 |

Clang包 `1:18.1.3-1ubuntu1`，CMake `3.28.3-1build7`，Graphviz `2.42.2-9ubuntu0.1`。三个恢复的gitlink与原官方StateAFL一致：Containers `d7c0177b34b49f4831d7943c4be420a5733345ac`、TLSH `fb38843a474ea2bd3a94400a3d61a37b403ca0b0`、MVPTree `bc229f6ebd73413a892d0c0d96fa6b360505e7ec`。子模块源码保持干净。

## 编译、IR与动态回归

下表命令的输出目录均以 `tests/llvm18/out/verified-suite` 为前缀；每组的完整 argv 在对应commands.json，全部由统一脚本实际执行。

| 测试项目 | 实际执行命令 | 实际结果 | 证据 | 状态 |
|---|---|---|---|---|
| C/C++×O0–3×g/g0 | `python3 tests/llvm18/run_ir.py --output .../ir` | 16组合+幂等+4诊断，共21/21；检查真实cc1优化参数、Clang18、插件加载、DICompileUnit有无与optnone | [IR结果](tests/llvm18/evidence/ir-result.json)、[命令](tests/llvm18/evidence/ir-commands.json)、[O0无debug IR](tests/llvm18/evidence/c-O0-nodebug.ll)、[O2 debug IR](tests/llvm18/evidence/c-O2-debug.ll) | 通过 |
| IR合法与去重 | 上组对每例执行`opt-18 -passes=verify`，再`-load-pass-plugin ... -passes=stateafl,stateafl,verify -verify-each` | IR全部合法；helper数量/coverage全局数不变 | 同上 | 通过 |
| opaque及ABI边界 | `python3 tests/llvm18/run_pass_edges.py --output .../pass-edges` | 11/11：formal/actual/debug恢复、callee正式类型优先、原int stub、32位IR、超宽/溢出/二级指针诊断等 | [边界结果](tests/llvm18/evidence/pass-edges-result.json)、[命令](tests/llvm18/evidence/pass-edges-commands.json) | 通过 |
| O0显式lifetime阈值 | 边界脚本对手写IR执行`stateafl,stateafl,default<O0>,verify`并运行observer | 63字节不执行hook，64字节start/end各执行1次，无重复 | 同上 | 通过 |
| 包装器/安全模式 | `python3 tests/llvm18/toolchain_features.py --output .../features` | 36/36；默认真Clang18、AFL_CC/CXX覆盖、分编链接、HARDEN/ASAN/MSAN、配置互斥、NO_BUILTIN均验证；ASAN C/C++和MSAN C实际执行 | [结果](tests/llvm18/evidence/features-result.json)、[命令](tests/llvm18/evidence/features-commands.json)、[driver](tests/llvm18/evidence/driver.log) | 通过 |
| 覆盖率反馈及ratio | 同上，test-instr经两次showmap运行；ratio1/100及非法0/101/invalid | 两路径tuple为3与5且map不同；合法ratio接受，非法值拒绝；边界脚本另验证ratio不丢state事件 | [空输入](tests/llvm18/evidence/path-empty.map)、[另一输入](tests/llvm18/evidence/path-one.map)、上述JSON | 通过 |
| 运行时实际记录 | `run_all.sh`中普通CC编译runtime_records.c、CXX链接`.build`库，再运行 | 直接assert map/queue/ignore/socket set，heap/calloc/realloc/free、stack、>2^33计数保存、SHM布局通过 | [记录日志](tests/llvm18/evidence/runtime-records-tracer.log)、[运行](tests/llvm18/evidence/runtime-records.log) | 通过 |
| 真实插桩I/O/对象 | `python3 tests/llvm18/run_dynamic.py .../dynamic` | io/cpp/memory/custom四组实际执行通过；非仅检查IR | [结果](tests/llvm18/evidence/dynamic-results.json)、[组日志](tests/llvm18/evidence/dynamic-suite.log) | 通过 |
| 全部原I/O分支 | 同上，真实socketpair及FILE操作 | recv/recvfrom/recvmsg/send/sendto/sendmsg/read/write/fprintf/fgets/fread/fwrite/close/fclose全部成功；普通文件不当socket；_exit前分析完成 | [IO运行时](tests/llvm18/evidence/io-state-tracer-rt.log)、[实际操作](tests/llvm18/evidence/io-stdout.log) | 通过 |
| msg、栈及自定义旧语法 | 同上；custom_receive:1/custom_send:1 | msghdr长度均56；实际栈登记/释放；custom结构指针非空、长度256，产生状态 | [custom](tests/llvm18/evidence/custom-state-tracer-rt.log)、[IO](tests/llvm18/evidence/io-state-tracer-rt.log) | 通过 |
| C++异常路径 | 同上cpp fixture | new/new[]/delete/delete[]及构造异常cleanup，分配256/256/512与3次释放地址匹配；矩阵/边界另验证invoke IR | [CPP运行时](tests/llvm18/evidence/cpp-state-tracer-rt.log) | 通过 |
| 快照、TLSH与MVP | `./afl-clang-fast -O1 -g tests/llvm18/state_fixture.c -o .../fixture`，`python3 tests/llvm18/state_feedback.py .../fixture .../data` | 真实SHM七轮全部通过：新状态、重复复用、树持久化、校准与恢复 | [反馈JSON](tests/llvm18/evidence/state-feedback.json)、[运行日志](tests/llvm18/evidence/state-feedback.log) | 通过 |

共享布局实际为408/407324字节，结构定义与基线逐字相同。独立状态fixture的首次/重复序列为 `[0,1,2,3]`，变更长期对象后/重复为 `[0,4,5,6]`；iterations=3、seq_len=4。校准产生三个72字符T1哈希，重复距离 `[0,0,0]`；校准不修改MVP树，关闭后恢复原序列，最终六个唯一状态。瞬态对象与I/O子区域排除均有原运行时日志证据。

## 最终端到端验收

实际命令：`python3 tests/llvm18/e2e.py --output tests/llvm18/out/verified-suite/e2e --seconds 45`（由统一脚本调用）。实际目标是此版本wrapper编译的本机两字节帧服务器。完整[命令与端口](tests/llvm18/evidence/e2e-commands.json)、[fuzzer日志](tests/llvm18/evidence/e2e-fuzz.log)、[断言结果](tests/llvm18/evidence/e2e-result.json)已保存。

- 45.01秒，487次执行，两个原forkserver都启动，正常SIGINT退出0。
- queue/replayable queue均16（初始2），coverage0.07%，稳定性100%，crash/hang均0。
- 状态为0–8，16条状态路径，含fuzz阶段新路径；所有初始状态0，反馈序列的每条边都存在于Graphviz可解析的[ipsm.dot](tests/llvm18/evidence/ipsm.dot)。
- [tlsh.log](tests/llvm18/evidence/tlsh.log)记录原算法校准半径5；未人为调低半径以通过测试。
- 真实TCP合并与逐字节分片发送的回复完全一致，SHM均为 `[0,1,2,3]`：[预检结果](tests/llvm18/evidence/stream-framing.json)。
- 系统ASLR=2，Python父进程personality=0；fuzzer由setarch -R启动，自身检查证实ADDR_NO_RANDOMIZE。没有将父进程属性冒充目标属性。
- 原始[统计](tests/llvm18/evidence/fuzzer_stats)与JSON队列数量一致。完整套件[退出成功日志](tests/llvm18/evidence/verified-suite.log)。

## 迭代失败、定位与复验

1. 原Legacy PM/API、ABI及显式-O/-g0被覆盖分别由编译器、IR/driver/debug断言发现并修复；早期被默认-g污染的“无debug”矩阵不作最终证据。
2. 重建固定TLSH后，漏return的int包装函数生成ud2，首次距离计算SIGILL：[失败日志](tests/llvm18/evidence/tlsh-sigill.log)。单行return诊断副本用于定位；正式验收全部使用构建系统永久补丁后的archive，没有混入诊断对象。
3. 原ASLR全局检查拦截：[初次失败](tests/llvm18/evidence/initial-aslr-failure.log)。只认可实际per-process flag后运行，不取消稳定地址要求。
4. 第一轮统一套件的E2E未过：[结果](tests/llvm18/evidence/failed-framing-e2e.json)、[日志](tests/llvm18/evidence/failed-framing-e2e.log)。旧测试服务把一次recv当消息，同6字节合并发仅1响应、逐字节发却6响应，[精确复现](tests/llvm18/evidence/stream-framing-before.log)。修复fixture的TCP帧边界，限定8帧并把poll调为20ms，新增真实SHM分片一致性预检；未改状态算法/半径，重新执行完整统一套件全部通过。
5. 状态路径文件名尾随连字符由原引擎产生，测试解析器已兼容，严格非零状态/差异/新路径/图边断言仍保留。
6. agwrite后FILE未flush的初始猜测被精确复现及真实DOT反证；没有因此修改引擎图输出逻辑。

## 限制与未执行项

- 完整32位目标运行未通过环境条件：缺multilib，且基线从未提供完整32位状态tracer；只把32位IR标为通过。
- 普通O0下Clang不发lifetime时，没有自动栈事件；显式O0 lifetime与O1–3源代码生命周期分别验证。未知长度-1 lifetime的原有风险未通过置零隐藏。
- 极大计数只验证ABI/记录保存；原postexec int偏移及TLSH unsigned长度仍限制多GiB快照，未实际分配/哈希数GiB。
- 外部无debug且opaque IR完全不含struct证据时需显式custom大小；此限制有诊断。原语法的local/global/formal/debug可恢复场景及事件模式已经测试。
- QEMU/Android/非x86、libdislocator/libtokencap运行、分布式长期campaign、全部blacklist/global-data/mutex/计时宏组合、实际第三方大型服务器及-u/-U跨二进制组合未穷举验收，原源码/接口保留。
- ASAN目标测试关闭leak detection；不声称修完运行时所有既有资源释放问题。短期本机服务通过不等于任意目标的长期稳定性证明。

本次验收支持 **Ubuntu24.04 x86_64 / LLVM18的默认完整StateAFL路径**，包含覆盖率和实际状态反馈。对上述边界不作未执行的成功声明。

## 系统工具链复验（后续更改，当前默认方式）

本节取代早期临时工具链的构建方式；前文日志仅保留为第一次迁移历史。此轮在没有CC/CXX/CLANG/LLVM_CONFIG或库搜索路径覆盖变量的环境中，先删除临时工具链，再执行`make clean`、`make -j2`、`make -C llvm_mode -j2`以及完整回归。

| 项目 | 实际结果 | 证据 | 状态 |
|---|---|---|---|
| 系统入口 | `/usr/bin/clang`、`clang++`、`llvm-config`、`opt`、`llvm-dis`；LLVM/Clang均18.1.3 | [版本及系统包归属](tests/llvm18/evidence/system-toolchain/toolchain.log) | 通过 |
| 清理额外副本 | `/tmp/stateafl-deps`、`/tmp/stateafl-packages`、`/tmp/stateafl-env.sh`、`/tmp/stateafl-install`均已删除 | [实际删除记录](tests/llvm18/evidence/system-toolchain/system-selection-and-cleanup.json) | 通过 |
| 默认干净构建 | 未指定版本化二进制、前缀或工具链脚本；根工具/Pass/运行时/依赖及原覆盖率自检成功 | [根构建](tests/llvm18/evidence/system-toolchain/root-build.log)、[LLVM构建](tests/llvm18/evidence/system-toolchain/llvm-build.log) | 通过 |
| 无旧前缀及CMake缓存 | 五个构建产物不含删除的临时前缀，CMake编译器为`/usr/bin/clang` | [产物检查](tests/llvm18/evidence/system-toolchain/artifact-paths.json) | 通过 |
| PATH默认选择 | 不设置AFL_CC/CXX，C/C++版本查询与真实对象编译四次均经过PATH探针 | [真实调用记录](tests/llvm18/evidence/system-toolchain/path-compiler-calls.jsonl) | 通过 |
| 版本匹配保护 | 模拟Clang17、系统LLVM18时构建前检查明确拒绝 | [拒绝日志](tests/llvm18/evidence/system-toolchain/version-mismatch.log) | 通过，预期拒绝 |
| IR与边界 | 非版本化`opt`验证：主矩阵21/21、边缘11/11 | [IR](tests/llvm18/evidence/system-toolchain/ir-result.json)、[边缘](tests/llvm18/evidence/system-toolchain/pass-edges-result.json) | 通过 |
| 工具链与安全配置 | 原36项与新增四次PATH调用，共40/40；ASAN/MSAN实际构建运行保留 | [工具链结果](tests/llvm18/evidence/system-toolchain/features-result.json) | 通过 |
| 内存及全部I/O | 原对象断言、io/cpp/memory/custom四组真实插桩测试通过 | [动态结果](tests/llvm18/evidence/system-toolchain/dynamic-results.json)、[对象日志](tests/llvm18/evidence/system-toolchain/runtime-records.log) | 通过 |
| 真实状态反馈 | 七轮SHM/TLSH/MVP/校准通过，序列仍为`[0,1,2,3]`与`[0,4,5,6]`并重复复用 | [状态证据](tests/llvm18/evidence/system-toolchain/state-feedback.json) | 通过 |
| 端到端45秒 | 443执行、queue13、状态0–8、校准半径5、stability100%、crash/hang0，18项断言通过 | [结果](tests/llvm18/evidence/system-toolchain/e2e-result.json)、[状态图](tests/llvm18/evidence/system-toolchain/ipsm.dot)、[统计](tests/llvm18/evidence/system-toolchain/fuzzer_stats) | 通过 |
| 完整脚本 | `bash tests/llvm18/run_all.sh tests/llvm18/out/system-toolchain`退出0 | [完整执行日志](tests/llvm18/evidence/system-toolchain/suite.log) | 通过 |

`/usr/lib/llvm-18`属于任务开始前已有的系统包，且系统入口通过符号链接依赖它；本轮没有将该目录误作额外安装删除，也没有下载或手工安装另一份LLVM。构建仍使用系统`llvm-config`返回的合法头文件/库路径。此前32位完整运行时、极大快照等限制不变。
