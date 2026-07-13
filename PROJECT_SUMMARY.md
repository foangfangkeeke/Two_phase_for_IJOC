# Project Summary: Wireless Charging Facility Deployment and Adaptive Battery Configuration

For implementation status, CLI commands, and experiment execution details, see [handoff.md](handoff.md).

## 1. Research problem and modeling interpretation

需求不确定性下的无线充电设施选址 + 场景条件电池容量配置，建模为面向中期服务需求不确定性的二阶段随机规划。随机性来自未来中期需求水平，而不是日常运营扰动。每个需求场景先被操作化为一套代表性服务计划（发车频率、时刻表、车辆链、车队规模和路线级运营强度），再用于评估该需求水平下的电池容量和充电运营成本。

无线充电设施选址 `w` 是唯一跨场景共享的 here-and-now 战略决策；电池容量 `z_ω` 是给定需求场景及其代表性服务计划后的 lifted scenario-dependent recourse decision；日常充电调度 `y_ω` 是给定设施和电池后的运营响应。

Plug-in/快充设施是既有 exogenous charging opportunities，只作为运营子问题中的容量约束出现，不属于战略 facility-location decision。本文待选址的战略设施仅为无线充电站点/路段。

## 2. Decision hierarchy and scenario definition

### 2.1 Decision timing

```
Facility planning stage:
    future medium-term service plans are uncertain;
    wireless charging facilities are selected as cross-scenario here-and-now decisions.

Service-plan realization stage:
    one demand/service-plan scenario is realized;
    its timetable, vehicle chains, fleet size, and operating intensity are generated.

Fleet-battery configuration stage:
    battery capacities are optimized conditionally on the realized service-plan scenario.

Operational stage:
    charging schedules are optimized given the facility layout and battery configuration.
```

电池容量不会每天变动；它随中期服务计划调整，而非随每日排班波动。

### 2.2 Decision hierarchy

| 层级 | 决策 | 场景关系 | 算法位置 |
|------|------|---------|---------|
| 战略 | 无线充电设施选址 `w` | 跨需求场景共享，here-and-now | Master |
| 战术 | 电池容量配置 `z_ω` | 场景相关 adaptive recourse | Master（lifted） |
| 运营 | 充电调度 `y_ω` | 给定 `w` 和 `z_ω` 后优化 | Sub-problem |

### 2.3 Scenario definition

场景是 **demand scenario**（中期需求场景），不是日常运营噪声。时刻表和车辆链不是随机源本身，而是需求场景的运营层具象化：需求水平变化 → 线路发车频率变化 → 重新生成 timetable、trip chain、最小车队规模和线路能耗。实际每日排班可以围绕该代表性服务计划浮动；模型只用该代表性服务计划刻画对应中期需求水平下的车队与电池需求。

> **A scenario is not a battery plan.** A scenario represents a future medium-term demand/service plan. The battery configuration is not prescribed by the scenario — it is optimized conditionally under that scenario and the shared facility layout.

场景生成流程：

```
base 需求水平
  → 设定线路发车频率/服务强度
  → 生成代表性 timetable
  → 生成 trip chain（固定 5 min turnaround / dwell-time 假设）
  → 最小车队分配 → 得到 base scenario
  → 修改选定线路发车频率（模拟不同中期需求水平）
  → 重新生成 timetable、trip chain 和车队 → 得到 alternative scenario
```

不同场景之间可能变化：线路频率、timetable、trip chain、车队规模、能耗需求。场景概率用于加权不同中期需求水平下的期望成本；方法不依赖特定概率分配。

## 3. Mathematical formulation

### 3.1 Decision variables

**Master Problem**（算法位置；不等同于全部 first-stage）：
- `w_station_s ∈ {0,1}` — 是否在站点 s 建设静态无线充电设施（first-stage，跨场景共享）
- `w_link_l ∈ {0,1}` — 是否在路段 l 建设动态无线充电设施（first-stage，跨场景共享）
- `z_{ω,r,cap} ∈ {0,1}` — 场景 ω 下路线 r 配置容量为 cap 的电池（lifted scenario-dependent recourse，每场景独立）

