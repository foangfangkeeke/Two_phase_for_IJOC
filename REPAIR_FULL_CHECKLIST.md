# Full / Repair-Enhanced Monotone Cut 检查清单

> 本文件是修改 Full、repair、RSP/Exact 控制流、测试数据或给出性能结论前的强制检查基准。
>
> 任何与本文不一致的代码、命令、数据或日志，都必须先解释并更新本文，不能直接沿用旧结论。

## 1. 当前基准状态

- 基准日期：2026-10-31。
- 最终 repair 触发实现提交：`f7648e2 Finalize cumulative repair trigger schedule`。
- 成本导向设施修复基线提交：`f1bc115 Use cost-guided facility repair prefixes`。
- 主要实现：`src/algorithm/lifted_two_phase.cpp`。
- Full 是 `Lifted Two-Phase + RSP + Repair-Enhanced Monotone Feasibility Cuts`。
- Repair 与强化 Monotone Cut 作为一个完整组件，不拆开进行主消融。
- Full 和 Lifted Two-Phase 默认都开启 RSP；No-RSP 只能作为明确标注的附加实验。

## 2. 固定最终实例

### 2.1 C3 机制验证实例

| 维度 | 固定值 |
|---|---|
| 数据目录 | `data/tower_search4_n5_r28_b50_data` |
| 命令行数据名 | `tower_search4_n5_r28_b50` |
| 四维标识 | `R28-N5-F50-C3` |
| 电池范围/步长 | 50--600 kWh / 50 kWh |
| energy scale | 1.0 |
| 场景概率 | 每个场景 0.2 |
| 总墙钟时限 | 7200 s |

固定场景顺序为：

1. `base`；
2. `charger_tengah_up10`；
3. `lo10pct`；
4. `route_984_up20`；
5. `route_941_down30`。

不得替换场景后继续沿用本实例的日志或性能结论。最终 Full 在
2886.826 s 达到 0.950507% gap，UB/LB 为
17886.326205/17716.315484。匹配的 L2P 日志覆盖了相同墙钟截点；
约 2876.602 s 时 UB 仍为无穷，LB 为 17689.064542。

### 2.2 C4 主表实例

主表固定使用 `tower_base` 的前五个场景：

1. `base`；
2. `charger_btbatok_up15`；
3. `global_down10`；
4. `route_453_up15`；
5. `route_941_down15`。

固定主表行为 `R28-N5-F10-C4`、`R28-N5-F20-C4` 和
`R28-N5-F50-C4`。共同设置为 50--600 kWh、50 kWh 步长、
energy scale 1.0、7200 s 时限、3 个场景 worker × 4 个线程；
Master 使用 12 线程。Phase 2 外层 gap 为 1%，求解器 MIP gap 为
0.0001（0.01%）。

最终 Full/L2P 结果为：

| 实例 | Full | Lifted Two-Phase |
|---|---|---|
| `R28-N5-F10-C4` | 364.664 s，gap 0.948357% | 300.077 s，gap 0.396994% |
| `R28-N5-F20-C4` | 547.001 s，gap 0.999663% | 7200.093 s 超时，UB=inf |
| `R28-N5-F50-C4` | 52.312 s，gap 0.295763% | 52.389 s，gap 0.295763% |

F 是候选设施丰富度，不应解释成单调的计算规模指标。F50 新增候选点
使 Phase 1 设计在 Phase 2 首轮即可整数可行，因此它可以比 F10/F20
更快；该现象不是配置错误。

## 3. Phase 2 正常控制流

### 3.1 RSP 到 Exact

每轮顺序为：

1. 求解 Master，得到当前候选资源设计。
2. 对全部场景求解 RSP。
3. 如果至少一个场景 RSP 被严格证明不可行：
   - 为这些场景添加 RSP 可行性 cut；
   - 按第 4 节规则更新 RSP 累计失败计数并可能执行 repair；
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

### 4.1 RSP 累计失败计数与 Exact 立即触发

对每个场景在整个 Phase 2 中独立维护
`rspInfeasibleCount[scenario]`，以及下一次 repair 的累计触发点：

