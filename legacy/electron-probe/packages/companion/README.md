# 前期 Electron 窗口探针

> 此目录不属于目标架构，尚未完成构建或真机验收，请勿作为 MultiWindows 成品使用。

这是用于验证独立窗口交互与回环桥接的 Electron＋TypeScript 骨架，包含本地令牌鉴权、请求校验、级联窗口和最小化 preload。

它不能复用 Manggo 原生翻译服务与历史，也不应在原生路线中新增 provider、外部历史库或安装程序。目标实现见 `docs/architecture.md`。
