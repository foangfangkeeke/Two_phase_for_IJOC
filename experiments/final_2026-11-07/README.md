# 当前实验结果包（2026-11-07）

先打开 [结果表](results.md)；本地 [HTML表](results.html)具有两层表头和合并用例组。数值均可点击打开对应原始日志。

| 内容 | 当前记录 |
|---|---:|
| 主表 | 45 |
| Lifted VI 开／关 | 2 |
| R28 Full／L2P | 10 |
| 去重原始日志 | 50 |

主表与消融复用相同运行，57个结果位置对应50份日志。按容量和场景分别展示时，有55个已记录显示位置，另有6个N10/N20/N40待替换位置。

- [全部数值与状态](results.json)、[逐项日志索引](analysis/log_index.md)、[输入及用例](cases.json)。
- 四条EF内存限制记录没有LB/UB；其他条目的最终值按原始日志记录。Natural的LB=0、UB=∞为明确输出值。
- N10/N20/N40的六条Full/L2P旧结果不在本包；F30不在正式范围。新候选仅在本地运行，尚未发布。
- [版本对应](versions.json)：33份来自7d932b9／98f7719a，17份来自c29cdc5／a14be609。逐条运行记录保留真实源码、exe及原始命令对应。最新版增加R28 Full/L2P搜索日志；当前结果不是全部由同一个exe产生。
- Full/L2P每对只改变算法选项；Lifted ±VI只改变VI选项。R28 Full/L2P含逐轮master解、repair锚点和incumbent；Lifted ±VI没有逐轮master解。
- [原始复核记录](analysis/raw_review.json)核对50份日志的界值、状态、实际时间、输入字节、参数及版本，未启动求解器。实际时间保留控制器的单调计时值；UTC起止时间差另列在运行记录中。
- [文件清单](manifest.json)保存本包文件和所用仓库输入文件的SHA256。

## 代码与运行

当前求解器源码为c29cdc570e361a3291bd27c265faa45efb324f7b；对应exe在versions/c29cdc5/wireless.exe。两个实际使用版本的源码快照和exe均保存在versions目录。运行需要可用的COPT 8.0.7安装及许可证。

全局gap为1%，内部MIP gap为0.01%，软时间预算7200秒。RSP workers=3，每worker threads=4，solver threads=12。实际超时结束时间保留原值。

现有旧版脚本和冻结输入索引保存在../final_2026-11-05，仅用于历史版本复现；本包为当前结果入口。
