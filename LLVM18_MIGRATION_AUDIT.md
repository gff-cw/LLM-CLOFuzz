# StateAFL LLVM 18 迁移审计

## 基线与范围

审计先于实现修改。基线：`598effa072eef993aa134a5f7ebb5518da8fc289`，分支 `main`，工作区无用户修改；迁移分支 `llvm18-preserve`。远程 `git@github.com:gff-cw/CLOFuzz.git`，读取成功。环境 Ubuntu 24.04.4 x86_64，LLVM 18.1.3；初始未安装 Clang、CMake、Graphviz 开发包，无免密 sudo。三个依赖目录为空，虽有 `.gitmodules` 却没有 gitlink；原仓库跟踪了旧 `.a`，不能将其当作源码重建证据。

以下行号指基线，可用 `git show 598effa:路径` 检查。完整 getenv/预处理配置与 Makefile 目标索引见 `tests/llvm18/baseline-inventory.txt`，从全部跟踪源码生成，并非只读 README。

## 原始架构与反馈协议

wrapper → Clang加载ModulePass → 边覆盖及内存/I/O/lifetime事件 → C tracer 的对象map、快照queue及忽略区map → TLSH → MVPTree状态匹配 → `state_shared` → afl-fuzz 的状态调度及IPSM。另有独立 AFL coverage forkserver。`afl-fuzz.c:10508` 默认启动两个 forkserver；状态进程禁用 coverage SHM，保留状态/校准 SHM。状态新颖度参与状态/seed调度，原队列保存仍由 coverage 新位判断，不能声称状态新颖度独立触发队列。

保持共享协议：`state-tracer.h` 中 `state_shared` = u32 length、u32 seq[100]、u32 iterations，x86_64 sizeof=408；`calibration` sizeof=407324，字段 offset 0/4/8/12/7312/7316/407316/407320。不修改结构、初始状态0、状态编号、TLSH/MVP算法、轮次或文件格式。

## 原始功能清单

