# 无线充电实验：代码、用例与结果

## 开始写作

**先打开 [已有实验结果](experiments/final_2026-11-05/writeup/results.md)。**

| 组别 | 已有记录 | 缺失 |
|---|---:|---|
| 主表 | 44 条已记录结局 + 1 条中断记录 | 6 条记录的 LB/UB 未记录；中断记录缺最终结果和实际总时间 |
| VI 消融 | 2 条 | 关闭 VI 的界值来自最后完整迭代，最终原生状态未记录 |
| R28 Full/L2P | 18 条 | 数值及实际墙钟时间已列出，含 N5-F50-C2 |

三组共有 65 个结果位置，已有 64 条实验结果记录及 1 条中断记录。限时、内存限制和未记录字段均保留。

- 本地用浏览器打开 `experiments/final_2026-11-05/writeup/results.html`，表头有用例、指标两层，主表按 R3/R12/R28 合并分组。
- [结构化数值](experiments/final_2026-11-05/writeup/results.json)包含完整精度、状态、缺失字段和逐条日志路径。
- 页面中的数值链接到随仓库保存的原始日志；输入快照也已随仓库保存。查看结果只需浏览器。
- [逐项日志索引](experiments/final_2026-11-05/analysis/LOG_INDEX.md)列出三组全部 65 个选用结果位置，对应 58 份去重原始日志，可查原始行号、完整迭代和版本对应。
- [代码消融核查](experiments/final_2026-11-05/analysis/ABLATION_AUDIT.md)列出 VI、Full/L2P 配对控制及实际代码差异。

## 代码与日志范围

当前冻结求解代码固定在提交 `7d932b99a8fe03e2b4df796e94071b5730dd5ae6`；执行文件和源码字节绑定见 [release.json](experiments/final_2026-11-05/release.json)。

已有结果来自历史归档版本，版本对应见 [history/records.json](experiments/final_2026-11-05/history/records.json)。这些历史数值不能标注为全部由新冻结 exe 产生。

仓库只发布上述三组的 65 个选用结果位置及对应日志。后续重跑的日志、结果、资源记录和进度页面仅保存在本地，待用户确认后再上传。重跑目前暂停。

## 用例

正式数据目录为 `data/R3_N5_data`、`data/R12_N5_data`、`data/R28_N5_data`、`data/R28_N10_data`、`data/R28_N20_data`、`data/R28_N40_data`。每个目录的 `scenarioCfg.txt` 明确场景集合与顺序。F/C 为运行参数，例如 `R28-N5-F50-C2`；正式名称不含搜索测试编号。

## 拉取与校验

新目录拉取：

```powershell
git clone --single-branch --branch codex/final-experiments-2026-11-05 git@github.com:foangfangkeeke/Two_phase_for_IJOC.git wireless-results
cd wireless-results
python experiments/final_2026-11-05/verify.py --history
```

该校验检查全部正式用例及历史日志、输入快照和归档代码/执行文件的哈希，不调用求解器。

代码构建与复现实验计划见 [实验版本说明](experiments/final_2026-11-05/README.md)。重跑保持暂停，等待用户继续指示。
