import { randomUUID } from "node:crypto";
import { join } from "node:path";
import { BrowserWindow, clipboard, ipcMain, screen } from "electron";

interface RendererSession {
  id: string;
  kind: "setup" | "translation";
  text: string;
  createdAt: string;
}

export class WindowManager {
  private readonly sessions = new Map<number, RendererSession>();
  private translationWindowCount = 0;

  constructor(
    private readonly token: string,
    private readonly port: number,
  ) {
    ipcMain.handle("session:get", (event) => this.sessions.get(event.sender.id) ?? null);
    ipcMain.handle("bridge:get-config", (event) => {
      const session = this.sessions.get(event.sender.id);
      return session?.kind === "setup"
        ? { endpoint: `http://127.0.0.1:${this.port}`, token: this.token }
        : null;
    });
    ipcMain.handle("clipboard:copy", (_event, value: unknown) => {
      if (typeof value === "string") clipboard.writeText(value);
    });
    ipcMain.handle("window:set-always-on-top", (event, enabled: unknown) => {
      const window = BrowserWindow.fromWebContents(event.sender);
      window?.setAlwaysOnTop(enabled === true);
      return window?.isAlwaysOnTop() ?? false;
    });
    ipcMain.on("window:create", () => {
      this.createTranslationWindow("");
    });
    ipcMain.on("window:close", (event) => {
      BrowserWindow.fromWebContents(event.sender)?.close();
    });
  }

  createSetupWindow(): BrowserWindow {
    return this.createWindow("setup", "MultiWindows 伴侣程序已启动", false);
  }

  createTranslationWindow(text: string): BrowserWindow {
    return this.createWindow("translation", text, true);
  }

  focusSetupWindow(): void {
    const setup = BrowserWindow.getAllWindows().find(
      (window) => this.sessions.get(window.webContents.id)?.kind === "setup",
    );
    if (setup) {
      if (setup.isMinimized()) setup.restore();
      setup.focus();
    } else {
      this.createSetupWindow();
    }
  }

  private createWindow(
    kind: RendererSession["kind"],
    text: string,
    alwaysOnTop: boolean,
  ): BrowserWindow {
    const cascade = this.translationWindowCount % 8;
    if (kind === "translation") this.translationWindowCount += 1;

    const workArea = screen.getPrimaryDisplay().workArea;
    const window = new BrowserWindow({
      width: kind === "setup" ? 560 : 480,
      height: kind === "setup" ? 600 : 640,
      x: workArea.x + 80 + cascade * 28,
      y: workArea.y + 48 + cascade * 28,
      minWidth: 360,
      minHeight: 420,
      show: false,
      alwaysOnTop,
      autoHideMenuBar: true,
      backgroundColor: "#0b1220",
      title: kind === "setup" ? "MultiWindows 设置" : "MultiWindows 翻译",
      webPreferences: {
        preload: join(__dirname, "preload.js"),
        contextIsolation: true,
        nodeIntegration: false,
        sandbox: true,
      },
    });

    this.sessions.set(window.webContents.id, {
      id: randomUUID(),
      kind,
      text,
      createdAt: new Date().toISOString(),
    });
    window.on("closed", () => {
      this.sessions.delete(window.webContents.id);
    });
    window.once("ready-to-show", () => window.show());
    void window.loadFile(join(__dirname, "renderer", "index.html"));
    return window;
  }
}
