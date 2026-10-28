# Full / Repair-Enhanced Monotone Cut 检查清单

> 本文件是修改 Full、repair、RSP/Exact 控制流、测试数据或给出性能结论前的强制检查基准。
>
> 任何与本文不一致的代码、命令、数据或日志，都必须先解释并更新本文，不能直接沿用旧结论。

## 1. 当前基准状态

- 基准日期：2026-10-28。
- repair 触发修复基线提交：`d0a4435 Preserve repair activation after incumbent updates`。
- 主要实现：`src/algorithm/lifted_two_phase.cpp`。
- Full 是 `Lifted Two-Phase + RSP + Repair-Enhanced Monotone Feasibility Cuts`。
- Repair 与强化 Monotone Cut 作为一个完整组件，不拆开进行主消融。
- Full 和 Lifted Two-Phase 默认都开启 RSP；No-RSP 只能作为明确标注的附加实验。

## 2. 固定机制验证实例

### 2.1 实例标识

| 维度 | 固定值 |
|---|---:|
| 数据目录 | `data/tower_search4_n5_r26_b50_data` |
| 命令行数据名 | `tower_search4_n5_r26_b50` |
| 线路数 `R` | 26 |
| 场景数 `N` | 5 |
| 候选设施数 `F` | 40 |
| 电池档位步长 | 50 kWh |
| 电池范围 | 50--600 kWh |
| energy scale | 1.0 |
| 场景概率 | 每个场景 0.2 |
| Phase 2 场景并行 | 3 workers × 4 threads |
| Master 线程数 | 12 |
| 总墙钟时限 | 7200 s |
| Phase 2 外层目标 gap | 1% |
| `solverCfg.txt` MIP gap | `0.0001`，即 0.01% |

不得将这个实例误写为 R28、20 kWh、其他 `top-N`、其他能耗比例或其他场景组合。

### 2.2 场景顺序与角色

场景顺序必须保持如下配置，`base` 必须是第一个场景：

| 索引 | 文件夹 | 角色 | buses | routes | trips |
|---:|---|---|---:|---:|---:|
| 0 | `base` | 原始真实路网基准 | 242 | 26 | 2788 |
| 1 | `charger_tengah_up10` | 单区域上调扰动 | 253 | 26 | 2851 |
| 2 | `lo10pct` | 整体下调扰动 | 231 | 26 | 2513 |
| 3 | `route_984_up20` | 单线路上调扰动 | 246 | 26 | 2821 |
| 4 | `route_941_down30` | 单线路下调扰动 | 241 | 26 | 2757 |

每个场景还应满足：

- `stations=493`；
- `plug_in_stations=5`；
- `energy_per_km=1.62`；
- 不含线路 452 和 453；
- 不允许用其他场景替换后仍沿用本实例的旧日志或结论。

### 2.3 容量实验目标

| 容量 | 角色 | Full 需要达到的目标 |
|---:|---|---|
| C4 | 较容易/弱耦合基准 | 7200 s 内收敛；优先争取 1800 s 内收敛；相对 L2P 不应出现明显退化。repair 不触发或优势不明显可以接受。 |
| C3 | 核心机制验证 | 7200 s 内收敛；Full 的 LB 必须优于同时间 L2P，最好同时获得有限且较紧的 UB。这是证明 repair-enhanced cut 有效的关键容量。 |
| C2 | 强耦合压力测试 | 不强制收敛，但在相同墙钟时间下 Full 的 LB 必须优于 L2P；只能在 C4/C3 流程固定后运行。 |

任何“Full 优于 L2P”的结论必须同时满足：相同数据、容量、时间上限、RSP设置、线程设置、电池档位、候选设施数和代码版本。

## 3. Phase 2 正常控制流

### 3.1 RSP 到 Exact

每轮顺序为：

1. 求解 Master，得到当前候选资源设计。
2. 对全部场景求解 RSP。
3. 如果至少一个场景 RSP 被严格证明不可行：
   - 为这些场景添加 RSP 可行性 cut；
   - 按第 4 节规则更新连续失败计数并可能执行 repair；
   - 当前候选已经被拒绝，不再为本候选执行常规 Exact SP；
   - repair 组合方案的全场景 UB 验证属于另一条 Exact 路径，不能与常规 Exact 混淆。