**Sub-problem**（每个场景 ω 的代表性运营日）：
- 各 EB 的充电量、电池能量状态（连续）
- `x_{eb,trip,station,time} ∈ {0,1}` — EB 在 plug-in/快充 charger 处的 scheduling binary

### 3.2 Objective and cost scale

```
min Σ facility_cost × w
  + Σ_{ω} p_ω × [Σ_r Σ_cap n_{ω,r} × c^bat_cap × z_{ω,r,cap}
                + Q_ω(w, z_ω)]
```

目标最小化 charging-infrastructure, battery-configuration, and charging-operation cost。`n_{ω,r}` 是场景 ω 下路线 r 的车辆数，`Q_ω(w,z_ω)` 是代表性运营日的日间充电 + 夜间充电成本。论文中应将所有成本统一到规划期总成本尺度（基础设施和电池按规划年限折算，运营成本按代表性日成本 × 规划期运营天数折算）。车辆购置成本作为场景外生常数 `Σ_ω p_ω C^bus_ω` 在 post-processing 加回，不影响优化器。

### 3.3 Core constraints

1. 每路线每场景选一个电池容量：`Σ_cap z_{ω,r,cap} = 1`
2. 无线充电设施必须已建设：`charge ≤ M × w`（RHS 机制）
3. 电池能量范围：`soc_min × B_{ω,r} ≤ energy ≤ soc_max × B_{ω,r}`，`B_{ω,r} = Σ cap × z_{ω,r,cap}`
4. 能量守恒：到达能量 = 出发能量 - 行驶消耗 + 充电量 × η
5. Plug-in/快充容量：`Σ_{eb} x_{eb,station,time} ≤ charger_count_s`（scheduling binary 约束）

### 3.4 Operational subproblem

```
Q_ω(w,z_ω) = min  Σ_{eb,trip,station} price · charge
               + Σ_{eb} price_night · charge_night
s.t.
     soc_min × B_{ω,r} ≤ energy ≤ soc_max × B_{ω,r}
     energy_arr = energy_dep - consumption + charge × η
     charge_static ≤ M × w_station
     charge_dynamic ≤ M × w_link
     charge_fast ≤ rate × Σ_t x_{eb,t,time}
     Σ_{eb} x_{eb,t,time} ≤ charger_count_s
     x_{eb,t,time} ∈ {0,1}
```

Phase 1 求解 route-level relaxation（简化 plug-in/快充容量竞争），提供 valid lower bound，不证明 MIP feasibility。

## 4. Methodology

方法名：**reformulation-enabled partial integer L-shaped method**。

### 4.1 Extensive-form MILP

同时包含 first-stage `w`、scenario-dependent `z_ω`、operational `y_ω`。w 是唯一 here-and-now 决策。

### 4.2 Natural integer L-shaped baseline

```
SP_ω(w) = min_{z_ω,y_ω} C^bat_ω(z_ω) + C^op_ω(y_ω)
```

Master 仅含 `w` + `θ_ω`，子问题联合优化 `z_ω` + `y_ω`。子问题含 battery integers + scheduling binaries → MILP。正式消融实验中，Natural ILS baseline 必须与 proposed method 求解等价 formulation（相同数据、目标、约束和成本缩放），两者只允许在分解结构和割生成策略上存在差异。在 `wireless.cpp` 与 `wireless_reform.cpp` 的模型一致性核验完成前，该等价性应视为实验前置条件，而不是已经验证的结论。

### 4.3 Lifted reformulation

```
SP_ω(w) = min_{z_ω} C^bat_ω(z_ω) + Q_ω(w,z_ω),
Q_ω(w,z_ω) = min_{y_ω} C^op_ω(y_ω)
```

