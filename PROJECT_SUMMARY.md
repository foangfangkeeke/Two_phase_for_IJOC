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

Although battery configuration and charging operation are physically distinct decisions, no additional uncertainty is revealed between them. Both are conditioned on the same realized service-plan scenario and therefore belong to the second-stage recourse. The model is thus a two-stage stochastic program: first-stage facility location, second-stage battery configuration and charging operation.

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

```text
min sum_j c^fac_j * w_j
  + sum_omega p_omega * [sum_r sum_cap n_{omega,r} * c^bat_day * cap * z_{omega,r,cap}
                         + Q_omega(w, z_omega)]
```

The objective is evaluated on a representative operating day and all monetary parameters use USD. Facility costs are daily-equivalent costs: plug-in and static wireless chargers use USD/day, dynamic wireless chargers use USD/km/day, and inverter cost uses USD/kW/day. Battery capacity cost uses USD/kWh/day in the master problem. The current data set uses the external benchmark battery purchase price of 108 USD/kWh amortized over the 2-year battery life used by Wang et al. (2024): 108 / (2 * 365) = 0.14795 USD/kWh/day, rounded to 0.148. Day and night charging prices are 0.1922 and 0.1233 USD/kWh.

`n_{omega,r}` is the number of vehicles on route r under scenario omega, and `Q_omega(w,z_omega)` is the representative-day charging-operation cost. Bus purchase cost remains an exogenous scenario constant and can be added in post-processing if needed; it does not affect optimization decisions.

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

Phase 1 求解 route-level relaxation：relax the cross-route plug-in/fast-charger capacity coupling（将共享容量约束松弛为 route-specific），retain route-local energy、charging、and battery constraints。提供 valid lower bound，不证明 MIP feasibility。

## 4. Methodology

方法名：**reformulation-enabled partial integer L-shaped method**。

### 4.1 Extensive-form MILP

同时包含 first-stage `w`、scenario-dependent `z_ω`、operational `y_ω`。w 是唯一 here-and-now 决策。

### 4.2 Natural integer L-shaped baseline

```
SP_ω(w) = min_{z_ω,y_ω} C^bat_ω(z_ω) + C^op_ω(y_ω)
```

Master 仅含 `w` + `θ_ω`，子问题联合优化 `z_ω` + `y_ω`。子问题含 battery integers + scheduling binaries → MILP。正式消融实验中，Natural ILS baseline 与 proposed method 求解等价 formulation（相同数据、目标、约束和成本缩放，已验证）。两者只允许在分解结构和割生成策略上存在差异。

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

Lift `z_ω` 后，Phase 1 求解 route-level operational relaxation（relax cross-route plug-in capacity coupling to route-specific capacity），用 LP dual optimality + Farkas cuts 快速推下界。Phase 2 继承 Phase 1 的 master、cuts 和 warm start，恢复 occupancy integrality 进行 per-scenario MIP verification，添加 feasibility/optimality cuts。Two-phase 是 sequential，不是循环切换。

Lifted reformulation 是 two-phase 的 enabling condition：未 lift 时 Phase 1 仍需面对 battery integers（MILP 子问题或 fractional battery relaxation）。

### 4.6 Strengthened cuts

Monotonicity-based strengthened feasibility cut。条件：`weightPerEnergy=0`（电池容量不影响能耗），opening more facilities / larger batteries only expand the feasible region。

Strengthened no-good cut（MIP infeasible）：
```
Σ_{j: w*_j=0} w_j + Σ_{r, cap>cap*_{ω,r}} z_{ω,r,cap} ≥ 1
```
Indicator 只在打开当前未开的设施或升级电池容量时取正。被当前 infeasible 方案支配的组合（不新增设施、不升级电池）仍不可行，因此继续被该 cut 排除。

Monotonicity-strengthened optimality cut，使用与 feasibility cut 相同的 direction-selective indicator。形式为 `θ_ω ≥ Q_ω(x*) - δ_ω · indicator`，其中 `δ_ω = Q_ω(x*) - L_ω`，`L_ω` 是对 master 所有可行 `(w, z_ω)` 都有效的 scenario-wide global lower bound。Indicator 包含当前解中未建设的设施（`w_j^* = 0`）和高于当前所选容量的电池选项（`cap > cap^*_{ω,r}`）。当不新增设施且不升级电池时 indicator = 0，cut 保持 `θ_ω ≥ Q_ω(x*)`；仅在当前关闭的设施被打开或选择更大电池容量时 indicator 取正，从而放松 RHS。由 monotonicity，这些变化不会增加最优运营 recourse cost `Q_ω`（注意是运营成本，不含电池投资成本）。

