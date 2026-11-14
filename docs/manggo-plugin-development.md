# Manggo 原生插件开发资料整理

整理日期：2026-11-13。本文以官方仓库当前 `main` 分支的开发指南为准，并用一个已发布 `.mplugin` 的开源项目交叉核对工程结构。

## 1. 一手资料

- 官方插件模板与完整指南：<https://github.com/Pylogmon/manggo-plugin>
- 官方 README 原始文件：<https://raw.githubusercontent.com/Pylogmon/manggo-plugin/main/README.md>
- 实际发布样例（翻译＋OCR）：<https://github.com/SchweppesSoda/bailian-plugins>
- Manggo 官网：<https://manggo.pylogmon.cn/>

插件系统属于 Manggo Pro 功能。本文只讨论 Manggo 原生 `.mplugin`，不是 Pot、Bob 或浏览器插件格式。

## 2. 能力总览

原生插件由 `manggo.plugin.json`、ES module 入口和可选资源组成，使用 Bun 运行，通过 `manggo.plugin.v1` 与宿主通信。

| `services[].kind` | 用途 | 推荐入口签名 | 最终返回 |
| --- | --- | --- | --- |
| `translation` | 文本翻译／词典 | `translate(text, from, to, options)` | 字符串或严格字典结构 |
| `ocr` | 图片文字识别 | `recognize(base64, language, options)` | 字符串 |
| `speech` | 文本转语音 | `tts(text, language, options)` | 音频字节、Base64 或约定对象 |
| `wordbook` | 添加生词 | `add(request, options)` | 空值、布尔值、提示字符串或结果对象 |
| `action` | 划词助手动作 | `entry(selectedText, options)` | 普通返回值不展示 |

一个插件可以在 `services` 中声明多个服务。普通服务需要用户在对应服务设置中添加实例；action 安装后直接出现在“划词助手”动作列表。

## 3. 最小插件与打包格式

```text
my-plugin/
├── manggo.plugin.json
├── main.js
└── icon.png              # 可选
```

最小清单：

```json
{
  "manifestVersion": 1,
  "id": "com.example.my-plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "permissions": ["network"],
  "runtime": {
    "kind": "bun",
    "api": "manggo.plugin.v1",
    "main": "main.js"
  },
  "services": [
    {
      "id": "my-action",
      "kind": "action",
      "displayName": "My Action",
      "entry": "executeAction"
    }
  ]
}
```

`.mplugin` 本质是 ZIP。`manggo.plugin.json` 必须位于压缩包根目录，不能再套一层文件夹。官方示例将 `manggo.plugin.json`、`main.js` 和图标直接压入根目录。

## 4. 清单字段

### 顶层

- 必填：`manifestVersion: 1`、`id`、`name`、`version`、`runtime`、非空 `services`。
- 可选：`description`、`author`、`homepage`、`icon`、`permissions`。
- 插件 ID 推荐反向域名；版本推荐语义化版本。
- 网络访问声明 `permissions: ["network"]`；顶层与服务级权限会合并。

### `runtime`

当前固定使用：

```json
{
  "kind": "bun",
  "api": "manggo.plugin.v1",
  "main": "main.js"
}
```

`services[].main` 可以覆盖默认入口。

### `services[]`

常用字段：

| 字段 | 含义 |
| --- | --- |
| `id` | 插件内唯一服务 ID |
| `kind` | 五类服务之一 |
| `displayName` | 用户可见名称 |
| `entry` | 入口导出的函数名，必须完全一致 |
| `main` | 可选的服务专用入口 |
| `providerId` | 普通服务提供方 ID；action 不使用 |
| `resultType` | 翻译服务的 `text` 或 `dictionary` |
| `language` | Manggo 语言代码到第三方代码的映射 |
| `config` | 配置表单 |
| `precheck` | action 可选预检函数 |
| `iconName` | action 的 Material Symbols 图标名 |
| `enabledByDefault` | action 是否默认启用，默认 `true` |

翻译／OCR／语音只声明实际支持的语言映射。传入入口的是映射后的值，自动检测语言可能是 `auto`。

## 5. 配置表单

保存后的值通过 `options.config` 传入，尚未保存时使用清单中的 `default`。

支持控件：

| `control` | 值类型／用途 |
| --- | --- |
| `text` | 单行字符串 |
| `password` | 敏感单行字符串，默认按 secret 处理 |
| `textarea` | 多行字符串 |
| `select` | 下拉选择，可允许自定义值 |
| `integer` | 整数 |
| `decimal` | 小数 |
| `boolean` | 布尔开关 |

字段支持 `key`、`label`、`control`、`placeholder`、`description`、`required`、`secret`、`editable`、`default` 和 `options`。插件只应依赖自己声明的键，下划线开头的内部字段不属于稳定 API。

## 6. JavaScript 运行时

入口使用 ES module，可命名导出，也可默认导出对象。不要使用 CommonJS `require()` 或未声明全局对象。

所有入口最后都有 `options`：