1. 同一场景第一次被 RSP 严格证明不可行时立即 repair。
2. 后续 RSP repair 的累计触发点为 `1, 2, 4, 8, 16, 24, 32, ...`；达到 16 后，触发间隔固定为 8 次严格不可行。
3. repair 得到强化 cut、场景可行设计或全局 UB，均不得清零累计计数。
4. 如果所有 RSP 均可行但当前候选的 Exact SP 严格不可行，则立即 repair，不等待 RSP 累计触发点，也不增加 RSP 失败计数。
5. repair UB 是另一个资源设计的可行性证据，不能证明当前 Master 候选已经可行。
6. Exact SP 可行或不可行都不改变 RSP 累计失败计数；计数在 Phase 2 内不重置。
7. `UNKNOWN` 不增加不可行计数，也不能作为 repair 激活证据。

### 4.2 必须出现的日志序列

若同一场景持续被 RSP 严格证明不可行，正确日志应类似：

```text
failure 1:  Repair ..., next_rsp_repair=2
failure 2:  Repair ..., next_rsp_repair=4
failure 3:  repair_probes=0
failure 4:  Repair ..., next_rsp_repair=8
failure 5--7: repair_probes=0
failure 8:  Repair ..., next_rsp_repair=16
```

即使 repair 更新了 `repair_ub`，后续 RSP 严格不可行仍必须沿用累计计数，
不能重新从 1 开始。日志出现 `trigger=exact-unresolved` 时则表示所有 RSP
均可行后 Exact SP 严格不可行；该路径必须立即 repair，与上述触发点无关。

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

例：`battery_probes=2, accepted_battery_steps=14, accepted_battery_layers=0` 在 R=28 时表示：

- 全部 28 条线路各升一档得到可行设计；
- 成本最低的前 14 条线路各升一档仍不可行；
- 14 个升档被接受进不可行锚点；
- `layers=0` 不代表电池没有更新，只表示完整的一层没有作为不可行层被接受。

### 5.3 设施阶段：成本前缀认证与递增搜索

1. 设施是当前 Master 候选中 `w_f=0` 的全部候选设施。
2. 当前代码遍历全部 Master 设施，不仅限于当前场景的 relevant facilities。
3. 在 Phase 1 前生成一次全局设施顺序：按建设成本升序，同成本按设施索引排序。每轮只从该顺序中删除当前 Master 已建设设施，各场景使用同一个剩余序列。
4. 电池阶段结束后，将修复后的场景电池和当前 Master 已建设设施共同代入本轮 RSP Farkas cut。
5. 若该完整配置仍严格违反 RSP cut，则按全局成本顺序逐座加入设施；仍严格违反 cut 的成本前缀可直接接受进不可行锚点，无需 Exact probe。
6. 第一座使旧 RSP cut 无法继续严格证明不可行的设施不直接写入锚点，而是作为后续 Exact 搜索的第一个边界设施。
7. 从最后一个已证不可行锚点出发，对边界及其后的剩余成本序列测试嵌套前缀 `1,2,4,8,...,剩余设施总数`。
8. Exact 严格证明不可行的前缀可以接受；遇到第一个可行前缀后停止；`UNKNOWN` 安全返回最后一个已证不可行锚点。
9. 若电池修复后旧 RSP cut 已不再严格违反，或没有有效 Farkas 系数，则不做分析认证，完整执行原成本排序的浅层 Exact 搜索。
10. 若全部剩余设施仍被严格证明不可行，则全部设施都可接受进不可行锚点。
11. 设施是跨场景共享资源；单场景 repair 中的临时设施配置不能直接作为全局 UB。

例：F50 的当前候选已选 37 座时，剩余 13 座设施的搜索顺序是 `1,2,4,8,13`。若 `facility_probes=5, accepted_facilities=13`，表示加满这 13 座后该场景在当前电池锚点下仍严格不可行。

### 5.4 强化 cut 与 UB

- 强化 monotone feasibility cut 使用最终的已证不可行锚点，而不是 repair 中的可行设计。
- 锚点逐项不小于原始不可行 Master 设计。
- 如果没有找到更大的已证不可行锚点，回退到原始 monotone cut。
- 场景 repair 找到的可行设计可以保存，但全局 UB 必须使用统一的共享设施布局。
- 各场景设施端点都是同一全局成本序列的前缀，因此它们的共享设施并集等于最长请求前缀；再结合各自场景电池，对全部场景执行 Exact 整数可行性/成本验证。
- 只有全部场景都有整数可行依据时，才能报告有限 repair UB。
- repair UB 更新不得改变 RSP 累计失败计数。

