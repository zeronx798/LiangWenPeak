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
- 通知设置约束、真实 transition 调度、补发窗口与持久化去重状态模型
- 自动刷新绝对时钟对齐
- 固定精度余额、设置约束和多币种模型
- CSV 历史读取、追加、归档与损坏恢复
- Series ID 计算、有效 interval 构建和预测算法
- 余额、消耗速率和 ETA 格式化

### LiangWenPeak.App

`src/LiangWenPeak.App/` 是 C++/WinRT WinUI 3 应用，主要职责包括：

- 创建 MainWindow 和 owned settings window
- 通过 `MainViewModel` 协调定价、余额、历史和 UI state
- 异步调用 DeepSeek balance endpoint
- 使用 Credential Locker 保存 API Key 与历史身份密钥
- 使用当前用户注册表保存非敏感普通设置
- 通过 classic WinRT `Windows.UI.Notifications` 与 desktop AUMID 发送 Toast
- 检测 Windows 版本并管理 Windows 11 Fluent Theme 状态
- 按真实时钟安排下一次自动刷新与通知，在系统恢复后重新协调调度，并驱动动态窗口布局

#### Fluent Theme 与 Windows 版本

Fluent Theme 是 `src/LiangWenPeak.App/AppTheme/` 中的纯 UI 能力，不进入 Core。`WindowsVersionDetector` 通过 `RtlGetVersion` 读取真实系统版本，Windows 11 的最低识别边界为 build `22000`，不依赖可能受 application manifest 影响的版本辅助宏。

Windows 11 显示“Fluent 主题”菜单项，首次运行默认开启；用户选择以 `FluentThemeEnabled` DWORD 保存在 `HKCU\Software\LiangWenPeak`。开启时，`ThemeManager` 为 MainWindow 和 API settings window 应用 Windows 11 圆角窗口、与深蓝背景协调的 DWM 边框、WinUI 原生控件圆角及系统焦点视觉。MenuFlyout、Button、PasswordBox、TextBox、ComboBox 和 ToggleSwitch 继续使用 WinUI 官方模板、ThemeResource、hover、pressed 与 focus visual state，不自绘控件。

Windows 10 不创建可见入口：菜单项保持 `Collapsed`，不是 disabled item，也不显示“不可用”说明。持久化值不能绕过系统版本检测。Windows 10 和关闭 Fluent Theme 的 Windows 11 仍使用深蓝纯色背景与原生 WinUI 控件，但回退为方角呈现。

主题切换不改变主窗口宽度、高度模型、内容结构或数据绑定，也不触碰 API 请求、Credential Locker、Settings DraftState、余额历史、Series ID、采样调度和预测算法。应用明确不使用 Mica、Acrylic、SystemBackdrop 或其它半透明材质。

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

Unpackaged WinUI 资源也必须随同版本化 payload 完整 staging。项目输出与可执行文件资源映射名一致的 `LiangWenPeak.App.pri`，并在 `App.xaml` 的应用级资源中合并官方 `XamlControlsResources`；两者共同保证 `NumberBox` 等原生 WinUI 控件在 build output、staged portable 目录及最终 ZIP 解压目录中都能解析默认样式和本地化字符串。Packaging 会把该 PRI 视为必需文件并拒绝旧的 `LiangWenPeak.pri` 名称，避免开发目录可用而 portable 启动时在控件套用模板阶段 fail-fast。

Toast 不改变这一发布结构。实际发送进程是 Launcher 启动的 unpackaged `LiangWenPeak.App.exe`，但 shell identity 的快捷方式 Target 固定为 portable 根目录的 `LiangWenPeak.exe`，而不是版本化 payload。Production AUMID 固定为 `zeronx798.LiangWenPeak`，与版本号和 portable 路径无关；进程在创建 UI 前调用 `SetCurrentProcessExplicitAppUserModelID` 设置同一身份。

`NotificationIdentityService` 使用官方 Shell COM API `IShellLinkW`、`IPersistFile`、`IPropertyStore` 与 `PKEY_AppUserModel_ID`，在 `%APPDATA%\Microsoft\Windows\Start Menu\Programs\LiangWenPeak.lnk` 创建 per-user identity。通知默认关闭，因此首次普通运行只设置进程 AUMID，不创建 shortcut；第一次启用正式通知或点击测试通知时才创建。已启用通知的应用启动时会检查 AUMID 和 Target；portable 文件夹整体移动后，只在现有 shortcut 的 AUMID 与 Launcher 文件名均属于本应用时修复 Target。同名但身份不匹配的 shortcut 不会被覆盖或删除。

