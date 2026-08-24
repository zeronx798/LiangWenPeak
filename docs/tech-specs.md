# LiangWenPeak Technical Specifications

## 1. 概述

LiangWenPeak 是一个非官方 Windows 桌面状态仪表，用北京时间展示 DeepSeek API 的峰谷价格状态、下一次真实价格变化、可选余额，以及基于本地固定采样历史估计的 API 消耗速率和余额触底 ETA。

项目把定价、时间、余额数据模型、历史存储和预测算法放在独立 Core 层；WinUI 3 应用负责窗口、设置、凭据、网络请求和调度；原生 Win32 Launcher 负责从 portable 目录启动当前版本。该边界使核心规则可以脱离 XAML 独立验证。

## 2. 技术栈

- C++20
- C++/WinRT
- WinUI 3 与 XAML
- Windows App SDK Stable
- Win32 API 与 Windows CNG
- Windows Credential Locker（PasswordVault）
- MSBuild、PowerShell 与 NuGet

项目不包含 .NET 业务层、Electron、WebView 或 HTML/CSS 前端。

## 3. 运行时架构

### LiangWenPeak.Core

`src/LiangWenPeak.Core/` 是不依赖具体 XAML 控件的核心层，包含：

- 北京时间转换、价格时间表、下一 transition 与时间格式化
- 自动刷新绝对时钟对齐
- 固定精度余额、设置约束和多币种模型
- CSV 历史读取、追加、归档与损坏恢复
- Series ID 计算、有效 interval 构建和预测算法
- 余额、消耗速率和 ETA 格式化

### LiangWenPeak.App

`src/LiangWenPeak.App/` 是 C++/WinRT WinUI 3 应用，主要职责包括：

- 创建 MainWindow 和 owned API settings window
- 通过 `MainViewModel` 协调定价、余额、历史和 UI state
- 异步调用 DeepSeek balance endpoint
- 使用 Credential Locker 保存 API Key 与历史身份密钥
- 使用当前用户注册表保存非敏感普通设置
- 按真实时钟安排下一次自动刷新，并驱动动态窗口布局

### LiangWenPeak.Launcher

`src/LiangWenPeak.Launcher/` 是不依赖 WinUI 或 Windows App SDK 的 Unicode Win32 executable。它使用 `GetModuleFileNameW` 获取自身目录，读取并校验 `current.txt`，构造版本目录和 `LiangWenPeak.App.exe` 的绝对路径，再以版本目录作为 working directory 调用 `CreateProcessW`。

Launcher 正确引用包含空格的 executable path，启动成功后立即关闭 process/thread handles 并退出，不等待 App 结束。它严格服从 `current.txt`，不选择其它版本，也不实现自动更新、fallback 或 rollback。

## 4. Portable 部署结构

```text
LiangWenPeak/
├─ LiangWenPeak.exe
├─ current.txt
├─ data/
│  ├─ balance-history.csv
│  └─ history/
└─ app-<version>/
   ├─ LiangWenPeak.App.exe
   └─ ...
```

- `LiangWenPeak.exe` 是稳定的用户入口和 Launcher。
- `current.txt` 只保存当前活动版本，Launcher 据此定位 payload。
- `app-<version>/` 保存可替换的版本化 WinUI 应用、Windows App SDK runtime 和资源。
- `data/` 与 Launcher 同级，是应用按需创建的 portable 本地用户数据目录。
- Windows Credential Locker 属于当前 Windows 用户配置，不在 portable filesystem 中。

Release ZIP 不创建或携带用户 `data/`。Packaging 只从允许的 build output 组装隔离 staging，并在发布到已有 portable 目录时保留其中的 `data/`，不得删除、移动、清空或覆盖真实用户历史。

在正式 portable layout 中，App 从自身 `app-<version>/` 目录向上解析 deployment root，再使用根目录下的 `data/`。开发构建则通过仓库中的 `Version.props` 定位 repository root；测试可以显式注入隔离 data root。

## 5. 峰谷价格模型

所有定价计算固定使用北京时间（UTC+8），不依赖 Windows 当前系统时区。

周一至周五：

- `09:00–12:00` 为 Peak / 原价。
- `14:00–18:00` 为 Peak / 原价。
- 其它时间为 Valley / 半价。

