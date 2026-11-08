# 无线充电实验

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

当前求解源码为 c29cdc570e361a3291bd27c265faa45efb324f7b；对应 [wireless.exe](experiments/final_2026-11-07/versions/c29cdc5/wireless.exe)。每份日志的实际版本由索引和运行记录标明。

正式数据按 R/N 命名，scenarioCfg.txt 确定场景组合和顺序，F/C 为运行参数。运行需要 COPT 8.0.7 和有效许可证；构建使用 AGENTS.md 记录的直接 Ninja 命令。

## 拉取

```powershell
git clone --single-branch --branch codex/final-experiments-2026-11-05 git@github.com:foangfangkeeke/Two_phase_for_IJOC.git wireless-results
cd wireless-results
```

已有目录在同一分支执行 `git pull --ff-only`。