|功能|实现/关键函数（基线）|插桩入口→运行时接收者|反馈作用|配置入口|LLVM18风险|验证|
|---|---|---|---|---|---|---|
|AFL边覆盖|pass:109–246，rt:__afl_map_shm|每BB→`__afl_area_ptr`,`__afl_prev_loc`|64K位图，xor前位置，8bit递增|AFL_INST_RATIO/AFL_QUIET，MAP_SIZE|Load/GEP显式类型，TLS|IR+showmap两路径|
|覆盖率运行时|afl-llvm-rt.o.c:75, forkserver/manual_init/persistent_loop|constructor及wrapper宏|SHM/forkserver/持久模式|__AFL_SHM_ID/__AFL_PERSISTENT/__AFL_DEFER_FORKSRV|构建及初始化顺序|动态SHM、forkserver E2E|
|模糊测试引擎|afl-fuzz.c，aflnet.c|run_target/send_over_network|coverage、havoc、deterministic、队列、crash/hang|原getopt全集见索引，-i/-o/-d/-m/-t等|现代C/Graphviz|根构建及限时fuzz|
|自动协议状态|runtime net_receive/net_send:1101–1149，end_state_tracer:1475|收发→trace_receive/send|receive→send快照，每轮状态|STATE_SHM_ID/AFL_OUTDIR|事件被优化或遗漏|SHM序列、状态分叉|
|malloc/C++new|pass:308，runtime:820|调用后→new_heap_alloc_record|对象记录及零初始化|ASAN特例|错误vararg声明、u64|IR及实际记录|
|calloc/realloc|pass:326–358，runtime:860–975|调用后→trace_calloc/realloc|大小乘积、旧对象链与长寿命|同上|size_t/i32不匹配|对象大小/链断言|
|free/C++delete|pass:360，runtime:833|调用前→free_heap_alloc_record|标记结束轮次|无|runtime两参，pass一参|动态freed断言|
|栈生命周期|pass:298/690，runtime:840/853|llvm.lifetime.start/end→new/free_stack_alloc_record|>=64B记录/清理|MIN_STACK_ALLOC_SIZE=64|时机、i64、CFG|O0–3 IR及动态记录|
|recv/recvfrom|pass:380|调用前→trace_receive|输入区排除及轮次切换|无|size_t声明|本地socket动态|
|send/sendto|pass:437|调用前→trace_send|触发快照、排除输出|无|同上|本地socket动态|
|recvmsg/sendmsg|pass:398/456|msghdr参数→trace_receive/send|原义为整个msghdr对象区，不是payload长度|无|旧getContainedType、布局恢复|本地socket，sizeof(msghdr)断言|
|自定义收发|pass:127–198/487|name[:zero-based-index]→trace_receive/send|原省略index时传NULL/0；struct*按结构大小|TRACE_CUSTOM_RECEIVE/SEND，分号列表|opaque丢pointee、非法index越界|local/formal/external/debug/-g0及非法配置|
|read/write|pass:588/609，runtime:1151/1164|调用前→trace_read/write|getsockopt识别socket，普通文件忽略|无|size_t|socketpair及文件反例|
|fprintf/fgets|pass:630/650，runtime:1177/1198|fprintf后（使用返回值）、fgets前|FILE→fileno→is_socket|无|正确返回值顺序|socket FILE及普通FILE|
|fread/fwrite|pass:670附近，runtime:1219/1240|调用前→trace_fread/write|元素大小×数量|无|两size_t参数|socket FILE动态|
|close/fclose/_exit|pass:652–688，runtime:1261/1283/1475|关闭事件/强制析构|清socket缓存、终结状态|无|fclose后访问已释放FILE|动态关闭与_exit|
|快照及长寿命筛选|runtime new_alloc_record:732/tracer_dump:978/end_state_tracer:1592|map→queue|第0轮对象、realloc链、最终仍有效对象|RESTRICT_TEXT_ALLOCS/BLACKLIST_ALLOC_SITES|算法应保持|对象与快照断言、状态差异|
|I/O缓冲排除|runtime check_inner_pointer:1055/check_iobuf:1075|忽略区map|完整/子区从TLSH输入排除|无|长度截断|忽略区及hash反例|
|TLSH|runtime compute_state_value:1301，tlsh-wrapper.h|补至1024后Tlsh_final/get_hash|模糊hash及distance|TLSH_SIZE=72|缺依赖源码|非空hash、相同/不同状态|
|MVPTree|runtime:1393/1449/1512/1847，mvptree.h|retrieve/add/read/write|半径近邻、复用编号及持久化|MVP_RADIUS|旧archive复用|新状态、同状态复用、tree文件|
|状态校准/限流|runtime:1336/1793；fuzzer校准代码|calibration SHM|参考hash、距离、半径/限流|MVP_CALIBRATION_SHM_ID，DISABLE_RATE_LIMITING|ABI共享不可变|多次校准断言、E2E日志|
|状态序列/IPSM|afl-fuzz.c:1494/2954/1305，state_shared|runtime→extract_response_state_tracer|seq[0]=0、选择状态/seed、图|-E/-q/-s/-R/-F，-P可选crosscheck|stdio输出既有缺陷|SHM及ipsm.dot、queue|
|双forkserver|afl-fuzz.c:10508|两个编译目标|-u vanilla、-U cwd|原参数保留|错误禁forkserver会损坏路径|正常forkserver E2E|
|编译器选择及链接|wrapper:112–140/287–327|C/C++编译/链接|加载Pass及两种tracer对象|AFL_CC/CXX/PATH|旧-load及错误默认clang|-###，C/C++分编链接|
|优化/安全选项|wrapper:172–245|编译标志|默认O3/-g、hardening、ASAN/MSAN|AFL_DONT_OPTIMIZE/HARDEN/USE_ASAN/USE_MSAN/NO_BUILTIN|默认O3覆盖显式O0；ASAN运行时冲突|优化矩阵及安全模式|
|可选模式/位宽|llvm Makefile，rt USE_TRACE_PC|32/64 coverage对象、可选AFL_TRACE_PC|原可选coverage机制保留|AFL_TRACE_PC/CC/CXX/LLVM_CONFIG等|原32bit tracer本就只有host对象|独立构建/明确多库限制|
|传统AFL及辅助工具|根Makefile，afl-{gcc,as,showmap,tmin,analyze,gotcpu,replay}、aflnet-replay|原构建目标|重放/最小化/覆盖工具|原env/CLI索引|Clang18隐式int检查|完整构建、自检、重放|
|其他可选运行时|runtime宏及libdislocator/libtokencap/qemu/experimental|索引所列编译条件|全局段追踪、blacklist、日志、互斥、timing等|全部原宏/env保留|不应借迁移删除|源码差异审查；未执行则明确标记|

