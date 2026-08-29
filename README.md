# LiangWenPeak

一个使用原生 C++20、C++/WinRT 和 WinUI 3 编写的 Windows 桌面峰谷价格状态小窗。

LiangWenPeak 根据北京时间显示 DeepSeek API 当前处于原价还是半价时段，并显示距离下一次真实价格变化的倒计时、下一时段，以及可选的峰谷通知、API 余额、消耗速率和余额触底 ETA。它是非官方社区工具，与 DeepSeek 没有授权、维护或商业关系。

`LiangWenPeak = LiangWen + Peak`，是表达峰值时段的趣味命名。中文界面相应显示“梁 文 峰 · 原 价”或“梁 文 谷 · 半 价”。

<table>
  <thead>
    <tr>
      <th>紧凑模式</th>
      <th>余额</th>
      <th>余额预测</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><img src="docs/images/liangwenpeak-api-off.png" alt="API Key 功能关闭时的 LiangWenPeak 主窗口"></td>
      <td><img src="docs/images/liangwenpeak-balance.png" alt="显示 API 余额的 LiangWenPeak 主窗口"></td>
      <td><img src="docs/images/liangwenpeak-forecast.png" alt="显示余额消耗速率与 ETA 的 LiangWenPeak 主窗口"></td>
    </tr>
  </tbody>
</table>

<p align="center">
  <img src="docs/images/liangwenpeak-api-settings.png" alt="LiangWenPeak 设置窗口" width="420">
</p>

## 功能

- 默认始终置顶的紧凑 Windows 桌面窗口
- 固定按北京时间（UTC+8）判断峰谷状态
- 工作日峰谷计价与周末全天半价
- 指向下一次真实价格变化的倒计时，支持跨周末超过 24 小时
- 下一价格时段显示
- 可选的峰谷到达通知与 `1–30` 分钟提前提醒
- 可选的 DeepSeek API 余额查询与返回币种选择
- 可配置且对齐整分钟的余额自动刷新
- 本地余额历史、API 消耗速率与余额触底 ETA
- 滑动平均、指数平均和稳健趋势三种预测算法
- Windows 11 Fluent Theme 视觉适配，可关闭并记住选择
- Windows Credential Locker 安全保存 API Key
- 原生 Win32 Launcher 与 self-contained portable distribution
- 一条命令完成构建、测试、校验和打包

## 峰谷规则

所有规则均按北京时间（UTC+8）计算，与 Windows 当前系统时区无关。

### 周一至周五

| 状态 | 价格 | 时段 |
| --- | --- | --- |
| 梁文峰 | 原价 | `09:00–12:00`、`14:00–18:00` |
| 梁文谷 | 半价 | `00:00–09:00`、`12:00–14:00`、`18:00–24:00` |

### 周六、周日

周末全天为梁文谷半价。`09:00`、`12:00`、`14:00` 和 `18:00` 不会产生价格状态切换。

应用始终寻找下一次真实变化。例如周五 `18:00` 进入低谷后，下一次峰值是周一 `09:00`。跨周末倒计时的小时数可以超过 24。

## 下载与运行