### Benders cuts summary

**Phase 1 LP dual optimality cut**（route-level relaxation）：
```
θ_ω ≥ Σ_j π_{ω,j} · w_j + Σ_{r,cap} λ_{ω,r,cap} · z_{ω,r,cap} + constant_ω
```

**Phase 1 LP Farkas feasibility cut**：relaxation infeasible → original MIP infeasible（单向）。排除连 relaxation 都不可行的 master 解，不等价于 MIP feasibility verification。

**Phase 2 classic feasibility cut**（used by `lifted-ils` and `lifted-two-phase`）：
```
Σ_{j: w*_j=0} w_j + Σ_{r,cap: z*_{ω,r,cap}=0} z_{ω,r,cap} ≥ 1
```

**Phase 2 classic integer optimality cut**（used by `lifted-ils` and `lifted-two-phase`）：classic Hamming-distance form（所有变量统一 δ，不区分 facility/battery 方向）。

**Phase 2 monotonicity-strengthened feasibility cut**（used by `full`）：
```
Σ_{j: w*_j=0} w_j + Σ_{r, cap>cap*_{ω,r}} z_{ω,r,cap} ≥ 1
```
Indicator 仅在未开设施和更大电池方向取正；被当前 infeasible 方案支配的组合仍不可行，因此继续被排除。

**Phase 2 monotonicity-strengthened optimality cut**（used by `full`）：
```
θ_ω ≥ Q_ω(x*) - δ_ω · [Σ_{j: w_j^*=0} w_j + Σ_{r} Σ_{cap>cap^*_{ω,r}} z_{ω,r,cap}]
δ_ω = Q_ω(x*) - L_ω
```
与 strengthened feasibility cut 使用同一 monotonicity indicator。Indicator 在未建设施和更大电池方向取正，当前配置方向取零，仅在可能改善运营成本的方向上放松 RHS。

## 5. Contribution summary

| # | Contribution | What it solves |
|---|-------------|---------------|
| 1 | Lifted reformulation | Decomposes joint battery+charging MIP into lifted battery decision + conditional operational subproblem |
| 2 | Reformulation-enabled two-phase framework | Phase 1 LP-relaxed operational response, Phase 2 MIP verification |
| 3a | Monotonicity-strengthened feasibility cut | Excludes all solutions dominated by the current infeasible configuration — those with no additional facilities and no larger battery capacities remain infeasible |
| 3b | Monotonicity-strengthened optimality cut | Maintains the Q* lower bound for all dominated feasible configurations; relaxes RHS only in the direction of opened facilities or larger battery capacities |

上述三类贡献共同构成 **reformulation-enabled partial integer L-shaped method**：一部分 scenario-dependent recourse lift 到 master，其余留在子问题，two-phase 求解，strengthened cuts 加速。

## 6. Experimental and ablation design

### 6.1 Algorithm ladder

| Algorithm | Description |
|-----------|-------------|
| `solver` | Direct extensive-form MILP |
| `natural-ils` | Natural ILS: `w` in master, `z_ω` + `y_ω` in subproblem（naive, no LP pruning） |
| `lifted-ils` | Lifted reformulation, single-phase MIP, classic cuts |
| `lifted-two-phase` | Lifted + two-phase (LP→MIP), classic cuts |
| `full` | Lifted + two-phase + strengthened feasibility and optimality cuts |

### 6.2 Ablation logic

| Comparison | Isolates |
|-----------|----------|
| natural-ils → lifted-ils | Effect of lifting `z_ω` |
| lifted-ils → lifted-two-phase | Effect of two-phase structure |
| lifted-two-phase → full | Combined effect of monotonicity-strengthened feasibility and optimality cuts |

The `lifted-two-phase → full` comparison evaluates the combined effect of the monotonicity-strengthened feasibility and optimality cuts.

Natural ILS is a naive baseline（no LP pruning, no relaxed warm start）to keep the comparison clean.

### 6.3 Main comparison and scalability

The computational section should use two two-dimensional tables.

**Table 1. Network-scale ablation under three service-plan scenarios**

| Instance | Solver | Natural ILS | Lifted ILS | Lifted Two-phase | Full |
|----------|--------|-------------|------------|------------------|------|
| `tower_small` | 5.37s | TL, 63.15% | TL, 52.60% | 0.44s | 0.36s |
| `tower_medium` | 967.18s | - | - | 6.04s | 5.02s |
| `tower` | OOM, 4.18% | - | - | 66.48s | 58.13s |