周六和周日全天为 Valley / 半价。周末的 `09:00`、`12:00`、`14:00`、`18:00` 不构成价格 transition。

`PricingScheduleService` 同时负责当前 period、下一次真实 transition 和剩余时间。它检查候选边界前后的状态，跳过不改变价格的边界，因此周五 `18:00` 进入 Valley 后可以直接找到周一 `09:00` 的下一次 Peak。下一时段 formatter 会在跨日或跨周末时补充必要的星期信息。

倒计时使用总小时数格式化，小时字段不会在 24 小时后回绕，所以跨周末 duration 可以显示超过 24 小时。

## 6. DeepSeek API 集成

余额请求使用异步 Windows HTTP API：

```text
GET https://api.deepseek.com/user/balance
Authorization: Bearer <API_KEY>
Accept: application/json
```

成功响应从 `balance_infos` 读取每一项的 `currency` 和 `total_balance`。一次响应必须至少包含一个合法币种，币种不能重复，余额必须是非负十进制数。余额内部使用固定 8 位小数的整数缩放表示，避免二进制浮点参与历史比较。

网络请求不阻塞 UI thread。失败时保留最近一次成功 observation 和余额，以低权重状态提示失败，不把余额改为零。已有余额请求进行时不会并发发出重复请求；保存新 Key 或重新启用 API 所需的即时 observation 可以在当前请求结束后继续执行。

## 7. API Key 与凭据存储

API Key 使用 Windows Credential Locker 保存，Credential resource 为 `LiangWenPeak.DeepSeekApi`，credential user 为 `api-key`。输入会先去除首尾空白；应用不把完整 Key 写入普通设置、CSV 或日志。

设置窗口不回显已保存 Key。已配置时 PasswordBox 保持空白并显示占位说明；空白保存代表保留原 Key。清除是 DraftState 中的 pending action，只有保存设置后才真正调用 Credential Locker 删除；取消或撤销不会影响已保存凭据。

非敏感设置保存在当前用户注册表 `HKCU\Software\LiangWenPeak`，包括 API 功能状态、预测状态、显示币种、刷新周期、速率窗口、preferred algorithm、各币种预警和已知币种列表。

## 8. Balance Observation 与 Scheduled Sample

Observation 表示一次成功 HTTP 响应得到的最新余额和实际返回时间，用于立即更新主窗口。Scheduled Sample 是允许写入预测历史的固定时钟采样。

| Refresh reason | 更新最新 Observation | 写入 Scheduled history |
| --- | --- | --- |
| 对齐边界的 scheduled automatic refresh | 是 | 是 |
| startup refresh | 是 | 否 |
| manual refresh | 是 | 否 |
| save-new-key refresh | 是 | 否 |
| API re-enable refresh | 是 | 否 |

Scheduled request 只有在成功取得并解析响应后才写历史。每一行 sample timestamp 使用计划采样边界，而 last observation time 使用 HTTP 成功返回的实际时刻。这样网络延迟不会改变采样轴，更新时间仍反映用户实际拿到新余额的时间。

自动刷新周期支持 `1 / 5 / 10 / 15 / 30 / 60` 分钟。下一目标每次都从当前北京时间重新计算为绝对整分钟边界，而不是在上次触发时间上反复累加 interval，因此手动刷新、Dispatcher 延迟和长期运行不会移动自动节奏。

设备 Sleep 或长时间暂停后不会补写多个错过的 sample。仍属于当前目标 interval 的边界最多执行一次；已经过期的目标被跳过，然后按恢复后的真实时间计算下一边界。手动刷新只产生 Observation，不重置自动 scheduler。

## 9. 本地余额历史

活动历史为 `data/balance-history.csv`，编码为 UTF-8，采用 append-only CSV：

```text
series_id,timestamp,currency,balance
```

普通记录包含 64 位十六进制 Series ID、UTC Unix 秒时间戳、币种代码和固定精度余额。一次成功 Scheduled Sample 会使用相同的计划时间戳，写入该 response 返回的全部币种。

API 状态 transition 使用 marker：

```text
@API_OFF,<timestamp>,,
@API_ON,<timestamp>,,
```

Marker 是硬 continuity break，不携带 Series ID、币种或余额。