## 原始辅助函数ABI与迁移契约

原返回均void。`new/free_heap_alloc_record`、`new/free_stack_alloc_record` 为(ptr,u64)；calloc(ptr,int,int)、realloc(ptr,int,ptr)、receive/send(ptr,int)、read/write(int,ptr,int)、fprintf/fgets(ptr,ptr,int)、fread/fwrite(ptr,ptr,int,int)、close(int)、fclose(ptr)、end_state_tracer()。原Pass多处 `FunctionType::get(voidTy,ptrTy,intTy)` 实际选中(result,ArrayRef,bool)，生成varargs；还把i64 size_t原参传i32 formal，free少一参，end错误使用void参数。这些均须修复而非隐式容忍。

迁移保持heap/stack u64和free两参数（未知size传0，原size仅log使用）；将实际size_t来源的calloc/realloc、网络/FD/块FILE长度及内部ignore区大小改成size_t，IR从模块DataLayout取pointer宽度。fprintf返回/fgets上限仍int。新增msg适配器在具有系统头的C runtime中计算sizeof(struct msghdr)，再调用原trace_receive/send，保持整个msghdr对象的原语义。不得改成iovec payload长度。

## 修改假设、风险与顺序

1. **源码依赖与构建**：恢复上游相同固定gitlink提交；不依赖已跟踪旧archive，保留objcopy、两个tracer对象和32/64 coverage目标。选择匹配LLVM_CONFIG bindir的编译器并把路径编进wrapper，保留AFL_CC/CXX。验证干净重建、实际加载、安装副本。
2. **Pass**：独立instrumentModule、PassInfoMixin、isRequired、PipelineStart+显式opt parser。只一个自动回调，metadata防重复。LLVM18 PipelineStart在O0 AlwaysInliner前、O1+ module simplification前；OptimizerLast太晚会失去原调用和lifetime。覆盖算法保持，状态事件不再受coverage ratio随机continue误伤。验证O0–3/g0/g矩阵、double pass、verify-each。
3. **opaque pointers**：全面去除getContainedType。custom从alloca/global/GEP、load/store链、formal参数的GEP使用及调试签名恢复struct；对确实信息丢失外部声明给明确诊断和可选显式size扩展，不静默NULL/0。验证原有struct* local/formal/外部g场景，g0不可恢复例、错误index、无index旧行为。TypeSize只接受固定大小，拒绝scalable/溢出，不用盲cast。
4. **ABI及事件顺序**：上表同步Pass/runtime/header。close/fclose移到原调用前，因fclose后fileno为UAF。验证实际对象记录/所有I/O事件，不只数IR helper。
5. **回归**：代码覆盖与状态覆盖独立验证；本地socket+SHM+TLSH/MVP；再正常双forkserver有限时fuzz，检查state序列/队列/IPSM，保存日志。确认后更新README和测试报告，每阶段commit并push。

## 与迁移测试相关的既有缺陷及未扩大范围的问题

