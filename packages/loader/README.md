# 进程生命周期加载器原型

将同目录下的 `manggo-multiwindows.dll` 和 `manggo-multiwindows-loader.exe` 放在一起。

Manggo 已运行时：

```powershell
./manggo-multiwindows-loader.exe
```

需要由加载器启动 Manggo 时：

```powershell
./manggo-multiwindows-loader.exe 'D:\Manggo\bin\Manggo.exe'
```

卸载并恢复当前 Manggo 进程的原行为：

```powershell
./manggo-multiwindows-loader.exe --unload
```

加载器只负责当前进程生命周期，不写入启动项、不驻留后台、不修改 Manggo 安装文件。冷启动期间会有界等待原生翻译窗口完成构造；构建不匹配或初始化失败时会卸载 DLL 并返回非零退出码。