4. 如果所有场景 RSP 均可行：
   - 可以添加需要的 RSP 最优性 cut；
   - 即使新增了 RSP 最优性 cut，仍必须继续执行当前候选的 Exact SP；
   - 不能仅因新增 RSP cut 就提前返回 Master。
5. Exact SP 严格不可行时添加普通或 repair-enhanced monotone feasibility cut；Exact 可行时才可用于当前候选 UB 和最优性 cut。

### 3.2 状态证据

- `PROVEN_INFEASIBLE`：仅求解器严格报告不可行时成立，可以更新不可行锚点。
- `FEASIBLE`：找到满足全部约束的整数可行运营解即可；repair probe 使用零目标，因此不要求运营成本最优。
- `UNKNOWN`：超时、数值状态或其他未定状态。不能当作不可行，不能更新不可行锚点。
- RSP 可行不代表 Exact MIP 可行。
- repair probe 得到的可行设计与当前 Master 候选是不同设计。

## 4. Repair 触发时机

### 4.1 连续失败计数

对每个场景独立维护 `unresolvedFailures[scenario]`：

1. 同一场景第一次被 RSP 或 Exact 严格证明不可行：`failures=1`，只加常规 cut，不 repair。
2. 同一场景第二次连续严格不可行：`failures=2`，激活 repair。
3. 激活后，只要该场景仍连续严格不可行：`failures=3,4,5,...`，每一轮都 repair。
4. RSP 不可行和 Exact 不可行使用同一个场景计数；同一轮通常只计一次，因为 RSP 不可行后不会再走该候选的常规 Exact。
5. repair 得到强化 cut、场景可行设计或全局 UB，均不得清零连续失败计数。
6. repair UB 是另一个资源设计的可行性证据，不能证明当前 Master 搜索路径的连续不可行已经结束。
7. 只有正常流程中该场景的连续失败被打断，或当前候选的 Exact SP 确认可行时，才清零该场景计数。
8. `UNKNOWN` 不增加不可行计数，也不能作为 repair 激活证据。

### 4.2 必须出现的日志序列

若同一场景连续失败，正确日志应类似：

```text
iter k:     failures=1, repair_probes=0
iter k+1:   failures=2, Repair ...
iter k+2:   failures=3, Repair ...
iter k+3:   failures=4, Repair ...
```

即使 `iter k+1` 或 `iter k+2` 更新了 `repair_ub`，下一轮连续失败也必须继续显示递增的 `failures`，不能重新从 1 开始。

## 5. 当前实际 Repair 搜索逻辑

### 5.1 通用原则

- 输入是当前场景下已被严格证明不可行的 Master 资源配置。
- probe 复用 Exact SP 模型，通过固定资源并将目标暂时设为 0，只判断整数可行性。
- probe 完成或异常退出后必须恢复原目标和原资源固定状态。
- 所有测试配置只存在于 repair 内，不得直接写入 Master。
- 始终维护一个已被严格证明不可行的 `infeasibleAnchor`。
- `accepted_*` 表示“接受进不可行锚点”，不是“可行方案所需资源”。

### 5.2 电池阶段：单档粗粒度搜索

1. 仅考虑当前未达到上限的线路。
2. 每条线路作为一个资源项，不把多个档位拆成多个排序项。
3. 按下一档的电池增量成本升序排序；当前成本结构下主要等价于按车辆数升序，同成本按线路索引排序。
4. 将全部可升级线路各提高一个离散档位，进行第一次零目标 Exact probe。
5. 如果“全部线路升一档”仍严格不可行：
   - 接受这一整层进入不可行锚点；
   - `accepted_battery_steps=可升级线路数`；
   - `accepted_battery_layers=1`；
   - 当前版本不继续升第二个完整电池层，转入设施阶段。
6. 如果“全部线路升一档”可行：
   - 保存该场景可行设计；
   - 依次测试成本排序前缀 `R/2, R/4, ...`；
   - 找到第一个仍严格不可行的前缀后，将该前缀的一档升级接受进不可行锚点并结束电池阶段；
   - 这不是完整二分，也不追求极大不可行点。
7. 如果测试返回 `UNKNOWN`，停止当前强化，保留最后一个已证不可行锚点。

例：`battery_probes=2, accepted_battery_steps=13, accepted_battery_layers=0` 在 R=26 时表示：