Exact reformulation：`z_ω` 从子问题 lift 到 master，不改变决策 timing。`z_ω` 仍是 scenario-dependent recourse，不施加 nonanticipativity。Lift 后子问题只剩 `y_ω`。

### 4.4 Reformulated master

```
min C^fac(w) + Σ_ω p_ω [C^bat_ω(z_ω) + θ_ω]
s.t. Σ_cap z_{ω,r,cap} = 1    ∀ω,r
     Benders cuts on θ_ω      ∀ω
     w ∈ {0,1}, z_ω ∈ {0,1}
```

每个场景一个 operational subproblem，`θ_ω` 近似 `Q_ω(w,z_ω)`。Master 不是 pure first-stage：含 shared w + lifted scenario-dependent z_ω。

### 4.5 Two-phase method

Lift `z_ω` 后，Phase 1 求解 route-level operational relaxation（简化 plug-in 容量竞争），用 LP dual optimality + Farkas cuts 快速推下界。Phase 2 继承 Phase 1 的 master、cuts 和 warm start，恢复 occupancy integrality 进行 per-scenario MIP verification，添加 feasibility/optimality cuts。Two-phase 是 sequential，不是循环切换。

Lifted reformulation 是 two-phase 的 enabling condition：未 lift 时 Phase 1 仍需面对 battery integers（MILP 子问题或 fractional battery relaxation）。

### 4.6 Strengthened cuts

**Implemented**: monotonicity-based strengthened feasibility cut。条件：`weightPerEnergy=0`（电池容量不影响能耗），opening more facilities / larger batteries only expand the feasible region。

Strengthened no-good cut（MIP infeasible）：
```
Σ_{j: w*_j=0} w_j + Σ_{r, cap>cap*_{ω,r}} z_{ω,r,cap} ≥ 1
```
Indicator 只在打开当前未开的设施或升级电池容量时取正。被当前 infeasible 方案支配的组合（不新增设施、不升级电池）仍不可行，因此继续被该 cut 排除。

**Planned**: monotonicity-strengthened optimality cut。同一 indicator，形式为 `θ_ω ≥ Q_ω(x*) - δ_ω · indicator`。当前实现为 classic Hamming-distance integer optimality cut。

### Benders cuts summary

**Implemented — Phase 1 LP dual optimality cut**（route-level relaxation）：
```
θ_ω ≥ Σ_j π_{ω,j} · w_j + Σ_{r,cap} λ_{ω,r,cap} · z_{ω,r,cap} + constant_ω
```

**Implemented — Phase 1 LP Farkas feasibility cut**：relaxation infeasible → original MIP infeasible（单向）。排除连 relaxation 都不可行的 master 解，不等价于 MIP feasibility verification。

**Implemented — Phase 2 strengthened no-good cut**（MIP infeasible）：
```
Σ_{j: w*_j=0} w_j + Σ_{r, cap>cap*_{ω,r}} z_{ω,r,cap} ≥ 1
```

**Implemented — Phase 2 integer optimality cut**: classic Hamming-distance form（所有变量统一 δ，不区分 facility/battery 方向）。

**Planned（not yet implemented）— Phase 2 monotonicity-strengthened optimality cut**：
```
θ_ω ≥ Q_ω(x*) - δ_ω · [Σ_{j: w_j^*=0} w_j + Σ_{r} Σ_{cap>cap^*_{ω,r}} z_{ω,r,cap}]
δ_ω = Q_ω(x*) - L_ω
```
与 strengthened feasibility cut 使用同一 monotonicity indicator。差异化 δ，仅在新增设施/升级电池方向放松 RHS。

## 5. Contribution summary

| # | Contribution | What it solves |
|---|-------------|---------------|
| 1 | Lifted reformulation | Decomposes joint battery+charging MIP into lifted battery decision + conditional operational subproblem |
| 2 | Reformulation-enabled two-phase framework | Phase 1 LP-relaxed operational response, Phase 2 MIP verification |
| 3 | Monotonicity-based strengthened feasibility and optimality cuts（feasibility implemented; optimality pending implementation） | Keep cuts tight for dominated fewer-facility/lower-capacity solutions and relax them only when facilities are added or battery capacities are increased |