正常历史不按预测窗口裁剪，也不自动过期；30 天只是可选预测窗口上限，不是 retention policy。“重新开始统计”把活动文件归档到 `data/history/balance-history-<timestamp>.csv`，再创建只有 header 的新活动文件，旧归档继续保留。

加载时会验证 header、列数、Series ID、币种、余额和时间顺序。中间出现损坏记录时，整个活动文件会归档到 `data/history/balance-history-invalid-<timestamp>.csv`，随后创建干净的活动文件。若最后一次 append 只留下明显不完整的尾行，可以忽略该尾行并修复文件结尾；完整但缺少换行的合法尾记录会保留并补齐换行。

## 10. History Identity Secret 与 Series ID

`HistoryIdentitySecret` 首次不存在或已失效时生成。它是由 Windows CNG `BCryptGenRandom` 和系统首选 RNG 产生的 256-bit cryptographically secure random secret，并以独立 credential user `history-identity-secret` 存入 Windows Credential Locker。

Series ID 定义为 $\mathrm{SeriesId} = \mathrm{HMAC\text{-}SHA256}(\mathrm{HistoryIdentitySecret}, \mathrm{APIKey})$ ，结果编码为 64 位十六进制字符串。计算期间的 secret 和 API Key 临时缓冲区在使用后清零。

不使用裸 API Key hash，是为了避免创建可跨机器稳定关联同一 Key 的 fingerprint。Portable history 可以复制，但 Credential Locker secret 不会随目录复制；新机器会生成新的 secret，同一个 API Key 因此得到新的 Series ID。

Series ID 只决定相邻两个同币种 sample 能否连续构成 interval。Series 改变只排除跨 Series 的那一个 interval，不会清空窗口内边界两侧各自有效的历史。

## 11. Continuity 与 Interval 规则

对目标币种按历史顺序检查相邻 sample。只有同时满足以下条件时才建立有效 interval：

- Series ID 相同。
- 币种相同。
- 两个 sample 之间没有 `@API_OFF` 或 `@API_ON` marker。
- 后一个时间戳晚于前一个时间戳。
- 后一个余额不高于前一个余额。

余额下降时，消费额为 $C_i = B_{i-1} - B_i$ ，形成正消费 interval。余额相等时， $C_i = 0$ ，仍是合法的 zero-consumption interval，其持续时间必须进入算法分母。

余额上涨被视为充值或其它 balance increase。跨越上涨的整个 interval 被排除，消费 numerator 和时间 denominator 都不参与；上涨后的 sample 仍可作为下一个 interval 的起点。

Series 改变和 API marker 同样只切断跨界 interval。程序退出、设备 Sleep、断网或长时间 gap 本身不是 continuity break。若只有一个长 interval 的两端余额，预测采用该 interval 内余额线性变化的假设；窗口左边界切中 interval 时，消费贡献按落入窗口的时间比例裁剪。

## 12. 多币种模型

一次 Scheduled Sample 把 `balance_infos` 中全部返回币种写入同一个计划时间点。CNY、USD 和未来其它合法币种不进行汇率换算，并分别维护：

- 最新余额
- 历史 sample 与有效 interval
- 余额预警
- API 消耗速率
- ETA

币种代码不是封闭 enum。未知但合法的币种仍可存储；formatter 对 CNY 使用 `¥`、对 USD 使用 `$`，其它币种 fallback 为 `currency code + amount`。

接口返回的可用币种会更新设置列表。当前选择仍可用时保持不变；不可用时自动切换到 response 中实际存在的币种。每个币种未显式配置的预警余额默认为零。

## 13. 消耗速率窗口

刷新周期决定采样频率和速率窗口下限，速率窗口决定预测观察范围，preferred algorithm 决定如何从该范围估计消耗。约束为 $T_{\mathrm{refresh}} \le T_{\mathrm{window}} \le 30\,\mathrm{days}$ 。

可选速率窗口为 `1 / 5 / 10 / 15 / 30` 分钟、`1 / 3 / 6 / 12` 小时，以及 `1 / 3 / 7 / 14 / 30` 天。设置窗口只列出不短于当前刷新周期的选项，ComboBox 始终允许用户在合法集合中选择。

刷新周期增大并使现有窗口失效时，DraftState 自动把窗口提升到新的刷新周期；刷新周期减小时不自动降低窗口。默认速率窗口为 30 天。

