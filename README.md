<p align="center">
  <img src="branding/cursor-agent.webp" alt="CursorUsageMonitor" width="168" height="184" />
</p>

<h1 align="center"><img src="branding/titlebar.png" alt="任务栏图标" width="36" height="36" align="absmiddle" />&nbsp;&nbsp;CursorUsageMonitor</h1>

<p align="center">
  <a href="https://github.com/zhongyang219/TrafficMonitor">TrafficMonitor</a> 插件：在任务栏显示本机 Cursor 账号的套餐占用、已用金额和按量费用。
</p>

<p align="center">
  <code>套餐: 27%</code>
  &nbsp;&nbsp;<code>已用: $15.16/$20</code>
  &nbsp;&nbsp;<code>按量: $0.00</code>
  &nbsp;&nbsp;<code>Auto: 24%</code>
  &nbsp;&nbsp;<code>API: 64%</code>
  <br />
  <sub>任务栏示例</sub>
</p>

纯 C++ 实现，编出来只有一个 `CursorUsageMonitor.dll`。

## 显示项

| 名称 | ID | 标签 | 示例 | 数据 |
|---|---|---|---|---|
| Cursor 套餐占用 | `CursorPlan` | `套餐:` | `27%` | `individualUsage.plan.totalPercentUsed`，任务栏可画占用条 |
| Cursor 套餐已用 | `CursorSpend` | `已用:` | `$15.16/$20` | `plan.used` / `plan.limit`（美分） |
| Cursor 按量费用 | `CursorOnDemand` | `按量:` | `$0.00` | `individualUsage.onDemand.used`；个人未开启时用 `teamUsage.onDemand`；都没开显示 `$0` |
| Cursor Auto 占用 | `CursorAuto` | `Auto:` | `24%` | `plan.autoPercentUsed`，任务栏可画占用条 |
| Cursor API 占用 | `CursorApi` | `API:` | `64%` | `plan.apiPercentUsed`，任务栏可画占用条 |

还没拿到数据时显示 `--`。

鼠标悬停提示包含：账号与会员类型、Auto/API 占用、赠送额度已用、账期结束（本地时间与剩余天数）、上次更新时间、刷新状态和失败原因。

## 刷新

- 后台线程拉取，`DataRequired` 只读缓存，不会卡 TrafficMonitor。
- 默认每 5 分钟刷新一次；失败时 1 分钟后重试，并保留上次成功的数值。
- 右键插件项目 →“立即刷新”。
- 插件选项（TrafficMonitor →“插件管理”→ 选中本插件 →“选项”）可改刷新间隔（1–1440 分钟），保存在 TrafficMonitor 插件配置目录下的 `CursorUsageMonitor.ini`。

## 原理

1. 找登录态（只看当前 Windows 用户），顺序同 `SessionDiscoverer`：
   - 环境变量 `CURSOR_SESSION_TOKEN` / `WORKOS_CURSOR_SESSION_TOKEN`（支持 `sub::jwt`、`sub%3A%3Ajwt`）
   - `%APPDATA%\Cursor\User\globalStorage\state.vscdb`（及 `.backup`），内嵌 SQLite 以 `mode=ro&immutable=1` 只读打开，读 `cursorAuth/accessToken` 等
   - `~\.cursor\auth.json` 等 CLI auth 文件
   - 按 `glass.lastSignedInAuthId` 优先当前登录账号，跳过已过期的 JWT
2. 拼 Cookie `WorkosCursorSessionToken=<sub>%3A%3A<jwt>`。
3. 用 WinHTTP（跟随系统代理）请求：
   - `GET https://cursor.com/api/auth/me`
   - `GET https://cursor.com/api/usage-summary`

不写任何用量数据到磁盘，只有刷新间隔写进 ini。

## 构建

需要 Visual Studio 2019 及以上（或 Build Tools），装好“使用 C++ 的桌面开发”工作负载。CMake 用 VS 自带的即可。

```powershell
.\build.ps1                                            # 产物：build\x64\Release\CursorUsageMonitor.dll
.\build.ps1 -PluginDir "D:\Tools\TrafficMonitor\plugins"   # 编完直接复制到插件目录
.\build.ps1 -Platform Win32                            # 32 位 TrafficMonitor 用
```

插件位数必须和 TrafficMonitor 一致。

## 安装

1. 退出 TrafficMonitor。
2. 把 `CursorUsageMonitor.dll` 放进 `TrafficMonitor.exe` 同级的 `plugins` 目录（没有就新建）。
3. 启动 TrafficMonitor，在任务栏窗口右键 →“显示设置”里勾选要显示的 Cursor 项目。

DLL 依赖 Visual C++ 2015–2022 运行库（`vcruntime140.dll`、`msvcp140.dll`），大多数电脑已经装了。

## 目录

```
include/PluginInterface.h   TrafficMonitor 官方插件接口（API 版本 8）
src/Session.cpp             登录态发现、JWT、Cookie
src/UsageClient.cpp         WinHTTP 请求与 usage-summary 解析
src/DataManager.cpp         后台刷新、缓存、ini
src/UsageItem.cpp           五项显示项
src/Plugin.cpp              ITMPlugin、提示、命令、选项对话框、TMPluginGetInstance
third_party/                nlohmann/json 3.12.0、SQLite 3.54.0 amalgamation
```

## 第三方

- `PluginInterface.h`：来自 [zhongyang219/TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)
- [nlohmann/json](https://github.com/nlohmann/json)：MIT
- [SQLite](https://www.sqlite.org/)：Public Domain