三项构成 **reformulation-enabled partial integer L-shaped method**：一部分 scenario-dependent recourse lift 到 master，其余留在子问题，two-phase 求解，strengthened cuts 加速。

## 6. Experimental and ablation design

### 6.1 Algorithm ladder

| Algorithm | Description |
|-----------|-------------|
| `solver` | Direct extensive-form MILP |
| `natural-ils` | Natural ILS: `w` in master, `z_ω` + `y_ω` in subproblem（naive, no LP pruning） |
| `lifted-ils` | Lifted reformulation, single-phase MIP, classic cuts |
| `lifted-two-phase` | Lifted + two-phase (LP→MIP), classic cuts |
| `full` | Lifted + two-phase + strengthened feasibility and optimality cuts（current implementation: feasibility implemented; optimality pending） |

### 6.2 Ablation logic

| Comparison | Isolates |
|-----------|----------|
| natural-ils → lifted-ils | Effect of lifting `z_ω` |
| lifted-ils → lifted-two-phase | Effect of two-phase structure |
| lifted-two-phase → full | Combined effect of monotonicity-strengthened feasibility and optimality cuts |

The final `lifted-two-phase → full` comparison evaluates the two strengthened cut types as one contribution package. Until the strengthened optimality cut is implemented, any interim comparison isolates only the strengthened feasibility cut.

Natural ILS is a naive baseline（no LP pruning, no relaxed warm start）to keep the comparison clean.

### 6.3 Main comparison and scalability

- Phase A: `full` @ 7200s to find the ceiling instance.
- Phase B: all five algorithms @ 600s scan to find the break point.
- Phase C: main table @ 7200s on 3-4 instances around the break point, ≥3 runs per cell.
- Ablation table: tower_5 @ 7200s.

### 6.4 Required output metrics

Per-run: UB, LB, gap, wall-clock time, iterations, Phase 1 time, Phase 2 time, LP cut count, feasibility cut count, optimality cut count, incumbent status, termination status.

## 7. Paper-writing guidance

### 7.1 SCQA story line

**Situation**: 公交电气化需要建设无线充电设施 + 配置电池。设施是长期战略投资（跨场景 here-and-now），电池是场景适应性决策。评估方案需解典型日充电调度。→ 二阶段适应性规划。

**Complication**: Natural ILS 子问题含 battery integers + scheduling binaries → MILP。Classical LP dual cuts 不能直接表示 mixed-integer recourse，generic integer cuts 的信息量有限。当前开发测试表明，在 600s 筛查时限下，Solver 和 Natural ILS 从 `tower_5` 开始已出现明显的可扩展性困难；正式结论仍需基于统一时限实验。

**Question**: 如何高效求解子问题为 MILP 的适应性设施选址问题？

**Answer**: Reformulation（lift z_ω）→ Two-phase（Phase 1 LP push LB, Phase 2 MIP verify）→ Strengthened cuts（monotonicity-based feasibility and optimality cuts; optimality pending implementation）。

### 7.2 Scenario definition wording

场景是中期 demand/service-plan scenario，不是日常运营噪声。时刻表和车辆链是需求场景的运营具象化，不是随机源本身。场景概率加权期望成本，方法不依赖特定概率分配。

### 7.3 Decision timing wording

- `z_ω` 是 lifted scenario-dependent recourse，不是 first-stage，也不是每日可变决策。
- Master 不是 pure first-stage。
- 设施选址是唯一跨场景战略输出；电池是场景条件战术输出；调度是验证可行性和成本的运营响应。

### 7.4 Plug-in / fast-charger wording

