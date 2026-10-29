# LLM-CLOFuzz Offline Validation Tools

本目录包含针对目标项目 `LLM-CLOFuzz`（工控/网络协议模糊测试系统）设计的离线表示校验与模型保存响应校验工具。

严格遵循离线与无网络原则：
- **不调用任何在线 LLM API**，不依赖 API Key；
- **不启动任何 fuzzer、网络目标服务或 benchmark**；
- 具备严格的有界执行、防越界和清晰错误归因能力。

---

## 包含工具

### 1. 表示与消息边界校验工具 (`validate_representation.py`)

用于校验网络协议输入文件的三层表示与转换一致性：
- **replay 表示**：4 字节无符号小端整数长度前缀（`uint32_t`）+ 消息负载（`payload`），支持多条消息连续存放。
- **raw 表示**：所有消息负载直接级联（纯数据流），无 4 字节长度前缀。
- **region 表示**：闭区间 `[start_byte, end_byte]` 结构（定义于 `aflnet.h:9-15`），严格指向 `raw` 流中的消息偏移。

#### 命令行用法

```bash
# 校验 replay 文件并与参考 raw 文件逐字节对比
python tools/offline_validation/validate_representation.py --replay path/to/seed.replay --raw path/to/seed.raw

# 从 replay 导出 raw 文件与 region JSON 映射
python tools/offline_validation/validate_representation.py \
  --replay path/to/seed.replay \
  --output-raw path/to/out.raw \
  --output-regions path/to/regions.json \
  --json-output path/to/report.json
```

#### 退出码说明

| 退出码 | 常量名 | 含义与归因 |
|---|---|---|
| 0 | `SUCCESS` | 校验通过，replay/raw/region 逐字节与边界完全一致 |
| 1 | `CLI_ERROR` | 参数错误、文件不存在或读取权限失败 |
| 2 | `TRUNCATED_HEADER` | replay 尾部长度头不完整（不足 4 字节） |
| 3 | `OVERFLOW_LENGTH` | 声明的长度超出文件剩余字节数（消息负载被截断） |
| 4 | `ZERO_LENGTH_MESSAGE` | 消息声明长度为 0（破坏 AFLNet 消息非空约束） |
| 5 | `TRAILING_GARBAGE` | 声明消息解析完毕后存在未归属尾部残余字节 |
| 6 | `EMPTY_FILE` | 输入文件大小为 0（未指定 `--allow-empty` 时） |
| 7 | `RAW_MISMATCH` | 生成的 raw 与参考 raw 文件存在字节差异或长度不符 |
| 8 | `RESOURCE_EXCEEDED` | 文件大小或消息数量超出安全上限 |

---

### 2. 模型保存响应与语法校验工具 (`validate_saved_responses.py`)

用于严格离线校验已保存的 LLM 响应 JSON、语法模板（Grammar）及干预建议：
- 防范 ChatAFL 源码审计中发现的关键隐患（C01 缓冲区溢出、C02 短串越界读、C03 空指针解引用、C06 未经校验直接入表、C10 转义截断）。
- 校验字段类型、空值（Null）、数组非空约束、嵌套层级上限（防 DoS 递归炸弹）、字符串安全长度及占位符匹配。

#### 命令行用法

```bash
# 自动识别 schema 并校验单个或多个 JSON 文件
python tools/offline_validation/validate_saved_responses.py path/to/response1.json path/to/response2.json

# 指定模式校验（api_chat, api_completion, grammar, message_types, stagnation）
python tools/offline_validation/validate_saved_responses.py \
  --schema grammar \
  path/to/grammar.json \
  --json-output path/to/report.json
```

#### 退出码说明

| 退出码 | 常量名 | 含义与归因 |
|---|---|---|
| 0 | `SUCCESS` | 结构完整，符合 schema 约束，类型安全 |
| 1 | `CLI_ERROR` | 参数或文件读取错误 |
| 2 | `INVALID_JSON_SYNTAX` | JSON 语法错误、非法控制字符、嵌入 NUL 字节或无效转义 |
| 3 | `SCHEMA_VIOLATION` | 缺少必需字段、字段类型不匹配（如要求 list 传入 str） |
| 4 | `NULL_VALUE_ERROR` | 必需字段或关键内容为 null（防 C 代码空指针解引用） |
| 5 | `CONSTRAINT_VIOLATION` | 数组为空、消息头标识符非法、占位符语法残缺 |
| 6 | `RESOURCE_EXCEEDED` | 字符串超长、文件超限或嵌套层级过深（防 C01 溢出与栈爆） |

---

## 自动化测试套件

测试套件位于 `tests/offline_validation/`。

### 离线运行方式

```powershell
# 运行统一测试脚本（包含 fixture 生成与全部 46 个测试用例）
python tests/offline_validation/run_offline_validation_tests.py

# 或者使用 Python 内置 unittest 模块运行
python -m unittest discover -s tests/offline_validation -p "test_*.py"
```