- 全部 26 条线路各升一档得到可行设计；
- 成本最低的前 13 条线路各升一档仍不可行；
- 13 个升档被接受进不可行锚点；
- `layers=0` 不代表电池没有更新，只表示完整的一层没有作为不可行层被接受。

### 5.3 设施阶段：递增前缀搜索

1. 设施是当前 Master 候选中 `w_f=0` 的全部候选设施。
2. 当前代码遍历全部 Master 设施，不仅限于当前场景的 relevant facilities。
3. 按设施建设成本升序排序；同成本按设施索引排序。
4. 从电池阶段留下的已证不可行锚点出发，依次测试前缀 `1,2,4,8,...,剩余设施总数`。
5. 每个仍严格不可行的前缀都可被接受；遇到第一个可行前缀后停止。
6. 若全部剩余设施建成后仍不可行，则全部设施都可接受进不可行锚点。
7. 设施是跨场景共享资源；单场景 repair 中的临时设施配置不能直接作为全局 UB。

例：F40 的当前候选已选 27 座时，剩余 13 座设施的搜索顺序是 `1,2,4,8,13`。若 `facility_probes=5, accepted_facilities=13`，表示加满这 13 座后该场景在当前电池锚点下仍严格不可行。

### 5.4 强化 cut 与 UB

- 强化 monotone feasibility cut 使用最终的已证不可行锚点，而不是 repair 中的可行设计。
- 锚点逐项不小于原始不可行 Master 设计。
- 如果没有找到更大的已证不可行锚点，回退到原始 monotone cut。
- 场景 repair 找到的可行设计可以保存，但全局 UB 必须使用统一的共享设施布局。
- 组合不同场景 repair 设计时，先取共享设施并集和各自场景电池，再对全部场景执行 Exact 整数可行性/成本验证。
- 只有全部场景都有整数可行依据时，才能报告有限 repair UB。
- repair UB 更新不得改变连续失败计数。

## 6. 日志字段解释

| 字段 | 含义 |
|---|---|
| `failures` | 该场景当前连续严格不可行次数 |
| `before` | 原始不可行 Master 候选的场景电池配置 |
| `after` | 最终已证不可行锚点的场景电池配置；不显示设施，也不是 repair 可行设计 |
| `battery_probes` | 实际执行的电池候选 Exact feasibility solve 数量；缓存命中不增加 |
| `facility_probes` | 实际执行的设施候选 Exact feasibility solve 数量；缓存命中不增加 |
| `accepted_battery_steps` | 接受进不可行锚点的线路单档升级数量 |
| `accepted_battery_layers` | 接受进不可行锚点的完整全线路电池层数；当前最多为 1 |
| `accepted_facilities` | 接受进不可行锚点的新增设施数量 |
| `accepted_resources` | `accepted_battery_steps + accepted_facilities` |
| `repair_ms` | 该场景 repair 的墙钟时间，不等同于串行累加的总求解器 CPU 时间 |
| `subproblem_ms` | 常规 Exact 或 repair 组合方案的全场景 Exact UB 验证时间；RSP 拒绝当前候选时仍可能非零 |
| `repair_ub` | repair 组合方案经全场景 Exact 验证后的目标值；`inf` 表示没有有效全局 UB |

## 7. 运行前强制检查

### 7.1 Git 与代码

- [ ] 阅读本文件。
- [ ] 记录当前分支、HEAD 和 `git status`。
- [ ] 确认业务代码修改与大量数据工作区修改没有混在同一提交。
- [ ] 检查 `kRepairFailureThreshold == 2`。
- [ ] 检查 RSP 和 Exact 不可行都增加同一个场景计数。
- [ ] 搜索所有 `unresolvedFailures` 清零位置；repair UB 分支不得清零。
- [ ] 检查所有 RSP 可行且新增最优性 cut 时仍进入 Exact。
- [ ] 检查 `UNKNOWN` 没有被归类为不可行。
- [ ] 使用直接 Ninja Release 命令编译，不默认执行 `run.bat`。

直接构建命令：

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat`" && cd /d C:\faq_work\Papers\Two_phase_wireless_charging_and_integer_lshape && cmake.exe -S . -B out/build/x64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe && cmake.exe --build out/build/x64-ninja --config Release"
```

### 7.2 数据检查

