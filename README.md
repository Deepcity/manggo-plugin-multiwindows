# MultiWindows for Manggo

> Independent input translation windows for Manggo on Windows

为 Manggo 提供多个**可独立输入、独立翻译**的原生桌面窗口。

```text
Manggo 原生集成层
  ├─ 标题栏「＋」：新建独立翻译窗口
  ├─ 输入、划词、截图分发：新建而非覆盖
  └─ Manggo 原生服务配置与 HistoryStore
       ├─ 原生翻译窗口 1
       ├─ 原生翻译窗口 2
       └─ 原生翻译窗口 N
```

## 仓库结构

```text
packages/native/          Qt 原生窗口工厂、标题栏＋和可恢复入口钩子
packages/loader/          当前进程生命周期的原生加载器原型
scripts/build-native.ps1  MSVC + Qt 构建脚本
docs/                     官方 API 整理、原生架构和验证边界
legacy/electron-probe/    已归档的前期 Electron/action 探针，不是产品入口
```

## 构建

在 x64 MSVC Developer PowerShell 中执行：

```powershell
npm run build:native -- -QtRoot '...\Qt\6.11.1\msvc2022_64'
```

产物位于 `dist/native/`。也可以使用根目录 `CMakeLists.txt`，通过 `CMAKE_PREFIX_PATH` 指向同一 Qt 安装。

## 使用原型

将 `manggo-multiwindows.dll` 与 `manggo-multiwindows-loader.exe` 保持在同一目录。Manggo 已运行时执行：

```powershell
./manggo-multiwindows-loader.exe
```

同时启动 Manggo 时：

```powershell
./manggo-multiwindows-loader.exe 'D:\Manggo\bin\Manggo.exe'
```

恢复当前进程原行为：

```powershell
./manggo-multiwindows-loader.exe --unload
```

## 当前状态

**已实现并真机验证**：关闭按钮左侧的标题栏 `＋`、独立原生窗口、输入／划词／截图最终分发、新建历史 ID、原生 Gemini 管线、并行流式回调、独立语言方向、独立置顶、进行中关闭、卸载恢复及加载器冷启动。

所有输入翻译窗口属于同一个焦点组：原窗口与新增窗口之间、新增窗口彼此之间切换不会隐藏其他窗口。离开整个窗口组时恢复原生失焦规则，未置顶窗口隐藏，置顶窗口保留；不会全局关闭“失焦关闭”设置或强制置顶。

**版本边界**：当前只支持 PE timestamp `0x6ace6168`、SizeOfImage `0x1936000` 的 Windows x64 Manggo 构建；其他构建会拒绝加载。地址不是跨版本通用接口。

**尚未完成**：词典服务专项验收、其他 Manggo 版本适配、正式安装包与自动更新策略。当前交付物是原生扩展及加载器原型，不是 Manggo 官方插件 API 可直接安装的 `.mplugin`。利用安装校验触发模块初始化的想法仅有待验证方案，尚未实验，不能视为可用的插件安装路线。

详见 [架构与验证记录](docs/architecture.md) 和 [Manggo 官方插件 API 整理](docs/manggo-plugin-development.md)。