Notes:
- The table reports wall-clock time for converged runs. For non-converged runs, the final global gap is reported.
- `-` indicates not run because the method failed to close the gap on a smaller instance within the time limit.
- `TL` indicates time limit with a finite incumbent.
- `OOM` indicates Gurobi error 10001 (out of memory) with a finite incumbent; the reported gap is based on the final incumbent and bound.
- The `tower` row uses the corrected full-method log after the Phase-2 outer-gap check fix.

**Table 2. Scenario-scale scalability on the full Tower network**

| # Scenarios | Extensive-form Solver | Lifted Two-phase ILS | Full Method |
|------------:|-----------------------|----------------------|-------------|
| 3 | see Table 1 | see Table 1 | see Table 1 |
| 5 |  |  |  |
| 10 |  |  |  |
| 20 |  |  |  |
| 40 |  |  |  |

Notes:
- The 3-scenario row reuses the full-network results from Table 1.
- For scenario-scale experiments, the extensive-form solver may use a memory-conservative configuration when the default configuration runs out of memory. The same wall-clock limit and hardware budget are retained.
- A blank cell indicates that the run has not yet been completed.
- Reported entries use the format `time` for converged runs and `status, gap` for non-converged runs with a finite incumbent.

### 6.4 Required output metrics

Per-run: UB, LB, global gap, total wall-clock time, master solve time, Phase 1 time, Phase 2 time, iteration count, LP cut count, feasibility cut count, optimality cut count, incumbent status, model-level termination status, outer-loop termination status.

## 7. Paper-writing guidance

### 7.1 SCQA story line

**Situation**: 公交电气化需要建设无线充电设施 + 配置电池。设施是长期战略投资（跨场景 here-and-now），电池是场景适应性决策。评估方案需解典型日充电调度。→ 二阶段适应性规划。

**Complication**: Natural ILS 子问题含 battery integers + scheduling binaries → MILP。Classical LP dual cuts 不能直接表示 mixed-integer recourse，generic integer cuts 的信息量有限。随着场景数量和问题规模增长，extensive-form MILP 和 natural ILS 面临日益严重的计算困难——前者包含完整的场景展开 formulation，后者反复求解 mixed-integer recourse 子问题且依赖信息量相对有限的 generic integer cuts。

**Question**: 如何高效求解子问题为 MILP 的适应性设施选址问题？

**Answer**: Reformulation（lift z_ω）→ Two-phase（Phase 1 LP push LB, Phase 2 MIP verify）→ Strengthened cuts（monotonicity-based feasibility and optimality cuts）。

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
- Lifted reformulation enables clean LP-first decomposition（未 lift 时 Phase 1 必须松弛 battery binaries 或面对 MILP 子问题）。
- Phase 1 求解 route-level relaxation，其可行域包含 Phase 2 MIP 可行域，因此 `Q^R_ω(w,z_ω) ≤ Q_ω(w,z_ω)`，提供 valid LB，但不证明 MIP feasibility。
- Strengthened feasibility cut validity：monotonicity 保证被 infeasible 方案支配的配置（不新增设施、不升级电池）仍不可行。
- Strengthened optimality cut validity：`L_ω` 是对 master 所有可行 `(w, z_ω)` 都有效的 scenario-wide global lower bound。对被 feasible 方案支配的配置，`Q_ω ≥ Q_ω(x*)`；indicator 仅在新增设施/升级电池方向取正，由 monotonicity 这些方向不增加运营成本，因此 `θ_ω ≥ Q_ω(x*) - δ_ω · indicator` 对一切 master 可行解成立。
- Finite convergence：假设 master 精确求解、Phase 2 精确验证且所有 cut 有效，master 含有限个 binary facility 和 battery 配置。Phase-2-infeasible 配置被 feasibility cut 排除；Phase-2-feasible 配置若 θ_ω 被低估则被 optimality cut 修正，避免同一配置以同一低估 θ_ω 重复出现。因此算法在有限步内精确收敛。计算实现中 master 精确求解（MIPGap = 0），outer loop 按 `solverConfig.mipGap` 容差终止，而不声称精确最优。

## 9. References

- Laporte, G., & Louveaux, F. V. (1993). The integer L-shaped method for stochastic integer programs with complete recourse. *Operations Research Letters*, 13(3), 133–142.
- [Add reference on partial Benders decomposition / partial decomposition]