- [ ] 确认没有残留 `wireless.exe` 进程。
- [ ] 运行 `data-check`，不能只凭目录名判断实例。
- [ ] 核对五个场景名称、顺序、26条线路、buses/trips、50--600 kWh。
- [ ] 核对 `scenarioCfg.txt` 中五个概率均为 0.2。
- [ ] 若数据内容变化，旧 Full 和 L2P 日志同时失效，必须重新跑双方。

```powershell
.\out\wireless.exe --pb wireless_reform --alg data-check --data tower_search4_n5_r26_b50 --scenario-count 5 --scenario-start 0 --cap 3 --battery-step 50 --dwc-top-n 40 --energy-scale 1 --rsp 1 --rsp-workers 3 --rsp-threads 4
```

### 7.3 C3 Full 正式命令

```powershell
.\out\wireless.exe --pb wireless_reform --alg full --data tower_search4_n5_r26_b50 --scenario-count 5 --scenario-start 0 --cap 3 --battery-step 50 --dwc-top-n 40 --energy-scale 1 --rsp 1 --rsp-workers 3 --rsp-threads 4
```

时间、Master 线程和 MIP gap 来自 `solverCfg.txt`，启动前必须核对日志开头实际打印值。

## 8. 运行中强制检查

- [ ] 第一次连续不可行时 `repair_probes=0`。
- [ ] 第二次连续不可行时日志出现 `failures=2` 和 repair。
- [ ] repair 更新 UB 后，若下一轮仍连续不可行，必须出现 `failures=3`，不能跳过 repair。
- [ ] `battery_probes`、`facility_probes` 与搜索路径一致。
- [ ] `accepted_resources == accepted_battery_steps + accepted_facilities`。
- [ ] 不把 `after` 误认为可行 repair 方案。
- [ ] RSP 拒绝当前候选时若 `subproblem_ms>0`，先检查是否来自 repair UB 的全场景 Exact 验证。
- [ ] 没有有效 UB 时不得报告有限 gap。
- [ ] 到达总时限后确认最终记录包含真实 elapsed、LB、UB、gap 和状态。

## 9. 给出结论前强制检查

- [ ] 只比较相同实例、代码版本、容量、时间限制和线程设置。
- [ ] 同时间比较使用日志中的墙钟时间，不用迭代数代替。
- [ ] 分别报告 Phase 1 时间、Phase 2 Master 时间、RSP 时间、常规 Exact 时间、repair probe 时间和 repair UB 验证时间。
- [ ] 分别报告 Master 迭代数、RSP solve 数、常规 Exact solve 数、repair probe 数和强化 cut 数。
- [ ] Full 的优势必须重点看 LB；仅找到 UB 不能作为 repair-enhanced cut 推进 LB 的证据。
- [ ] 简单实例允许 Full 与 L2P 持平，但困难实例的核心结论必须由同时间 LB 领先支持。
- [ ] 不得用中断日志、旧逻辑日志或不同数据日志替代最终结果。

## 10. 当前日志有效性

### 10.1 旧 C3 Full 日志

`logs/search4_n5_r26_b50_5_cap3/search4_n5_r26_b50_5_cap3_full_wireless_reform_2026-10-28_08-23-37.log`

- 该日志在 repair UB 后错误清空连续失败计数。
- 可用于定位旧 bug，不得作为最终 Full 结果。

### 10.2 触发修复后的中断日志

`logs/search4_n5_r26_b50_5_cap3/search4_n5_r26_b50_5_cap3_full_wireless_reform_2026-10-28_13-20-22.log`

- 已验证：iter 1 出现 `failures=2` 并更新 repair UB；iter 2 继续出现 `failures=3`。
- 证明连续触发修复已经生效。
- 该进程在 iter 2 后按要求人工停止，只能用于流程验证，不能用于性能或最终收敛结论。

## 11. 修改本文的条件

以下任一项变化时，必须同步更新本文并使旧结果失效：

- repair 触发阈值或清零规则；
- 电池 `R, R/2, R/4` 搜索策略；
- 设施 `1,2,4,8,...` 搜索策略；
- 资源排序规则；
- RSP 到 Exact 的提前返回逻辑；
- repair UB 组合与验证方式；
- 场景组成、顺序、线路数、概率或数据内容；
- 电池范围/步长、F、C、线程数、gap 或时间限制。