`NotificationService` 使用 `ToastNotificationManager::CreateToastNotifier(AUMID)`、`ToastText02` XML template 和 `ToastNotification::Show` 投递 title/body。此前基于 `Microsoft.Windows.AppNotifications.AppNotificationManager::Register()` 的原型在当前 self-contained unpackaged ZIP 中返回 `0x8007007E`；Windows App SDK 官方文档明确说明 self-contained 部署不支持依赖 Singleton package 的 API，因为 Singleton 无法被 self-contained deployment 自带或由 package graph 提供。因此这里不再围绕 `Register()` 修补，而改用 Windows 10/11 系统提供的 classic desktop WinRT API，消除 Singleton proxy 依赖，也不需要额外 MSIX、installer、COM activator、protocol activation、后台服务或第三方框架。Windows 拒绝通知时服务返回可读失败状态，但其它功能保持正常。实现依据见 [Windows App SDK self-contained deployment limitations](https://learn.microsoft.com/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps)、[desktop Toast quickstart](https://learn.microsoft.com/windows/win32/shell/quickstart-sending-desktop-toast)、[desktop AppUserModelID shortcut](https://learn.microsoft.com/windows/win32/shell/enable-desktop-toast-with-appusermodelid) 和 [SetCurrentProcessExplicitAppUserModelID](https://learn.microsoft.com/windows/win32/api/shobjidl_core/nf-shobjidl_core-setcurrentprocessexplicitappusermodelid)。

## 5. 峰谷价格模型

所有定价计算固定使用北京时间（UTC+8），不依赖 Windows 当前系统时区。

周一至周五：

- `09:00–12:00` 为 Peak / 原价。
- `14:00–18:00` 为 Peak / 原价。
- 其它时间为 Valley / 半价。

周六和周日全天为 Valley / 半价。周末的 `09:00`、`12:00`、`14:00`、`18:00` 不构成价格 transition。

`PricingScheduleService` 同时负责当前 period、下一次真实 transition 和剩余时间。它检查候选边界前后的状态，跳过不改变价格的边界，因此周五 `18:00` 进入 Valley 后可以直接找到周一 `09:00` 的下一次 Peak。下一时段 formatter 会在跨日或跨周末时补充必要的星期信息。

倒计时使用总小时数格式化，小时字段不会在 24 小时后回绕，所以跨周末 duration 可以显示超过 24 小时。

### 5.1 通知调度

`NotificationScheduler` 位于 Core，`NotificationService` 位于 App。前者只处理时间、事件与文案，后者只处理 Windows Toast 注册和发送；`PricingScheduleService` 继续只负责峰谷规则，不包含任何通知 API。

通知设置默认值为：`Enabled=false`、`AdvanceEnabled=true`、`AdvanceMinutes=10`。提前分钟数必须是 `1–30` 的有限整数；读取到非法持久化值时恢复为 10，设置保存时则拒绝 `0`、负数、超过 30、NaN 和非整数。

调度唯一来源是 `PricingScheduleService::GetNextTransition` 返回的下一次真实 transition：

- `transition - AdvanceMinutes` 产生一次 Advance event。
- `transition` 产生一次 Arrived event。
- Advance 只在目标时间实时发送，错过后永不补发。
- Arrived 可从当前时间向前检查最近一次真实 transition，并只在闭区间 `[transition, transition + 15min]` 内补发。
- 普通启动不按“当前处于某状态”构造事件；只有确实存在、未去重且仍在 15 分钟窗口内的最近 transition 才能补发 Arrived。

MainWindow 使用 one-shot `DispatcherQueueTimer` 唤醒，并以每秒 UI 时钟作为时钟跳变和恢复兜底。`PowerManager::SystemSuspendStatusChanged` 收到 `AutoResume` 或 `ManualResume` 后回到 UI queue，重新读取当前系统时间、检查 Arrived 补发并重新计算下一次唤醒；逻辑不假设 Timer 在睡眠期间精确触发。

事件身份是 `transition UTC timestamp + NotificationType`。注册表分别保存最后一次 Advance 和 Arrived transition 的 Unix 秒；同类型不晚于已保存时间戳的事件都视为已处理。发送前先持久化去重状态，从而在 Timer 重入、UI 刷新、设置保存、恢复、重新激活或进程重启之间保持 at-most-once。两种类型独立记录，因此同一个 transition 的提前通知不会压掉到达通知。该状态不进入 `balance-history.csv`。

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

非敏感设置保存在当前用户注册表 `HKCU\Software\LiangWenPeak`，包括通知总开关、提前提醒开关与分钟数，以及 API 功能状态、预测状态、显示币种、刷新周期、速率窗口、preferred algorithm、各币种预警和已知币种列表。通知去重时间戳也保存在该注册表路径，但不属于用户可编辑 Draft。

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

原“API Key 功能”窗口复用并更名为“设置”。打开窗口时，持久化普通设置完整复制为独立 DraftState。窗口内通知总开关、提前提醒开关与分钟数、API 总开关、显示币种、刷新周期、预测开关、各币种预警、速率窗口和 preferred algorithm 都只修改 draft；通知 scheduler 始终只读取 persisted settings。

Save 通过一个 commit path 提交完整普通设置和 API Key pending action；Cancel 关闭窗口并丢弃未保存 draft。API Key 有 `Keep`、`Clear`、`Replace` 三种 draft action：空白保持、待清除和输入新 Key 都不会在保存前修改 Credential Locker。

主菜单“启用通知”是同一 persisted setting 的快捷入口：点击后立即持久化并立即启动或停止调度。设置窗口打开期间发生的外部快捷开关提交不会改写已有 Draft，也不会偷偷刷新窗口控件；若用户随后保存，则完整 Draft 覆盖当前 persisted settings，即 Snapshot + Last Commit Wins，不做字段级 merge。

通知总开关为 OFF 时，提前提醒 ToggleSwitch 和 NumberBox disabled，但保留 Draft 原值；总开关为 ON 且提前提醒为 OFF 时只禁用 NumberBox。测试通知 Button 始终 enabled。NumberBox 的 `Min=1`、`Max=30`、`SmallChange=1`，保存路径仍会做最终有限整数验证。

“重新开始统计”不是普通设置。用户确认后立即归档当前 active history 并创建新文件，这个 Action 不属于 Save / Cancel；随后取消设置窗口不会撤销已经完成的 rollover。“测试通知”同样是 Immediate Action，不读取或提交 Draft，也不要求 persisted 或 draft 通知总开关开启。

“危险区 / 彻底清理”也是 Immediate Action，必须先通过 WinUI `ContentDialog` 明确确认。确认后 MainWindow 先停止 notification timer，`CleanupService` 再对当前 `StateProfile` 依次执行通知 runtime 停止、按当前 AUMID 清除受支持的 notification history（best effort）、验证并删除当前应用 shortcut、精确删除 API Key 与 History Identity credential、删除当前应用 Registry subtree。各步骤独立继续执行并收集必要项失败，不做无法保证并发安全的 rollback，也不会把已删除 secret 写回。

清理路径没有 `BalanceHistoryStore` 或 `data/` 删除能力。`data/balance-history.csv`、`data/history/` 与未来所有 portable 本地历史都保持原位。清理成功或部分失败后，设置窗口先销毁清理前的 `SettingsDraft` 与 PasswordBox replacement text，再由 `SettingsService::LoadBalanceSettings()` 和现有 `BalanceSettings{}` 中央 defaults 建立新 Draft。MainViewModel 增加 generation 以废弃进行中的余额请求结果，重新加载实际 persisted state，ThemeManager 重新按 OS/default 计算，主菜单同步刷新。随后 Save 只能提交新 Draft，因此不能复活清理前的通知值、API Key action 或 identity secret。

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
- 通知 Advance/Arrived 去重时间戳保存在当前用户注册表，不写入余额历史。
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
- 通知默认值、`1–30` 整数约束、Snapshot/Cancel/Save/Last Commit Wins 事务
- Advance/Arrived、Timer 重入去重、周末、Friday-to-Monday、missed Advance 与 15 分钟 Arrived catch-up
- CSV append/load、truncated tail repair、中间损坏归档、rollover、rollback 和 deployment path
- MainWindow 三状态动态高度、更新时间 row 与 bottom padding 模型
- Windows 10/11 build 边界与 Fluent Theme 可用性分类

PowerShell tests 覆盖：

- Launcher 正常启动、空格路径、任意 working directory、版本校验和错误场景
- source version 冲突、缺失构建环境、测试失败阻断、禁止 package 内容和 ZIP layout
- 本地 `data/` sentinel 在 clean/publish 后保持不变，且不泄漏到 ZIP
- staged Launcher smoke test 和 portable payload 验证
- 主菜单通知快捷开关的即时持久化
- danger cleanup Cancel、精确 Registry/Credential/shortcut 清理、unrelated shortcut 保留、notification history（系统支持时）、完整 Draft defaults 重建及 Save non-resurrection
- isolated `data/balance-history.csv`、`data/history/test.csv` 与 sentinel 的逐文件 SHA-256 保留验证
- Release-equivalent staged Launcher 的 classic Toast title/body、portable 目录移动后 shortcut Target repair 与 test-only teardown
- MainWindow 动态高度、更新时间边界、settings owned/topmost/activation、控件 gutter 和 owner close
- Windows 11 Fluent Theme 菜单默认状态、开关切换与持久化；Windows 10 菜单隐藏路径
- About dialog 与主窗口尺寸稳定性
- 100%、125%、150%、200% DPI 下的 UI layout 验证

`release.ps1` 自动运行 native tests、build pipeline tests 和 Launcher tests。UI automation/DPI tests 作为独立验证脚本运行，因为它们需要可用桌面 session 和相应 DPI monitor 环境。

1.1.2 最终 ZIP 的 About 已在 100%、150%、200% DPI 下完成人工视觉验收，“版本 1.1.2”在三档缩放下均完整、清晰可见。此前 About UI Automation 报告的裁切属于自动化边界检测误报，不是实际 UI regression；自动化继续严格要求 UIA 树中存在精确的版本文本，但不再根据该 TextBlock 偶发返回的空 `BoundingRectangle` 推断视觉裁切。窗口打开/关闭稳定性只容许 2 个物理像素的 DPI 舍入误差，其它 About 元素的边界与 Close 按钮重叠检查保持不变。自动化结果需要结合截图与人工视觉结论解释，不应据此修改已经通过验收的 About 视觉样式。

Toast 验证分为两层。第一层始终严格验证隔离测试 AUMID 的通知中心历史中存在精确 title/body，同时验证 shortcut Target repair 与 teardown；这层不因专注/勿扰模式降级。第二层才检查 Windows Shell 横幅标题/正文是否暴露给桌面 UI Automation：脚本先通过公开、只读的 [`SHQueryUserNotificationState`](https://learn.microsoft.com/windows/win32/api/shellapi/nf-shellapi-shqueryusernotificationstate) 判断当前会话是否适合展示通知 UI；状态不是 `QUNS_ACCEPTS_NOTIFICATIONS` 时直接跳过横幅元素查询。若该 API 报告可展示、但随机测试 AUMID 的横幅仍未暴露给 UI Automation，同样只跳过第二层并提示“当前桌面会话没有向 UI Automation 暴露 Toast 横幅标题/正文，进行 UI Automation 请先关闭专注/勿扰模式。”，不把它误判为 Toast backend 或通知历史失败。测试不会读取 Windows 私有通知 Registry 来猜测勿扰状态。2026-08-28 已人工确认 1.1.2 的 classic Toast 测试通知可以正常显示系统横幅；UI Automation 是否暴露 Shell 横幅元素不作为人工可见性结论的唯一依据。

### 21.1 Test Profile 隔离

任何会启动 App 或 Launcher 的 integration/UI/portable test 都必须先生成 canonical GUID `TestRunId`，并通过 `LIANGWENPEAK_TEST_RUN_ID` 与 `LIANGWENPEAK_TEST_PORTABLE_ROOT` 只注入该子进程。两者缺一、GUID 不规范、root 非绝对路径或路径不包含 GUID 时，`StateProfile::FromEnvironment()` fail closed，绝不回落到 production namespace。

Production/Test 资源映射如下：

| Resource | Production | Test run |
| --- | --- | --- |
| Registry | `HKCU\Software\LiangWenPeak` | `HKCU\Software\LiangWenPeak.Tests\<GUID>` |
| API credential | `LiangWenPeak.DeepSeekApi / api-key` | `LiangWenPeak.Test.<GUID>.ApiKey / api-key.<GUID>` |
| History credential | `LiangWenPeak.DeepSeekApi / history-identity-secret` | `LiangWenPeak.Test.<GUID>.HistoryIdentity / history-identity-secret.<GUID>` |
| AUMID | `zeronx798.LiangWenPeak` | `zeronx798.LiangWenPeak.Test.<GUID>` |
| Shortcut | `LiangWenPeak.lnk` | `LiangWenPeak Test <GUID>.lnk` |
| Local data | portable root `data/` | GUID staged root `data/` |

测试凭据只使用 `TEST_ONLY_DO_NOT_USE_<GUID>` 等明显无效值，API 功能在系统集成测试中关闭，不发送 DeepSeek 请求。测试只保存自己启动的明确 PID；关闭时验证 PID ownership，不按 image name kill、Stop-Process 或向已有 LiangWenPeak 实例发送关闭消息。shortcut teardown 在删除前同时验证文件名、test AUMID 与当前 staged Launcher 的绝对 Target；Credential Locker 只按本次 Resource/UserName 精确 Retrieve/Remove，不 enumerate。测试禁止 production backup → mutate → restore。

`Test-TestIsolation.ps1` 在 release tests 前静态拒绝 production Registry、credential、AUMID、shortcut、registry backup/restore 与 image-name termination pattern。每个运行在 `finally` 中清除本次 GUID 的 Registry、两个 credential、notification history、已验证 shortcut 与 staged root；旁边的 unrelated shortcut 和其它 test/production namespace 不受影响。

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
- 自建通知服务器、后台常驻服务或第三方通知框架
- System tray
- Mica、Acrylic、透明桌面组件或大型 theme/i18n overhaul

余额统计仅根据官方 balance endpoint 的固定采样变化估计消耗，不代理 API 请求，也不重建 Token 或账单明细。
