# 无线充电实验：代码、用例与结果

## 当前结果

打开 [当前结果表](experiments/final_2026-11-07/results.md)，或在本地打开 [HTML表](experiments/final_2026-11-07/results.html)。主表按R3/R12/R28分组，算法为行，用例／LB、UB、actualTime(s)为两层表头。

| 内容 | 当前记录 | 待补 |
|---|---:|---:|
| 主表 | 45 | 0 |
| Lifted VI开／关 | 2 | 0 |
| R28 Full／L2P | 10 | N10/N20/N40共6条待替换 |

共50份去重原始日志，对应57个结果位置。主表与消融复用相同运行；F30已移出范围。四条EF内存限制结果没有LB/UB，限时、内存和未记录信息均保留在表中。

- [逐项日志索引](experiments/final_2026-11-07/analysis/log_index.md)：每个结果可打开原始日志和运行记录。
- [完整数值](experiments/final_2026-11-07/results.json)、[用例与输入校验值](experiments/final_2026-11-07/cases.json)、[待补列表](experiments/final_2026-11-07/MISSING.md)。
- [版本对应](experiments/final_2026-11-07/versions.json)：33份日志来自7d932b9／98f7719a，17份来自c29cdc5／a14be609；对应源码快照和exe随包保存。
- [原始证据复核](experiments/final_2026-11-07/analysis/raw_review.json)、[全部文件校验值](experiments/final_2026-11-07/manifest.json)。

## 当前代码和消融

当前求解器源码固定在c29cdc570e361a3291bd27c265faa45efb324f7b，对应 [wireless.exe](experiments/final_2026-11-07/versions/c29cdc5/wireless.exe)。本次没有修改求解器源码、算法或参数。

Full/L2P每对运行使用相同exe、输入和配置，只改变算法选项。Lifted ±VI使用相同exe和输入，只改变VI选项。R28 Full/L2P日志包含逐轮master解及repair锚点；现有Lifted ±VI日志没有逐轮master解。

正式数据按R/N命名，scenarioCfg.txt给出具体场景集合和顺序，F/C为运行参数。查看结果无需求解器；运行需要COPT 8.0.7及有效许可证。构建使用AGENTS.md记录的直接Ninja命令。

## 拉取

```powershell
git clone --single-branch --branch codex/final-experiments-2026-11-05 git@github.com:foangfangkeeke/Two_phase_for_IJOC.git wireless-results
cd wireless-results
```

打开experiments/final_2026-11-07/results.html即可逐项分析。已有目录在同一分支执行git pull --ff-only。

N10/N20/N40的六条待替换旧结果不在当前结果包。后续补跑使用冻结代码，仅在本地保存，另行确认后发布。旧结果、失败尝试和探索记录保留在本地归档及Git历史中。