| 字段 | 能力 |
| --- | --- |
| `options.config` | 当前服务／动作配置 |
| `options.detect` | 翻译请求的已映射检测语言 |
| `options.setResult(chunk)` | 翻译／OCR 的增量文本输出 |
| `options.utils` | 网络、文件和运行时信息 |
| `options.manggo` | 仅 action 执行函数可用的宿主操作 |

`options.utils`：

- `fetch(url, init)`：标准 Fetch 风格请求；
- `readTextFile(path)`、`readBinaryFile(path)`：读取资源；
- `cacheDir`：可写缓存目录；
- `pluginDir`：插件安装目录，用于读取随包资源；
- `osType`：`Darwin`、`Windows_NT` 或 `Linux`；
- `setResult(chunk)`：等价于 `options.setResult`。

流式接口只发送新增片段，不要反复发送累计全文。词典、音频和生词本结果作为最终返回值，不传给 `setResult`。

## 7. 划词动作（本项目关键）

### 声明与预检

```json
{
  "id": "open-window",
  "kind": "action",
  "displayName": "Open Window",
  "iconName": "open_in_new",
  "precheck": "shouldShowAction",
  "entry": "openInNewWindow",
  "enabledByDefault": true,
  "config": []
}
```

预检签名：`shouldShowAction(selectedText, options)`。

- 只有严格布尔值 `true` 才显示动作；
- 可异步，但应快速、确定且无副作用；
- 不提供 `options.manggo`；
- 不声明预检时，动作对所有非空选区可见。

### 执行与宿主操作

执行签名：`openInNewWindow(selectedText, options)`。可以同步或异步；异常会显示为动作错误，普通返回值不会展示。

`options.manggo` 只有三个公开操作：

| 函数 | 能力 |
| --- | --- |
| `copyToClipboard(text)` | 写系统剪贴板 |
| `replaceSelectedText(text)` | 安全校验后替换原选区 |
| `openWithDefaultBrowser(url)` | 用默认浏览器打开有效 HTTP/HTTPS URL |

宿主操作进入 Manggo 队列，不向 JavaScript 返回执行回执；`await` 得到 `undefined` 不代表操作已完成。

**公开 API 没有创建桌面窗口、启动进程、访问翻译结果或调用 Manggo 当前翻译服务的能力。** 本项目因此需要单独的 Windows 原生集成研究；公开 action 桥接只保留为前期探针，不能实现目标架构。

## 8. 普通服务结果要点

- 文本翻译直接返回字符串，不是 `{ text: "..." }`。
- OCR 输入为不带 data URL 前缀的 PNG Base64，返回识别字符串。
- 词典使用 `resultType: "dictionary"`，外层严格为 `{ kind: "dictionary", dictionary: {...} }`，不要添加包装或字段别名。
- 语音可返回 `Uint8Array`、`ArrayBuffer`、数字数组、Base64 字符串或官方约定对象。
- 生词本请求含 `sourceText`、`translatedText`、语言代码和完整 `translationResult`。

## 9. 错误处理与工程实践

- 请求前校验配置，请求后检查 `response.ok`。
- 抛出可读的 `Error`，不要吞异常或把内部堆栈直接展示给用户。
- 不在日志、错误、返回值、截图或仓库中输出 Token／API Key。
- 对外部响应做结构和空值校验。
- 写运行数据使用 `cacheDir`，不要写安装目录或开发机绝对路径。
- 修改内容时同步更新插件 `version`，重新打包安装验证。
- 打包后检查清单、入口导出、图标和资源是否都位于包内。

## 10. 安装、发布与验证

在 Manggo“插件管理”安装 `.mplugin`。action 自动注册，普通服务还需到对应设置页添加实例。

发布到 GitHub 后可添加 `manggo-plugin` topic，供插件列表索引。建议 Release 同时提供 `.mplugin` 和 SHA-256。

自动化测试只能验证清单、入口和模拟请求，不能替代真机验收。至少验证安装、配置保存、启停、预检、执行、异常提示，以及普通服务的实际添加与调用。

## 11. 与 MultiWindows 的边界

公开 action 无法完成目标原生窗口、服务复用和历史隔离。仓库中的 action＋回环 HTTP 代码仅是前期探针，尚未真机验证，不是本项目采用的最终实现。目标集成点和验证状态统一记录在 `docs/architecture.md`。

本机免费版在“初始化插件运行环境”时直接提示需要专业版，因此目前不能通过正常插件安装流程做初始化探针。插件在独立 `bun.exe` 中运行；内置 runner 的模块 URL 携带请求标识并在每次调用时重新 `import()`，没有公开的安装／启用生命周期。`enabledByDefault` 只影响划词动作是否默认启用，不会执行初始化代码。

## 12. 待验证：安装校验是否触发模块导入

一个后续实验方向是：检查 `.mplugin` 安装校验是否会导入入口模块。如果确实会，可评估是否能借该过程触发原生模块初始化。但这只是研究假设，不是已确认能力，也不应绕过 Pro 授权或把副作用伪装成校验。

实验前必须保持当前 exe 加载器作为可用路线。实验应使用无实际改造能力的最小探针，只记录模块是否执行、执行时序、可用宿主对象和进程归属；确认行为后再评估设计。不得先声称它支持安装即初始化，也不得将它写入使用说明作为正式安装方式。