除“最近有效采样”外，各算法以所选币种最新 Scheduled Sample 的时间为窗口右端。早于窗口左边界的 interval 不参与；跨过左边界的 interval 按线性比例保留窗口内部分。

## 14. 预测算法

用户可选的 preferred algorithm 有滑动平均、指数平均和稳健趋势。默认且推荐滑动平均。“最近有效采样”是最短窗口下的 effective algorithm，不是第四个用户选项。

### 14.1 最近有效采样

当 $T_{\mathrm{window}} = T_{\mathrm{refresh}}$ 时，effective algorithm 自动切换为“最近有效采样”，算法下拉框禁用，但 preferred algorithm 不被覆盖。窗口再次增大后恢复用户原来的选择。

该算法使用目标币种最后一个合法 interval。若其消费额为 $C_{\mathrm{last}}$ 、实际持续时间为 $\Delta t_{\mathrm{last}}$ 小时，则速率为 $r_{\mathrm{last}} = \frac{C_{\mathrm{last}}}{\Delta t_{\mathrm{last}}}$ 。即使该 interval 跨越长时间 gap，也使用真实 duration，不把它压缩为配置的刷新周期。

### 14.2 滑动平均

滑动平均按窗口内所有合法 interval 的总消费和总有效时间计算。令 $C_i$ 为窗口内 interval 消费额， $\Delta t_i$ 为其有效小时数，则 $r_{\mathrm{avg}} = \frac{\sum_i C_i}{\sum_i \Delta t_i}$ 。

Zero-consumption interval 的 $C_i$ 为零，但 $\Delta t_i$ 仍进入分母。窗口边界切中长 interval 时， $C_i$ 与 $\Delta t_i$ 都按窗口内实际占比裁剪。

### 14.3 指数平均（EWMA）

EWMA 对较新的 interval 赋予更高权重。半衰期固定为 $T_{1/2} = \frac{T_{\mathrm{window}}}{2}$ ，不提供额外 UI 参数。

令 interval 中点距离窗口右端的 age 为 $a_i$ ，age decay 为 $d(a_i) = 2^{-a_i/T_{1/2}}$ ，interval 权重为 $w_i = \Delta t_i d(a_i)$ 。若 interval 自身速率为 $r_i = \frac{C_i}{\Delta t_i}$ ，则估计为 $r_{\mathrm{EWMA}} = \frac{\sum_i w_i r_i}{\sum_i w_i}$ 。

把 duration 纳入权重可保持不等长 interval 的时间贡献，同时由 age decay 提升近期数据影响。

### 14.4 稳健趋势

稳健趋势先按有效 interval 构建“累计有效时间、累计消费额”曲线，再对累计消费与有效时间执行 Huber robust regression。初始拟合使用普通加权直线，后续根据 residual 迭代调整权重。

Huber tuning constant 固定为 $1.345$ 。回归 slope 作为 forecast burn rate，并 clamp 到非负值；单个异常消费 burst 因此不会完全控制长期趋势。只有一个合法 interval 时，结果退化为该 interval 的实际平均速率。

所有算法输出供主窗口统一显示为“API 消耗”，不把该值描述为瞬时消耗。

## 15. ETA 计算与格式化

ETA 使用当前所选币种的最新 Observation、该币种预警余额和 forecast burn rate。令 $r_{\mathrm{forecast}}$ 为换算后的每秒消费速率，基础计算为 $\mathrm{ETA} = \frac{B_{\mathrm{current}} - B_{\mathrm{warning}}}{r_{\mathrm{forecast}}}$ ，内部 duration 单位为秒。

状态按以下优先级处理：

1. $B_{\mathrm{current}} \le B_{\mathrm{warning}}$ ：显示“已触底”。
2. 没有合法 interval：显示“获取中”。
3. 有合法数据但 $r_{\mathrm{forecast}} = 0$ ：显示“——”。
4. $\mathrm{ETA} < 60\,\mathrm{s}$ ：显示“小于1分钟”。
5. $\mathrm{ETA} \ge 365\,\mathrm{days}$ ：显示“大于1年”。

普通 ETA 只保留最高两个非零自然单位，1 月按 30 天定义。例如：