1. 从 [GitHub Releases](https://github.com/zeronx798/LiangWenPeak/releases/latest) 下载最新的 Windows x64 portable ZIP。
2. 完整解压 ZIP。
3. 运行解压目录根部的 `LiangWenPeak.exe`。

发布文件使用 `LiangWenPeak-<version>-windows-x64.zip` 命名。不要单独移动 Launcher、`current.txt` 或版本目录，也不需要直接进入 `app-<version>/` 启动内部应用。

## Fluent Theme

Windows 11 默认启用 Fluent Theme。可在主窗口的 `···` 菜单中取消勾选“Fluent 主题”，应用会立即切换窗口圆角、菜单、按钮、输入框、下拉框、开关及焦点视觉，并记住当前用户的选择。

Windows 10 不显示“Fluent 主题”菜单项；应用不会显示禁用项或“不可用”提示。无论主题是否启用，主窗口都保持深蓝纯色背景、固定尺寸与原有信息布局，不使用 Mica、Acrylic 或半透明材质。

Fluent Theme 只属于 WinUI UI 层，不改变 DeepSeek API 请求、API Key 保存/取消语义、余额历史、消耗预测或 Core Library。

## 通知

通知首次运行默认关闭。可从主窗口 `···` 菜单直接勾选“启用通知”，该快捷开关会立即保存并启动或停止真实通知调度。启用后，每次真实价格状态变化都会发送“到了”通知；提前提醒默认开启并提前 10 分钟，可在设置窗口中配置为 `1–30` 的整数分钟。

通知调度与主窗口倒计时共用已经验证的下一次真实价格变化。因此周末全天半价时不会在 `09:00 / 12:00 / 14:00 / 18:00` 产生伪通知，周五 `18:00` 之后的下一次提醒会指向周一 `09:00`。

睡眠或程序未运行期间错过的提前提醒不会补发。“到了”通知可在真实切换后的 15 分钟内补发一次；超过窗口后不补发，普通启动也不会仅因为当前处于峰价或谷价就发送通知。

“测试通知”是始终可用的立即操作，即使正式通知或当前设置草稿中的通知开关处于关闭状态，也会尝试立即发送“LiangWenPeak 通知测试”。应用必须以普通用户权限运行；Windows 通知能力不可用时，设置窗口会显示发送失败。

通知使用 Windows 官方 classic WinRT desktop Toast（`Windows.UI.Notifications`），不依赖 Windows App SDK App Notifications 的 Singleton proxy，也不需要 MSIX、installer、后台服务或管理员权限。首次真正启用或测试通知时，应用会在当前用户的开始菜单创建最小通知身份快捷方式，使用固定 AUMID `zeronx798.LiangWenPeak`，Target 始终指向 portable 根目录的稳定 Launcher `LiangWenPeak.exe`。移动整个 portable 目录后，应用会在下次启用通知时修复 Target；不会指向版本化的 `app-<version>/LiangWenPeak.App.exe`。

1.1.2 的 classic Toast 横幅已经过 Windows 实机人工验证；通过设置窗口点击“测试通知”可以正常显示系统通知横幅。

## 设置与 API Key

API Key 是可选项。未配置时，峰谷状态、北京时间、倒计时和下一时段仍然完整可用。通过主窗口的 `···` 菜单打开“设置...”即可集中管理通知和以下 API 相关设置。

### 启用 API Key 功能

这是全部 API 相关功能的总开关，默认开启。

关闭后，应用停止余额查询和历史采样；主窗口隐藏 API 余额、API 消耗、预计触底和更新时间，并自动缩小。已保存的 API Key、余额历史和其它设置都不会被删除，重新启用后可以继续使用。

### API Key

在此输入 DeepSeek API Key。已经配置过 Key 时，输入框会显示“已配置，留空则保持不变”；保持为空并保存不会替换原 Key。

### 清除 / 撤销

点击“清除”只会创建一个等待保存的清除操作，不会立即删除 Credential Locker 中的 Key。此时输入框显示“保存后将清除 API Key”，按钮变为“撤销”。

只有点击“保存”才会真正删除 Key。点击“撤销”可取消待清除状态；如果最后关闭窗口或点击“取消”，原 API Key 完全不变。

### 显示币种

列表来自 DeepSeek 官方余额接口实际返回的币种。不同币种的余额、历史、预警、消耗速率和 ETA 分别计算，不进行汇率换算。

如果当前选择的币种不再由接口返回，应用会自动切换到一个实际可用的币种。CNY、USD 之外的新币种也可以保存和显示。

### 余额自动刷新

支持 `1 / 5 / 10 / 15 / 30 / 60` 分钟。自动刷新按实际时钟的整分钟边界执行，例如 5 分钟周期会对齐到 `:00 / :05 / :10` 等边界。

主窗口菜单中的“刷新余额”会立即查询一次，但不会改变下一次自动刷新时间。手动刷新只更新当前显示，不会作为固定采样写入预测历史。

### 显示余额预测

开启后，主窗口增加“API 消耗”和“预计触底”两行。默认关闭。

关闭预测只会隐藏这两行；自动余额刷新和历史采样仍会继续，已有历史也会保留。

### 余额预警

每个币种独立保存预警余额，默认值为 `0`。“预计触底”表示按当前预测达到该预警余额的时间，不一定表示余额归零。

切换“显示币种”时，输入框会显示对应币种自己的预警值。

### 消耗速率窗口

此设置决定预测参考多长时间内的历史。最小值由“余额自动刷新”周期决定，最大值为 30 天。

如果增大刷新周期后现有窗口过小，窗口会自动提升到新的最小合法值；减小刷新周期不会自动缩短已经选择的窗口。

### 预测算法

可选算法为：

- 滑动平均（推荐）
- 指数平均
- 稳健趋势

当“消耗速率窗口”等于“余额自动刷新”周期时，应用自动使用“最近有效采样”，预测算法下拉框暂时不可修改。调大消耗速率窗口后，下拉框会恢复，并继续使用此前选择的算法。

### 重新开始统计

应用会先显示确认提示。确认后，当前活动余额历史立即归档，新的活动历史随即建立，消耗统计从后续自动采样重新积累；旧历史不会删除。

这是立即生效的独立操作，不属于设置草稿。执行后再点击设置窗口的“取消”也不会撤销归档。“测试通知”同样属于立即操作。

### 危险区：彻底清理

“彻底清理”是需要再次确认的立即操作。确认后会停止通知调度，并尽力清除 LiangWenPeak 明确定义的 API Key、历史身份密钥、`HKCU\Software\LiangWenPeak` 应用设置、通知去重状态、应用自己的 Toast 快捷方式，以及 Windows 官方 API 支持清除的通知历史。它不会枚举或删除其它应用的凭据、设置或快捷方式。

清理绝不会删除、清空、移动或改写 portable 根目录中的 `data/`，包括 `data/balance-history.csv` 与 `data/history/`。清理完成后，设置窗口会立即销毁清理前的整个 Draft，再从中央默认设置建立新 Draft；因此随后点击“保存”也不能复活旧通知设置或旧 API Key。若部分系统清理步骤失败，应用继续执行其它安全步骤、显示失败项、从实际剩余状态重载 UI，并且不会回写已经删除的凭据。

### 保存

“保存”一次性提交窗口中的普通设置，包括通知总开关、提前提醒及分钟数、API 功能总开关、API Key 操作、显示币种、自动刷新周期、显示余额预测、各币种余额预警、消耗速率窗口和预测算法。

设置窗口打开时会复制一份完整的已保存设置作为 Draft。若窗口打开期间又从主菜单修改通知快捷开关，已经打开的控件不会被外部变化覆盖；最后点击“保存”时以完整 Draft 为准，即 Snapshot + Last Commit Wins。

### 取消

“取消”丢弃当前窗口尚未保存的普通修改，也不会修改已经保存的 API Key。它不会回滚此前已经确认执行的“重新开始统计”。

## API 余额与隐私

余额通过 DeepSeek 官方 balance endpoint 获取：

```text
GET https://api.deepseek.com/user/balance
Authorization: Bearer <API_KEY>
```

- API Key 只用于请求该 endpoint。
- LiangWenPeak 没有自己的后台服务，不会把 API Key 上传到项目自己的服务器。
- API Key 和本地历史身份密钥由 Windows Credential Locker 保存，不写入普通明文配置或余额历史。
- 余额历史保存在解压目录根部的 `data/balance-history.csv`，归档保存在 `data/history/`。
- `data/` 由应用运行时创建；Release ZIP 不包含本机用户数据，构建和打包也不会删除现有数据。
- 网络请求失败时，应用保留最近一次成功获取的余额，不会将其重置为零。

Portable 目录可以复制，但 Windows Credential Locker 中的凭据不会随目录一起迁移。

## 从源码构建

需要 Windows、Visual Studio 2022 或 Build Tools 2022、MSVC v143、与项目 target 兼容的 Windows SDK，以及 PowerShell。正式 Release 的推荐入口是：

```powershell
.\scripts\release.ps1
```

脚本会自动发现构建环境和 [`Version.props`](Version.props) 中的版本，然后执行还原、clean build、tests、staging、校验、Launcher smoke tests 和 ZIP 打包。

日常开发可使用 Visual Studio，或只构建 Debug：

```powershell
.\scripts\build.ps1 -Configuration Debug
```

## 技术细节

LiangWenPeak 使用原生 C++20、C++/WinRT、WinUI 3 与 Windows App SDK 构建。

关于运行时架构、峰谷价格模型、余额采样、Series ID、多币种隔离、预测算法、本地存储、隐私边界、构建与发布流程，请参阅 [技术规格](docs/tech-specs.md)。

版本变化与发布产物信息见 [CHANGELOG.md](CHANGELOG.md)。

## License

LiangWenPeak 使用 [Apache License 2.0](LICENSE) 开源。
