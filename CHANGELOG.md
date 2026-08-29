# 更新日志

## 1.1.2

新增峰谷 Toast 通知，并将原“API Key 功能”窗口升级为统一的“设置”窗口。

### 亮点

- 新增峰谷到达通知与提前提醒，通知默认关闭，提前时间可在 `1–30` 分钟之间设置
- 提前提醒错过后不补发，到达提醒可在真实峰谷切换后的 15 分钟内补发一次
- 主菜单新增“启用通知”快捷开关，并提供“测试通知”
- 使用 Windows 原生 Toast，在 portable ZIP 下无需额外 MSIX 或安装程序
- 设置窗口整合 API、余额与通知配置，并优化高 DPI 下的布局与原生 `NumberBox`
- 主菜单“设置...”改用 WinUI 原生齿轮图标
- 新增“彻底清理”，可移除应用保存的 Credential Locker、Registry 与通知系统集成信息，同时保留全部 `data/`
- 改进测试隔离，开发和测试过程不会影响其他正在运行的 LiangWenPeak 实例

### 下载

`LiangWenPeak-1.1.2-windows-x64.zip`

SHA256：

```text
1A51C58CA91551553670C05339BD5D0929647B63643FF2801F7F64F1D15FF4D9
```

## 1.1.1

新增 Windows 11 Fluent Theme 视觉适配。

### 亮点

* Windows 11 默认启用“Fluent 主题”，可从主窗口菜单随时关闭并持久化选择
* Windows 10 完全隐藏主题菜单项，保持原有深蓝纯色窗口与功能行为
* 主窗口和 API Key 设置窗口适配 Windows 11 圆角、原生焦点视觉及 WinUI 控件样式
* 菜单继续使用 WinUI `FontIcon` 矢量图标，不使用 Emoji、Mica、Acrylic 或透明背景
* 设置窗口的余额预测开关升级为原生 `ToggleSwitch`，不改变保存、取消和草稿状态模型
* About 页面精简为项目名称、GitHub 项目链接、Apache License 2.0 许可证和版本号
* Fluent Theme 逻辑保持在 App UI 层，不影响 API、余额历史、预测算法或 Core Library

### 下载

`LiangWenPeak-1.1.1-windows-x64.zip`

SHA256：

```text
121B0000D0B018A810EED4AF6056F76137CB97DB0457964820F0A96AD1648ECD
```

## 1.1.0

新增本地余额统计与预测。

### 亮点

* 按固定时钟边界记录全部返回币种的余额历史，手动与即时刷新只更新 observation，不污染预测采样
* 支持 CNY、USD 等返回币种切换；各币种的历史、预警、API 消耗速率和 ETA 独立计算
* 支持滑动平均、指数平均和稳健趋势三种预测算法，最短窗口自动使用“最近有效采样”
* 新增 API 总开关；关闭后停止余额请求和采样，同时保留 API Key、设置与历史
* 新增独立的“API Key 功能”设置窗口，集中管理 Key、刷新周期、币种、预警、窗口和算法
* 本地历史使用 `data/balance-history.csv` append-only CSV，并支持归档到 `data/history/` 后重新开始统计
* 修复主窗口动态高度、设置滚动区域间距以及 owned topmost 设置窗口的层级与激活问题
* Portable 打包不包含、不覆盖也不删除本地 `data/` 用户数据

### 下载

`LiangWenPeak-1.1.0-windows-x64.zip`

SHA256：

```text
AC514B39F2BFAAB8012A8BB130B8FEB44E7992DF6740179452D4353298E9BAD2
```

## 1.0.0

首次公开发布。

### 亮点

* 使用原生 C++20、C++/WinRT 与 WinUI 3 构建的紧凑型桌面置顶小窗
* 支持工作日峰谷计价状态显示
* 周六、周日全天显示为梁文谷半价时段
* 按北京时间显示距离下一次真实价格变化的倒计时与下一时段
* 支持跨周末超过 24 小时的长倒计时
* 可选的 DeepSeek API 余额查询，并支持按整分钟对齐的自动刷新
* 使用 Windows Credential Locker 安全保存 API Key
* 使用原生 Win32 Launcher 与 self-contained portable 目录结构
* 支持一条命令完成构建、测试、校验与打包

### 下载

`LiangWenPeak-1.0.0-windows-x64.zip`

SHA256：

```text
A38F6ABEDBF91497E2D70CBE612D879930B1359F60163FEA5BD900F7CB850C4E
```