需修：上述ABI、fclose后UAF、根test_x86隐式int、wrapper显式-O被覆盖；若实测IPSM为空，修复agwrite后仅close(fd)未fclose导致缓冲未刷的缺陷。保留算法及SHM。

另行记录：原运行时malloc/calloc/realloc失败时NULL处理薄弱；状态/校准数组上限、校准<=ref_len；end_state_tracer非幂等；AFL_NO_FORKSRV网络路径；多线程默认无锁；原Pass只处理CallInst忽略invoke（迁移应评估C++实际EH路径）；未开启全局数据/分配站点过滤/ASAN初始化等宏的默认行为不随迁移改变。除直接阻断验证的最小修复外，不做无关重构。

## 设计依据

本机 `/usr/lib/llvm-18/include/llvm/Passes/PassBuilder.h` 460–495，LLVM [New PM](https://llvm.org/docs/NewPassManager.html)、[插件编写](https://llvm.org/docs/WritingAnLLVMNewPMPass.html)，以及[18.1.3实际pipeline源码](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.3/llvm/lib/Passes/PassBuilderPipelines.cpp)。[AFLNet PR145](https://github.com/aflnet/aflnet/pull/145)只作接口迁移参考，不能替代StateAFL状态Pass。[原README](https://github.com/stateafl/stateafl/blob/main/README.md)、[原Pass](https://github.com/stateafl/stateafl/blob/main/llvm_mode/afl-llvm-pass.so.cc)、[原runtime](https://github.com/stateafl/stateafl/blob/main/llvm_mode/afl-llvm-rt-state-tracer.o.c)与本地源码对照；所有行为最终以本地基线为准。

## 实施与迭代验证记录

- **构建基线**：原根构建在 Clang18 的 implicit-int 检查失败；显式 `int main(void)` 后，传统 afl-as 又因 Clang18 把 `-g -gdwarf-5` 放在输入文件之后而误读最后一个参数。仅调整这些尾随 debug 参数的输入定位，保留原汇编插桩；根工具及原自检通过。根自检显式传递构建时 CC，避免测到另一 PATH 中的编译器。
- **依赖重建**：恢复官方原 gitlink：Containers `d7c0177b34b49f4831d7943c4be420a5733345ac`，TLSH `fb38843a474ea2bd3a94400a3d61a37b403ca0b0`，MVPTree `bc229f6ebd73413a892d0c0d96fa6b360505e7ec`。新 `.build` archive 不复用基线旧二进制。运行时以独立 core 对象产生 C/C++ 两个真正目标；补齐安装原本遗漏的两个 tracer。配置戳比较 LLVM/编译器/选项，变更触发重建；section rename 等待全部产物再运行。
- **TLSH 实际 SIGILL**：源码重建后，真实共享内存 fixture 首次近邻距离计算 SIGILL，反汇编确认 `Tlsh_from_str()` 的 int 函数无 return 导致 Clang18 在调用后生成 `ud2`。`patches/tlsh-return-value.patch` 仅添加 `return`，在 `.build/tlsh-src` 的固定源码副本中应用；原子模块保持干净。先用单行诊断副本定位，再用永久构建产物重跑全部状态测试，不能混用两者证据。TLSH/MVP算法、hash/distance参数均未改。
- **辅助ABI**：所有声明改为固定参数；真实 size_t 按目标 DataLayout；窄int网络stub显式无符号扩展，保留原 `test-state-tracer.c` 的可编译性；拒绝超目标宽度/ABI拆分造成的不支持布局。free保留第二u64参数，传0（此参数只用于log）；新增msg适配器只计算正确的目标 `sizeof(msghdr)`。共享结构的字段、大小及offset不变。
- **CallBase 与事件位置**：先收集原调用，避免处理新增helper或CFG遍历失效。兼容invoke成功边，分配后才记录；异常未分配不记录。free/close/fclose在原调用前，fprintf在返回值可用后；其余I/O事件顺序保持。C++异常cleanup和所有I/O函数均有实际执行证据。
- **opaque custom**：优先callee formal的结构证据，符合原按formal pointee取sizeof的含义，再恢复actual对象。信息来源包括DataLayout可布局的struct、alloca/global/GEP、load/store/phi/select、formal用途、byval/sret、DI。保留原无位置/-1的纯事件模式；有位置但丢失类型时明确诊断，提供兼容可选 `name:index:bytes`；不把opaque pointer一概置NULL。歧义、非指针、越界、非法文本、S**与scalable/size_t溢出分别测试/诊断。
- **覆盖率与状态隔离**：原 ratio 的BB continue误伤状态事件，迁移后ratio只影响原coverage更新，state事件全部保留；ratio1/100状态调用序列相同。原xor/8bit计数/TLS/SHM算法不变。
- **Pass时机**：唯一PipelineStart自动注册、required模块pass、显式stateafl parser、module metadata幂等。O0本身不发lifetime时明确不声称自动追踪；手写O0 lifetime IR及动态observer验证63/64字节阈值，正常O1–3实际栈对象另测。
- **包装器矩阵纠错**：复审发现原默认追加 `-O3` 和 `-g` 会覆盖显式 `-O0`/`-g0`，均改为仅未显式指定时补默认值。测试加强检查实际cc1 driver参数和DICompileUnit有无，重新运行真实16组合，早期被默认-g污染的矩阵不作最终无debug证据。
- **可选trace-pc**：实际重建发现旧 backend 选项 `sanitizer-coverage-block-threshold` 在LLVM18已删除，用现存 `sanitizer-coverage-prune-blocks=0` 保留不裁剪的可选coverage路径；该模式自检通过，然后恢复并干净重建默认StateAFL Pass。没有将此模式设为默认或用它验收状态功能。
- **ASLR检查**：初次E2E被原全局sysctl检查拒绝。Linux允许 `setarch -R` 按进程关闭ASLR；增加只认可真实 `ADDR_NO_RANDOMIZE` personality 的检查，未设置且全局ASLR开启时仍拒绝。默认双forkserver继承同一personality；两轮实测全球sysctl=2/进程flag=262144，状态反馈稳定。未修改全局sysctl或要求sudo。
- **IPSM假设反证**：审计初期怀疑agwrite后仅close使DOT未flush；实际精确复现及E2E均证明本机Graphviz的agwrite已经flush，非空DOT可解析。因此没有为此修改afl-fuzz的输出逻辑；FILE资源泄漏等仍作原有问题单列。

## 验证范围与剩余限制

最终完整实测结果以 `LLVM18_TEST_REPORT.md` 为准。没有改动 fuzzer 的状态编号、调度算法、共享内存协议或TLSH/MVP推断参数。

1. 原32位目标仅有coverage运行时，缺完整32位状态tracer；本环境缺multilib，保留可选构建规则并实际记录失败，不宣称完整32位链接通过。32位helper IR独立验证通过。
2. 原postexec仍用int快照偏移/累积长度，TLSH API为unsigned长度；>2^33测试仅验证helper ABI/记录保存，不证明实际多GiB快照/哈希支持。
3. 原 `llvm.lifetime` 长度-1代表未知大小，但原阈值无符号比较会把其视作超大对象，可能导致memset错误；此原有风险记录，不将未知长度默默当0。常规Clang固定长度测试和显式O0阈值测试均通过。
4. 外部无debug且IR中完全无struct证据时，无法从opaque ptr安全重建被擦除的C类型，需可选显式尺寸；这项兼容限制明确诊断。手工musttail的需后置hook调用也明确诊断，不生成非法IR。
5. 可选全局数据、blacklist、互斥、rate-limit禁用、QEMU/Android/非x86、分布式长期campaign等未全面动态验收；宏/原接口均保留，不能据x86_64短期测试声称所有可选组合通过。
6. 初始审计列出的NULL分配问题中，实际ABI/记录测试要求最小补齐失败处理：malloc/calloc不记录NULL，realloc失败保留旧记录；glibc realloc(p,0)释放时仅标记旧对象。成功分配、realloc链与快照筛选算法保持。调试日志在fopen前使用的初始化顺序错误也仅移动一条日志至fopen后，运行时调试回归通过。

## 最终验收与最后一轮测试纠错

最终 `run_all.sh tests/llvm18/out/verified-suite` 退出0：IR主矩阵21项、Pass边界11项、工具链/coverage/安全配置36项、对象记录、四组真实插桩动态测试、七轮SHM/TLSH/MVP校准全部通过；最后45秒E2E执行487次，队列2→16，状态0–8，16条状态路径，coverage0.07%、stability100%、crash/hang0。原始统计、图、状态及命令已归档到 `tests/llvm18/evidence` 并提供SHA256。

最后一轮之前的统一E2E曾明确失败（半径595、状态被合并），没有将其标为成功。精确复现证明测试服务器把TCP分段当消息：同6字节合并发送仅1响应，逐字节发送6响应。仅修复测试服务为固定2字节MSG_WAITALL帧、最多8帧，poll从2ms改20ms，增加合并/分片发送的真实SHM一致性预检；两者均3个相同响应及 `[0,1,2,3]`。状态算法/校准半径未调整，重新运行完整套件后原算法得到半径5。测试解析器也兼容原引擎有时输出的尾随连字符，没有改引擎的状态序列格式。

ASLR证据已严格区分：最终Python启动器personality为0，fuzzer实际由setarch -R启动，其自身检查确认ADDR_NO_RANDOMIZE；不能把早先整进程树运行时的父进程属性作为最终子进程证据。全部边缘、主矩阵和E2E断言继续保留。

## 后续变更：直接使用本机系统命令

用户要求默认使用本机已有的LLVM18.1.3。此轮基线为 `06ef9e7`，不改插桩/状态算法，只调整工具链选择、相应测试和说明。

- 根构建默认`clang`；LLVM构建默认`llvm-config`、`clang`、`clang++`，通过PATH解析，不再从LLVM_BINDIR拼接编译器路径或要求`*-18`命令。wrapper的直接编译fallback也使用非版本化名称。
- `llvm-config --cxxflags/--ldflags`仍提供与系统安装匹配的头文件和链接参数。构建检查实际默认target Clang与llvm-config的主版本匹配；显式CC/CXX、CLANG/CLANGXX和运行时AFL_CC/AFL_CXX覆盖保留。配置戳记录PATH解析结果，避免复用旧前缀的产物。
- 本机`/usr/bin/clang`和`/usr/bin/llvm-config`均是系统包管理的符号链接，其真实程序位于任务开始前已存在的`/usr/lib/llvm-18`。删除该系统目录会同时破坏用户指定保留的系统命令，因此没有将其当额外安装删除。
- 已实际删除上一轮额外解包的`/tmp/stateafl-deps`、下载包`/tmp/stateafl-packages`、环境脚本`/tmp/stateafl-env.sh`和旧暂存安装`/tmp/stateafl-install`，再不带工具链覆盖变量执行干净重建。删除及系统包归属证据见`tests/llvm18/evidence/system-toolchain/`。
- 新增四次PATH探针：C/C++各执行版本查询与真实对象编译，探针记录参数后转到原系统编译器。与只检查版本相比，这能发现仍硬编码临时或安装前缀的包装器。所有旧功能回归继续保留。

此轮系统命令复验已全部完成：干净构建成功，IR21/21、边缘11/11、工具链40/40；内存、全部I/O、SHM/TLSH/MVP与校准通过；45秒443执行、queue13、状态0–8、stability100%、crash/hang0。临时路径确已删除且五个新产物均无该前缀。原共享协议与状态算法没有变化；全部原环境变量和覆盖机制保留。新证据及独立SHA256清单位于`tests/llvm18/evidence/system-toolchain`。
