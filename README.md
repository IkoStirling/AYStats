# AYStats 0.1

独立本地 Git 仓库，分支 `main`，无远端。实验性首版；根工程 `AY_ENABLE_STATS=ON` 启用，默认 OFF。

每个数值分别选择基础/高级、单值/资源池、持久/中间三个维度。Core 使用 C++23 标准库，不依赖 World、Renderer、UI、脚本、网络或设备。

## 已实现

- 基础属性与当前/容量资源池；高级基础值/有效值、显式乘区、来源身份、覆盖优先级及移除重算。
- int64 精确计算、万分比倍率、向零取整、检查溢出、不可变 DAG、查表、稳定随机域、纯扩展与有界追踪。
- 原子事务、实际变化/超额归属、容量三策略、定时效果、叠层/刷新/替换和有界因果队列。
- owned SoA、AYTask 分块、有限 SSE2 模板、取消/完成对象；快照与有序批量结算、读写冲突分组。
- versioned JSON 定义/配方/运行存档、原子 loose 文件写入、独立作者预览。
- Entity/Sim、World/Entity/组件代次验证、Scene/Host 生命周期、旧 Health 转换、精确字符串脚本读写和复制状态适配。
- 基础 Inspector 字段、修正来源只读视图、recipe JSON/trace 预览及 Editor owner/undo 命令接线。
- 可选真实 D3D11 strict32 GPU 求值、资格查询、融合 shader、缓冲复用、有界等待与 CPU 回退。

## 最小用法

```cpp
#include <AYStats/Stats.h>
#include <stdexcept>
using namespace ayt::stats;

auto schema = Schema::compile({Definition{
    .name="resource", .shape=Shape::Pool, .current=100, .capacity=100}});
if (!schema) throw std::runtime_error(schema.error().message);
auto state = StatSet::create(*schema);
if (!state) throw std::runtime_error(state.error().message);
Mutation consume{statId("resource"), MutationOp::Decrease, 30};
auto result = state->commit(std::span(&consume, 1), state->version());
if (!result) throw std::runtime_error(result.error().message);
// actual == -30; current == 70. Excess records quantities clipped at a boundary.
```

高级容量建议定义独立高级 scalar，再由 pool 的 `capacityFrom` 引用它。攻击/治疗通过资源提交改变 current；装备倍率修改容量属性。pool 的普通属性乘区不会隐式乘大当前量或容量。

复杂护盾/吸血示例见 [Test_Example.cpp](unittest/Test_Example.cpp)；不同语义和 80/80 对 100 的实际归属见 [Test_Batch.cpp](unittest/Test_Batch.cpp)。所有 Result 必须检查，StatSet 由单一模拟 owner 写入。

## 目标与接入

| Link target | 条件 / 用途 |
| --- | --- |
| `AYStats::Core` | 精确标量数值、计划、效果；可独立构建 |
| `AYStats::Batch` | `AY_STATS_BUILD_BATCH=ON`，复用 AYTask scheduler |
| `AYStats::Assets` | `AY_STATS_BUILD_ASSETS=ON`，nlohmann JSON 与 pure authoring |
| `AYStats::Gpu` | Windows、Batch、`AY_STATS_BUILD_GPU=ON`（默认 OFF） |
| `AYStats::Entity` | 根工程 EntityCore、Assets、Batch |
| `AYStats::Scene` / `AYStats::Host` | Scene/EventBus / Application，自动接线新 World |
| `AYStats::Script` / `AYStats::Inspector` | AYScript / AYUI 存在时可选 |

应用通过 `EngineModuleRuntime` 添加 `StatsIntegrationModule`（在 Entity.Runtime 后）及可选 `StatsHostIntegrationModule`，prepare 注册组件后再 seal，shutdown 在 Host 服务销毁前执行。Editor 默认模块组按可选目标接入；自定义无 Scene host 为每个 World 调用 `registerStatsSystem`。

## 构建与验证

从引擎根目录配置本地 toolchain 后可使用标准 CMake：

```powershell
cmake -S AYRuntime/AYStats -B out/build/stats-core -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/stats-core --target AYStats_UnitTests AYStats_StressTests
pwsh -NoProfile -File scripts/tests/run-module-tests.ps1 -Module AYStats -Tier full -BuildDirectory out/build/stats-core -SkipBuild -Audit
```

Assets 需要可发现的 nlohmann_json package。本机便利脚本 [build-local.ps1](tools/build-local.ps1) 定位 VS/Windows SDK；其他机器建议直接配置 toolchain。纯 Core 关闭 BUILD_TESTING、Batch、Assets、Gpu，仅构建 AYStatsCore。

测试层级为 fast、integration、stress、full 与独立 gpu；GPU 不进入 CPU-only full。AYTest 支持 --list、过滤与 case reports；运行器按实际 features 审计用例。性能 benchmark targets 为 EXCLUDE_FROM_ALL；测量方法和原始数据见 [performance.md](docs/performance.md)。

## 首版边界

GPU 只计算可证明 signed32 安全的纯中间量，资源结算和事件在 CPU。Automatic 默认 CPU，按配方/设备实测后可提供阈值。宽乘积、Table/Random/Extension 不降精度：强制 GPU 诊断，Automatic 回退。device/context/job 采用单一提交线程所有权。

运行时模式转换、通用节点作者工具、GPU 常驻权威状态/完整结算、多平台 GPU 与 ResourceManager/cook/pak 尚未提供。网络当前复制完整 runtime state，不提供数值专用 delta 压缩。脚本 facade 借用 World，销毁前解绑；监听者不能直接销毁其 owner。

完整 AYEditor Release 库及文档编辑/撤销/重做/保存重开回归已通过；完整交互式 Editor smoke 和更细性能遥测（GPU timestamps、缓存命中率等）仍需下一轮验证。已独立测量 C++ 分配次数和增量峰值，不将预算估算冒充测量数据。规范见 [design.md](design.md)，执行记录见 [implementation plan](../../AYDocs/AYSTATS-IMPLEMENTATION-PLAN.md)，公开入口可通过引擎 Agent SDK 查询。
