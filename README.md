# 无线充电实验

## 论文实验图复现

当前实验与出图分支为 `main`，论文撰写分支为 `paper-writing-dwc`。
在仓库根目录运行 `python manuscript/reproduce_figures.py`，即可校验归档数据并生成第五章的数据图。
依赖、图与脚本的对应关系及输出位置见 [复现说明](manuscript/REPRODUCE_FIGURES.md)。

## 当前结果

[全量结果表](experiments/final_2026-11-07/results.md) | [两行表头 HTML](experiments/final_2026-11-07/results.html) | [逐项日志索引](experiments/final_2026-11-07/analysis/log_index.md)

| 组别 | 结果位置 |
|---|---:|
| 主表 | 45 |
| Lifted VI 开／关（R12、R28） | 4 |
| R28 Full / L2P | 16 |
| 合计 | 65 |

共 57 份独立原始日志、14 个用例，当前选定范围无待跑项。

- [结果数值](experiments/final_2026-11-07/results.json)、[输入对应](experiments/final_2026-11-07/cases.json)、[缺失信息](experiments/final_2026-11-07/MISSING.md)。
- [版本对应](experiments/final_2026-11-07/versions.json)：33 份日志对应 7d932b9 / 98f7719a，24 份对应 c29cdc5 / a14be609；源码快照和 exe 已保存。
- [原始日志复核](experiments/final_2026-11-07/analysis/raw_review.json)、[全部文件校验值](experiments/final_2026-11-07/manifest.json)。

## 代码和用例

当前工作区的 `src/` 与提交 `83d3156`、[Section 5.3 源码快照](experiments/section53_2026-11-09/versions/ef_audit/source_snapshot.zip) 一致。该版本的 [EF／固定方案审计 exe](experiments/section53_2026-11-09/versions/ef_audit/wireless.exe) 用于 Section 5.3 的 EF 和审计记录，以及电价实验的 EF 和审计记录。

主表、VI 和 R28 Full／L2P 的日志按 [版本索引](experiments/final_2026-11-07/versions.json) 分别对应 `7d932b9` 或 `c29cdc5` 的归档源码与 exe。Section 5.3 和电价实验的 Full 记录使用 [c29cdc5 exe](experiments/final_2026-11-07/versions/c29cdc5/wireless.exe)。复现具体结果时，以各包的清单、日志和归档版本为准。

逐项记录中的 `original_launch` 路径保留原运行机器的位置；复现时使用版本索引所指的归档 exe。旧冻结包已从当前发布树移出，其版本证据仍保存在上述归档和 Git 历史中。

正式数据按 R/N 命名，scenarioCfg.txt 确定场景组合和顺序，F/C 为运行参数。运行需要 COPT 8.0.7 和有效许可证；构建使用 AGENTS.md 记录的直接 Ninja 命令。

## 拉取

```powershell
git clone --single-branch --branch codex/final-experiments-2026-11-05 git@github.com:foangfangkeeke/Two_phase_for_IJOC.git wireless-results
cd wireless-results
```

已有目录在同一分支执行 `git pull --ff-only`。