## 6. 日志字段解释

| 字段 | 含义 |
|---|---|
| `rsp_failures` | 该场景在 Phase 2 中累计被 RSP 严格证明不可行的次数 |
| `before` | 原始不可行 Master 候选的场景电池配置 |
| `after` | 最终已证不可行锚点的场景电池配置；不显示设施，也不是 repair 可行设计 |
| `battery_probes` | 实际执行的电池候选 Exact feasibility solve 数量；缓存命中不增加 |
| `facility_probes` | 实际执行的设施候选 Exact feasibility solve 数量；缓存命中不增加 |
| `accepted_battery_steps` | 接受进不可行锚点的线路单档升级数量 |
| `accepted_battery_layers` | 接受进不可行锚点的完整全线路电池层数；当前最多为 1 |
| `accepted_facilities` | 接受进不可行锚点的新增设施数量 |
| `accepted_resources` | `accepted_battery_steps + accepted_facilities` |
| `rsp_cert_checks` | 设施阶段执行的 RSP 仿射证书检查次数 |
| `rsp_cert_hits` | 其中仍严格违反 RSP cut、可证明不可行的次数 |
| `rsp_certified_facilities` | 按成本顺序直接由 RSP cut 认证并接受的设施数 |
| `rsp_deferred_facilities` | 从第一个未认证边界开始交给浅层 Exact 搜索的设施数 |
| `repair_ms` | 该场景 repair 的墙钟时间，不等同于串行累加的总求解器 CPU 时间 |
| `subproblem_ms` | 常规 Exact 或 repair 组合方案的全场景 Exact UB 验证时间；RSP 拒绝当前候选时仍可能非零 |
| `repair_ub` | repair 组合方案经全场景 Exact 验证后的目标值；`inf` 表示没有有效全局 UB |

## 7. 运行前强制检查

### 7.1 Git 与代码

- [ ] 阅读本文件。
- [ ] 记录当前分支、HEAD 和 `git status`。
- [ ] 确认业务代码修改与大量数据工作区修改没有混在同一提交。
- [ ] 检查 RSP repair 初始触发点为 1，后续为 2、4、8、16、24……。
- [ ] 检查只有 RSP 严格不可行增加 `rspInfeasibleCount`。
- [ ] 检查 Phase 2 中不存在 RSP 累计计数清零路径。
- [ ] 检查 Exact 严格不可行立即 repair，但不增加 RSP 累计计数。
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
- [ ] 核对五个场景名称、顺序、28 条线路、buses/trips、50--600 kWh。
- [ ] C3 机制实例核对 `scenarioCfg.txt` 中五个概率均为 0.2。
- [ ] C4 主表核对 `tower_base` 前五个场景与第 2.2 节一致。
- [ ] 若数据内容变化，旧 Full 和 L2P 日志同时失效，必须重新跑双方。

```powershell
.\out\wireless.exe --pb wireless_reform --alg data-check --data tower_search4_n5_r28_b50 --scenario-count 5 --scenario-start 0 --cap 3 --battery-step 50 --dwc-top-n 50 --energy-scale 1 --rsp 1 --rsp-workers 3 --rsp-threads 4
```

### 7.3 C3 Full 正式命令

```powershell
.\out\wireless.exe --pb wireless_reform --alg full --data tower_search4_n5_r28_b50 --scenario-count 5 --scenario-start 0 --cap 3 --battery-step 50 --dwc-top-n 50 --energy-scale 1 --rsp 1 --rsp-workers 3 --rsp-threads 4
```

时间、Master 线程和 MIP gap 来自 `solverCfg.txt`，启动前必须核对日志开头实际打印值。

## 8. 运行中强制检查

- [ ] 第一次 RSP 严格不可行时日志出现 `rsp_failures=1`、repair 和 `next_rsp_repair=2`。
- [ ] RSP 累计失败次数 2、4、8、16、24……时触发 repair，其他次数不触发。
- [ ] repair 更新 UB 后不得重置 RSP 累计失败次数。
- [ ] Exact SP 可行或不可行都不得重置或增加 RSP 累计失败次数。
- [ ] 所有 RSP 可行但 Exact SP 严格不可行时，立即出现 `trigger=exact-unresolved` repair。
- [ ] `battery_probes`、`facility_probes` 与搜索路径一致。
- [ ] RSP 认证设施前缀严格按全局建设成本顺序；第一个未认证设施是 Exact 前缀的第一个点。
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