- `2mo 3d 4h 5min 6s` 格式化为“约 2 月 3 天”。
- `4h 0min 6s` 格式化为“约 4 时 6 秒”。
- `5min 6s` 格式化为“约 5 分 6 秒”。

## 16. API 功能状态模型

API Key 功能默认 ON，余额预测默认 OFF。

从 API ON 保存为 API OFF 时：

- 向活动历史追加 `@API_OFF` marker。
- 停止自动 scheduler 和余额请求。
- 不删除 API Key、普通设置或历史。
- MainWindow 折叠整个 API 区域并缩小。

从 API OFF 保存为 API ON 时：

- 向活动历史追加 `@API_ON` marker。
- 有有效 Key 时立即执行一次 API re-enable Observation refresh。
- 该即时 Observation 不写 history。
- 后续对齐边界的 Scheduled Sample 恢复历史采样。

Forecast OFF 只折叠“API 消耗”和“预计触底”。只要 API 功能仍为 ON，Scheduled Sample 与历史积累继续进行，因此重新打开预测后可以立即使用已有有效历史。

## 17. Settings DraftState

打开 API settings window 时，持久化普通设置复制为独立 DraftState。窗口内 API 总开关、显示币种、刷新周期、预测开关、各币种预警、速率窗口和 preferred algorithm 都只修改 draft。

Save 通过一个 commit path 提交完整普通设置和 API Key pending action；Cancel 关闭窗口并丢弃未保存 draft。API Key 有 `Keep`、`Clear`、`Replace` 三种 draft action：空白保持、待清除和输入新 Key 都不会在保存前修改 Credential Locker。

“重新开始统计”不是普通设置。用户确认后立即归档当前 active history 并创建新文件，这个 Action 不属于 Save / Cancel；随后取消设置窗口不会撤销已经完成的 rollover。

普通设置经规范化后保存在 `HKCU\Software\LiangWenPeak`。非法刷新周期、速率窗口、币种、负预警或不可持久化的 effective algorithm 会回退到合法状态；“最近有效采样”只在运行时由窗口关系决定，不覆盖 preferred algorithm。

## 18. MainWindow 动态布局

MainWindow 有三个主要状态：

- API OFF：仅显示价格状态、倒计时和下一时段。
- API ON + Forecast OFF：增加 API 余额；成功 Observation 存在时增加更新时间。
- API ON + Forecast ON：再增加 API 消耗和预计触底；成功 Observation 存在时增加更新时间。

逻辑宽度固定为 256 DIP。高度由标题区、状态区、实际可见信息行、可选更新时间行和 bottom padding 统一计算；`Collapsed` 行不占布局空间。窗口按当前 DPI 把 client DIP height 转换为物理像素，并补入实际 non-client height，DPI transition 后重新计算而不改变宽度。

更新时间只在当前进程已有成功 Observation 时可见，其 row 和 bottom padding 是 API-enabled 状态高度模型的一部分，不依赖 Forecast 是否开启。

API settings window 是 MainWindow 的 Win32 owned top-level window，不使用 `WS_CHILD`。它使用 tool-window style，不产生独立 taskbar entry；打开期间 owner 被禁用，settings window 与 always-on-top MainWindow 同处 topmost Z-order band，并保持在 owner 之上。重复打开会激活已有实例，关闭 MainWindow 会同步关闭 settings window。

## 19. 数据安全与隐私边界

- API Key 保存在 Windows Credential Locker，不进入普通配置、历史或日志。
- `HistoryIdentitySecret` 保存在 Windows Credential Locker，与 API Key 使用不同 credential user。
- 非敏感普通设置保存在当前用户注册表。
- 余额历史以明文 CSV 保存在 portable `data/`，包含 Series ID、时间、币种和余额，不包含 API Key。
- LiangWenPeak 没有自己的后台服务；API Key 只发往 DeepSeek balance endpoint。
- Release ZIP 不包含用户 `data/`；build/package 不删除或覆盖真实 `data/`。
- Portable 目录复制会带走其中的明文余额历史，但不会带走 Credential Locker secret 或 API Key。

由于身份 secret 不随 portable 目录迁移，复制到新机器的旧历史仍可读取，但新机器生成的 Series ID 默认不会与旧 Series 连续，避免跨机器继承 API identity association。项目不承诺对本地 CSV 进行静态加密；操作系统文件权限和用户对 portable 目录的管理仍然构成本地数据边界。