Plug-in/快充是既有外生补能机会，不是 master 中的选址或扩容决策。5 min dwell 是代表性 timetable 生成时的最小 turnaround/recovery time，不是"为了充电额外等 5 分钟"。

### 7.5 Risky wording to avoid

| Avoid | Use instead |
|-------|-------------|
| Battery sizing is one-to-one matched with scenarios | Battery sizing is optimized conditionally on each service-plan scenario |
| Battery sizing is a first-stage decision | Battery sizing is a lifted scenario-dependent recourse decision |
| Master problem = first-stage problem | The master contains first-stage facility + lifted scenario-dependent battery variables |
| Minimize total system cost | Minimize charging-infrastructure, battery-configuration, and charging-operation cost |

### 7.6 Recommended paper structure

```
3.1 Service-plan scenario generation
3.2 Extensive-form MILP formulation
3.3 Natural integer L-shaped decomposition
3.4 Lifted reformulation of scenario responses
3.5 Reformulated master problem and operational subproblems
3.6 Reformulation-enabled partial integer L-shaped method: two-phase algorithm
3.7 Strengthened cuts
```

## 8. Target journals and next paper tasks

Transportation Research Part B, INFORMS Journal on Computing, or Transportation Science。先补齐实验矩阵，再请专家判断。

### Key theoretical propositions

- Reformulated model 与 extensive-form MILP 的 exact equivalence。
- Lifted reformulation is necessary for clean LP-first strategy（未 lift 时必须松弛 battery binaries 或面对 MILP 子问题）。
- Phase 1 relaxation 提供 valid LB，不证明 MIP feasibility。
- Strengthened feasibility cut validity：monotonicity 保证被 infeasible 方案支配的仍 infeasible。
- Strengthened optimality cut validity（planned）：被 feasible 方案支配的 cost ≥ Q*；新增设施/升级电池方向放松 RHS。

## 9. References

- Laporte, G., & Louveaux, F. V. (1993). The integer L-shaped method for stochastic integer programs with complete recourse. *Operations Research Letters*, 13(3), 133–142.
- [Add reference on partial Benders decomposition / partial decomposition]

---

## Historical development results (not final paper results)

当前表混合时间口径（baseline 600s, proposed run-to-convergence），不作为最终实验表。正式论文统一 7200s。

| Dataset | Algorithm | UB | LB | Gap% | Iters | Time | Status |
|---------|-----------|----|----|------|-------|------|--------|
| tower_small (2) | Solver | 9572.05 | 9572.05 | 0 | 1720 nodes | 259.3s | converged |
| | ILS | 9572.05 | 9571.39 | 0.0069 | 115 | 224.9s | converged |
| | Ours | 9572.05 | 9572.05 | 0 | Ph1:6 Ph2:1 | 1.6s | converged |
| tower_medium (2) | Solver | 16203.06 | 15959.49 | 1.50 | 1313 nodes | 600.8s | timeout |
| | ILS | 16155.53 | 16031.30 | 0.77 | 164 | 601.7s | timeout |
| | Ours | 16155.53 | 16155.53 | 0 | Ph1:6 Ph2:1 | 2.5s | converged |
| tower_5 (5) | Solver | inf | 346.78 | — | — | 601.1s | no incumbent |
| | ILS | — | 52725.20 | — | 11 | 601.3s | no incumbent |
| | Ours | 54374.34 | 54372.57 | 0.0032 | Ph1:12 Ph2:2 | 52.8s | converged |
| tower_20 (20) | Solver | — | — | — | — | — | N/A |
| | ILS | — | — | — | — | — | N/A |
| | Ours | 54439.04 | 54436.83 | 0.0041 | Ph1:12 Ph2:3 | 1838.1s | converged |

Key observations: under the recorded development settings, Solver and Natural ILS did not obtain an incumbent on `tower_5` within 600s; the `tower_20` baseline results were not available. The proposed method converged on all recorded instances, including `tower_20` in about 30 minutes, but the mixed time limits mean these results are not a fair final comparison.