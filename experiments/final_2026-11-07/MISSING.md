# 缺失信息

当前选定的 65 个结果位置均有运行记录，无待跑项。

## 未记录的原问题界

| 用例 | 算法 | LB | UB | actualTime(s) | 状态 |
|---|---|---|---|---:|---|
| [R12-N5-F50-C4](logs/R12-N5-F50-C4_ef.log) | EF | 未记录 | 未记录 | 87.203 | MEMORY_LIMIT_EXTERNAL |
| [R28-N5-F10-C4](logs/R28-N5-F10-C4_ef.log) | EF | 未记录 | 未记录 | 157.968 | MEMORY_LIMIT_EXTERNAL |
| [R28-N5-F20-C4](logs/R28-N5-F20-C4_ef.log) | EF | 未记录 | 未记录 | 154.031 | MEMORY_LIMIT_EXTERNAL |
| [R28-N5-F50-C4](logs/R28-N5-F50-C4_ef.log) | EF | 未记录 | 未记录 | 932.407 | MEMORY_LIMIT_EXTERNAL |

## 搜索记录

- Lifted 的 VI 开／关日志没有完整 master 决策向量或原生 MIP 分支节点历史。
- R28 no-VI 最后一次 master 未形成完整迭代；最终 LB 和最后完整轮 LB 分别记录。
- R28 VI 两组均未取得有限原问题 UB；no-VI 在最后完整轮与最终记录间没有新的全局 LB 时间点。