## 20. 构建与发布流程

项目版本的唯一来源是根目录 [`Version.props`](../Version.props)。Solution 为 [`LiangWenPeak.sln`](../LiangWenPeak.sln)，包含 Core、App、Launcher 和 native tests。

构建脚本职责如下：

- [`scripts/common.ps1`](../scripts/common.ps1)：发现 repository root、严格解析 source version、通过 `vswhere.exe` 定位 Visual Studio/MSBuild、检查 MSVC 与 Windows SDK、计算 build/dist 路径，并提供受约束的 clean、copy 和 package validation helper。
- [`scripts/build.ps1`](../scripts/build.ps1)：还原 NuGet/Windows App SDK packages，以指定 `Debug` 或 `Release`、`x64` 构建 solution，并验证 App、Launcher 和 tests output。
- [`scripts/package.ps1`](../scripts/package.ps1)：从已验证 build output 创建隔离 staging，生成 `current.txt` 和 `app-<version>/`，运行 staged Launcher tests，校验内容并创建 portable ZIP。
- [`scripts/release.ps1`](../scripts/release.ps1)：执行 clean Release build、native tests、build pipeline negative tests、完整 Launcher tests、staging、package validation 和 ZIP 输出。

正式入口为：

```powershell
.\scripts\release.ps1
```

脚本不依赖当前 working directory 或预先加载的 Developer PowerShell。所有 C++ project 保持 `/W4 /WX`。Release staging 排除 PDB、LIB、EXP、tests 和 UI validation artifacts；必须包含 Launcher、`current.txt`、版本化 App executable 与完整 self-contained runtime payload。

Package validation 在压缩前后都拒绝 `data/` 和禁止产物。发布到 `dist/` 时只同步生成的应用内容，并显式保留目标 portable root 已有的 `data/`。Release ZIP 的顶层直接包含 Launcher、`current.txt` 和 `app-<version>/`，不额外嵌套 package-name 目录。

## 21. 测试与验证

Native C++ tests 覆盖：

- 北京时间、工作日峰谷边界、完整周末、Friday-to-Monday transition 和超过 24 小时 countdown
- 下一时段 metadata、格式化和自动刷新整分钟 alignment
- 固定精度 DecimalAmount、CNG secret 和 HMAC Series ID
- Scheduled Sample 与 Observation 分离、多币种 batch 和计划时间戳
- Series boundary、充值、API OFF/ON marker、zero consumption 和 long gap interval
- 滑动平均、窗口边界线性裁剪、EWMA half-life、Huber robust trend 和非负 clamp
- 多币种独立速率/ETA、ETA 状态优先级与自然单位格式化
- DraftState、Key clear/undo/replace、window constraint 和 preferred/effective algorithm
- CSV append/load、truncated tail repair、中间损坏归档、rollover、rollback 和 deployment path
- MainWindow 三状态动态高度、更新时间 row 与 bottom padding 模型

PowerShell tests 覆盖：

- Launcher 正常启动、空格路径、任意 working directory、版本校验和错误场景
- source version 冲突、缺失构建环境、测试失败阻断、禁止 package 内容和 ZIP layout
- 本地 `data/` sentinel 在 clean/publish 后保持不变，且不泄漏到 ZIP
- staged Launcher smoke test 和 portable payload 验证
- MainWindow 动态高度、更新时间边界、settings owned/topmost/activation、控件 gutter 和 owner close
- About dialog 与主窗口尺寸稳定性
- 100%、125%、150%、200% DPI 下的 UI layout 验证

`release.ps1` 自动运行 native tests、build pipeline tests 和 Launcher tests。UI automation/DPI tests 作为独立验证脚本运行，因为它们需要可用桌面 session 和相应 DPI monitor 环境。

## 22. 非目标与明确边界

当前设计不包含：

- Token usage aggregation
- Web scraping 或 undocumented DeepSeek APIs
- Chart 或 history viewer
- History import UI
- `HistoryIdentitySecret` export/import UI
- Multiple API key management
- Auto updater
- MSIX 或 installer
- System tray
- 大型 theme 或 i18n overhaul

余额统计仅根据官方 balance endpoint 的固定采样变化估计消耗，不代理 API 请求，也不重建 Token 或账单明细。
