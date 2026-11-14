# 原生集成层

该 DLL 在 Manggo 进程内创建真实 `TranslationWindow`，复用原应用的 `AppContext`、翻译服务和 `HistoryStore`。它不实现翻译服务，也不建立独立历史库。

当前只接受以下精确构建，其他版本必须重新验证地址与机器码后才能启用：

- Windows x64
- PE timestamp：`0x6ace6168`
- SizeOfImage：`0x1936000`
- Qt：6.11.1（MSVC 64 位）

导出函数：`InitializeMultiWindows`、`ShutdownMultiWindows`、`InspectMultiWindows`。初始化与卸载必须在 Manggo GUI 线程执行；初始化失败时不保留部分钩子。

## 窗口组失焦规则

原窗口和新增窗口视为一个焦点组。`WindowDeactivate` 入口为组内所有窗口调用原生延迟关闭调度，原生 `window+0x246` 标志负责去重；置顶窗口产生的失焦事件也不例外。

延迟回调从真实函数入口接管，使用自定义跳板保留各动作分支。回调执行时同时检查 Qt 活动窗口和 Windows 前台 HWND：焦点仍在任一翻译窗口内时，只清除本次待关闭标志；焦点离开窗口组时交回原函数，由原应用逐窗口执行失焦设置、置顶、活动状态及 popup/modal 判断。显式关闭按钮不经过该失焦回调。

卸载会恢复输入、划词、截图、窗口事件和失焦回调五处钩子，并删除新增窗口及扩展按钮。
