# 余额统计与预测

LiangWenPeak 根据 DeepSeek balance endpoint 返回的余额变化估计 API 消耗速率和余额触底时间。统计按返回币种独立进行，不记录请求内容、Token 用量，也不进行汇率换算。

## 本地数据

Portable 部署中的运行时数据位于 Launcher 同级目录：

```text
data/
├─ balance-history.csv
└─ history/
```

`balance-history.csv` 是当前 append-only 采样历史；“重新开始统计”会把当前文件归档到 `data/history/`，再建立新的活动历史。Release ZIP 不包含用户的 `data/`，构建与打包不会删除、移动或覆盖它。

API Key 和 `HistoryIdentitySecret` 由 Windows Credential Locker 管理，不写入 CSV，也不随 portable 目录复制。

## Observation 与固定采样

只有对齐固定时钟边界的 scheduled automatic refresh 写入历史：

| 刷新原因 | 更新当前 observation | 写入 scheduled history |
| --- | --- | --- |
| 固定边界自动刷新 | 是 | 是 |
| 手动刷新 | 是 | 否 |
| 应用启动后的即时刷新 | 是 | 否 |
| 保存新 API Key 后的即时刷新 | 是 | 否 |
| 重新启用 API 后的即时刷新 | 是 | 否 |

因此，手动操作不会改变固定采样序列。一次 scheduled response 会以同一采样时间记录 API 返回的全部币种；每个币种分别维护余额、预警、有效 interval、消耗速率和 ETA。

## Series 与有效 interval

历史中的 Series ID 按以下身份计算：

```text
HMAC-SHA256(
    HistoryIdentitySecret,
    API Key
)
```

Series ID 不包含明文 API Key，只用于判断相邻两个采样是否属于可连续计算的同一序列。

以下边界只丢弃跨越边界的 interval，不会清空边界两侧各自仍然有效的历史：

- API Key 或 Series 改变
- 后一个余额高于前一个余额
- API OFF / API ON marker

程序退出、设备 Sleep、断网和长时间未运行不会自动产生断点。恢复后仍按真实采样时间计算 interval；不会补发错过的所有请求，长 interval 可以正常进入统计。

## 设置关系

```text
余额自动刷新
    ↓
决定消耗速率窗口的最小值

消耗速率窗口
    ↓
决定预测观察范围

预测算法
    ↓
决定如何从该窗口估计 API 消耗
```

消耗速率窗口不能短于余额自动刷新周期。当两者相等时，应用自动使用“最近有效采样”，并禁用预测算法下拉框；这是为最短窗口准备的 effective algorithm，不是第四个用户可选算法。

## 预测算法

- **滑动平均（推荐）**：按窗口内有效 interval 的实际时长汇总消耗，适合稳定、易解释的默认估计。
- **指数平均**：给予较新的 interval 更高权重，使估计更快响应近期变化。
- **稳健趋势**：对累计消耗拟合稳健趋势，降低异常 interval 对结果的影响。
- **最近有效采样**：仅在消耗速率窗口等于自动刷新周期时自动启用，使用最近一个有效 interval 的实际时长。

主窗口统一将估计值标为“API 消耗”，不会把它描述成瞬时消耗。

## ETA 显示

ETA 使用当前所选币种的最新 observation、该币种预警余额以及有效消耗速率计算：

| 条件 | 显示 |
| --- | --- |
| 当前余额小于或等于预警余额 | `已触底` |
| 没有有效 interval | `获取中` |
| 有效消耗速率为零 | `——` |
| ETA 小于 60 秒 | `小于1分钟` |
| ETA 大于或等于 365 天 | `大于1年` |

普通 ETA 只显示最高两个非零自然单位，月按 30 天计算。例如：

```text
2mo 3d 4h 5min 6s  →  约 2 月 3 天
4h 0min 6s          →  约 4 时 6 秒
5min 6s             →  约 5 分 6 秒
```
