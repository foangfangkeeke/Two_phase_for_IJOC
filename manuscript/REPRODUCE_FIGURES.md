# 第五章实验图复现

实验与出图代码位于 `main`；论文正文位于 `paper-writing-dwc`。
本入口读取仓库内的正式日志和数据，并记录数值核查结果。

## 运行

在仓库根目录，使用 Python 3.10 或更高版本：

```powershell
python -m pip install -r manuscript/requirements-figures.txt
python manuscript/reproduce_figures.py
```

本次验证使用 Python 3.13.5，依赖版本见 `requirements-figures.txt`。
该入口需要完整 Git 历史中的规划导出提交 `1f7cf15a`。
浅克隆应先补齐该提交历史。

默认论文图输出到 `manuscript/figures/`，核查记录输出到
`outputs/figure_reproduction/report.json`。每个步骤的完整输出保存在同目录的日志中。
只有所有步骤与产物校验通过后，才会生成本次 `report.json`。

也可指定单独的输出目录：

```powershell
python manuscript/reproduce_figures.py --output-dir outputs/paper_figures --report-dir outputs/figure_check
```

## 当前论文中的图

| 文件 | 来源或脚本 | 核对内容 |
| --- | --- | --- |
| `chapter5_network_map.png` | 作者保存的 BusRouter SG 静态截图 | 保留原图；提供地理背景 |
| `chapter5_station_visits.png` | `chapter5_analysis/plot_charger_visits_r28_n40.py` | R28-S40 的五个站点，40 个场景 |
| `chapter5_vi_progress.pdf` | `figures/chapter5_vi_progress.py` | R12、R28 的 VI 开关配对轨迹 |
| `chapter5_l2p_progress.pdf` | `figures/chapter5_progress.py` | F10、F20、F50 的 Phase 1 下界 |
| `chapter5_repair_modes.pdf` | `figures/chapter5_progress.py` | F10、C3 的边界、修复用时和决策变化 |
| `chapter5_planning_adjustments.pdf` | `figures/chapter5_planning.py` | 电池配置、充电机会和站点充电量变化 |
| `chapter5_cost_sensitivity.pdf` | `figures/chapter5_planning.py` | 电池成本与 DWC 建设成本敏感性 |

六张数据图由脚本生成。网络地图为 2026-11-09 保存的静态截图，来自
[BusRouter SG 的 28 条选定线路](https://busrouter.sg/#/services/945~282~96~984~872~452~183~79~334~990~941~453~41~285~284~871~189~98~335~98M~992~66~870~947~78~333~173~49)。
图中的底图与地理信息沿用该截图；493 个站点和 624 条有向 link 的统计来自处理后的实验输入。
静态图从论文分支原样保留在本分支，运行时可复制到指定输出目录。

此外生成 `chapter5_repair_phase2.pdf`，以及站点访问量和线路工作量的 PNG、HTML、CSV 诊断文件。
诊断文件位于核查输出目录。

## 数据与结论核查

- 算法实验：校验 137 个发布文件、365 个输入文件、65 个结果位置和 57 份独立日志，核对状态、上下界、gap、配对设置和 VI 检查点。
- 规划与成本敏感性：从 14 组正式记录重新导出 12 张 CSV，逐字节比较论文采用的 `1f7cf15a` 导出表，再以该固定提交的数据出图。
- 电价实验：额外校验 `tariff_spread_2026-11-10` 的 101 个发布文件、五组电价水平、原始优化边界、六条机制记录及已发表的节省区间判断。当前论文上述七张图尚未包含该实验。
- 所有曲线用时读取正式目标机器日志。入口调用分析与出图脚本，不启动优化求解器。

输入缺失、校验值不符、CSV 差异或任一脚本失败均会终止运行。
依赖、执行命令、产物路径、SHA-256 和源提交记录在核查报告中。
