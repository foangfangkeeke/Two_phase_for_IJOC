# 当前实验结果

[结果表](results.md) | [两行表头 HTML](results.html) | [逐项日志索引](analysis/log_index.md)

| 组别 | 结果位置 |
|---|---:|
| 主表 | 45 |
| Lifted VI 开／关（R12、R28） | 4 |
| R28 Full / L2P | 16 |
| 合计 | 65 |

57 份独立日志、14 个用例；同一运行可在不同表中复用。当前选定范围无待跑项。N10/N20/N40 使用已完成的正式记录。

- [全部数值和状态](results.json)、[日志索引 JSON](log_index.json)、[用例和输入校验值](cases.json)。
- [缺失信息](MISSING.md)：四条 EF 外部内存终止记录没有 LB/UB；保留实际时间和终止状态。
- [R28 VI 对照数据](analysis/r28_vi_comparison.md)。
- [版本对应](versions.json)：33 份日志对应 7d932b9 / 98f7719a，24 份对应 c29cdc5 / a14be609；对应源码快照和 exe 保存于 versions/。
- [原始日志复核](analysis/raw_review.json)、[全部文件校验值](manifest.json)。

## 运行参数

全局 gap 1%，内部 MIP gap 0.01%，软时间预算 7200s，solver threads=12，RSP workers=3、threads=4。实际用时保留实测值。

Full/L2P 每对使用相同输入和 exe，只改变算法选项；Lifted VI 每对只改变 VI 选项。运行需要 COPT 8.0.7 和有效许可证。