只保留 `results/final/full_l2p/logs` 中的八份最终对比日志：

- `R28-N5-F50-C3_full.log`；
- `R28-N5-F50-C3_lifted-two-phase.log`；
- `R28-N5-F10-C4_full.log`；
- `R28-N5-F10-C4_lifted-two-phase.log`；
- `R28-N5-F20-C4_full.log`；
- `R28-N5-F20-C4_lifted-two-phase.log`；
- `R28-N5-F50-C4_full.log`；
- `R28-N5-F50-C4_lifted-two-phase.log`。

C4 的六份机器可读摘要保存在
`results/final/full_l2p/summaries`。其他历史、诊断、中断及旧逻辑日志
均已清除，不得从外部旧副本恢复后混入当前结果。

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

## 12. R28 正式实验的无 DWC 可行性 gate

R28 的正式性能实验开始前，必须先在相同场景、容量、电池档位和线程
设置下验证完全不建设 DWC 时原整数问题仍然可行。该检查防止把数据本身
超出 plug-in 与电池资源上限造成的不可行误判为算法困难。

当前 R28-N5 固定场景顺序为：

1. `base`；
2. `charger_btbatok_up15`；
3. `global_down10`；
4. `route_453_up15`；
5. `route_941_down15`。

使用独立的零目标 Exact-MIP 诊断，不运行 Master、RSP、cut 或 repair：

```powershell
.\out\wireless.exe --alg no-dwc-feasibility --data tower_base --scenario-count 5 --scenario-start 0 --cap 4 --battery-step 50 --energy-scale 1 --rsp-workers 3 --rsp-threads 4
```

该诊断强制日志中的 `wireless_option=0`，不创建 DWC 变量，将各线路电池
固定为物理最大档位，并保留共享 plug-in 容量及占用二进制变量。目标函数
恒为 0。`--rsp-workers` 和 `--rsp-threads` 在这里仅复用现有场景并行参数，
并不构建或求解 RSP。只有 `has_solution=1` 才构成场景可行证据；只有
`INFEASIBLE` 才构成严格不可行证据，超时或数值状态且无解时记为未知。

2026-10-29 的 R28-N5/C4/50-kWh 基准验证结果：

- 电池范围为 50--600 kWh，诊断固定所有线路为 600 kWh；
- 采用 3 个场景 worker、每个 4 个 COPT 线程；
- `base`、`charger_btbatok_up15`、`global_down10`、
  `route_453_up15` 和 `route_941_down15` 均返回 `OPTIMAL` 且
  `has_solution=1`；
- 五个场景各自求解约 1.21--1.56 秒；模型构建及求解的总墙钟时间约
  9.030 秒；
- 汇总结果为 `FEASIBLE`。该零目标诊断不计算原问题目标值、UB 或 gap。

此前完整 `wireless_reform_no_wpt + Full` 流程也曾得到零设施的有限可行
解，可作为交叉检查；正式 gate 统一使用上述独立诊断，避免把 Master、cut
和 repair 性能混入数据可行性判断。

因此，该 R28-N5/C4/50-kWh 场景组在完全不建设 DWC 时具有整数可行解。
该结果是数据可行性 gate，不是 DWC 算法的性能对比结论。若场景顺序、
电池上限、C、充电窗口或能耗参数变化，必须重新执行本检查。

## 13. 当前 R28-N5 固化状态

- C3 机制实例固定为 `tower_search4_n5_r28_b50 / F50 / C3`。
- C4 主表固定为 `tower_base / N5 / C4` 的 F10、F20、F50 三行。
- `scripts/experiments/benchmark_instances.csv` 已将上述 C4 三行的
  `lifted-two-phase;full` 标记为完成；EF、Natural ILS 和 Lifted ILS
  仍保持待运行状态。
- 正式 Full 代码基线为 `f7648e2`；资源排序和 RSP 设施前缀认证基线为
  `f1bc115`。
- 数据、算法逻辑、线程数、gap、时限或候选设施集合一旦变化，八份保留
  日志必须整体视为失效，不能只重跑 Full 或只重跑 L2P。
