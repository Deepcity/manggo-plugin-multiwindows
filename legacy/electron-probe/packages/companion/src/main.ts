import { randomBytes } from "node:crypto";
import { mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { app, dialog } from "electron";
import { DEFAULT_PORT } from "./protocol";
import { BridgeServer } from "./server";
import { WindowManager } from "./windows";

const smokeTest = process.argv.includes("--smoke-test");
let bridge: BridgeServer | undefined;
let windows: WindowManager | undefined;

function loadOrCreateToken(): string {
  const directory = app.getPath("userData");
  const path = join(directory, "settings.json");
  try {
    const settings = JSON.parse(readFileSync(path, "utf8")) as Record<string, unknown>;
    if (typeof settings.bridgeToken === "string" && settings.bridgeToken.length >= 32) {
      return settings.bridgeToken;
    }
  } catch {
    // 首次启动或配置损坏时生成新令牌。
  }

  const bridgeToken = randomBytes(32).toString("hex");
  mkdirSync(directory, { recursive: true });
  const temporary = `${path}.tmp`;
  writeFileSync(temporary, `${JSON.stringify({ bridgeToken }, null, 2)}\n`, {
    encoding: "utf8",
    mode: 0o600,
  });
  renameSync(temporary, path);
  return bridgeToken;
}

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on("second-instance", () => windows?.focusSetupWindow());

  app.whenReady().then(async () => {
    const token = loadOrCreateToken();
    windows = new WindowManager(token, DEFAULT_PORT);
    bridge = new BridgeServer({
      token,
      onCreateWindow: (session) => {
        windows?.createTranslationWindow(session.text);
      },
    });
    await bridge.start();
    windows.createSetupWindow();

    if (smokeTest) {
      setTimeout(() => {
        windows?.createTranslationWindow("MultiWindows smoke test");
        setTimeout(() => app.quit(), 1_000);
      }, 500);
    }
  }).catch((error: unknown) => {
    const message = error instanceof Error ? error.message : String(error);
    dialog.showErrorBox("MultiWindows 启动失败", message);
    app.quit();
  });

  app.on("window-all-closed", () => app.quit());
  app.on("will-quit", () => {
    void bridge?.stop();
  });
}
