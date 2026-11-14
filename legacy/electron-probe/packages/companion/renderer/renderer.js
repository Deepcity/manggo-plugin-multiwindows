const api = window.multiwindows;

function showFatal(message) {
  const element = document.getElementById("fatal");
  element.textContent = message;
  element.hidden = false;
}

async function initialize() {
  if (!api) {
    showFatal("安全桥接未加载。");
    return;
  }

  const session = await api.getSession();
  if (!session) {
    showFatal("窗口会话已失效，请关闭后重试。");
    return;
  }

  if (session.kind === "setup") {
    document.getElementById("setup-view").hidden = false;
    const config = await api.getBridgeConfig();
    document.getElementById("endpoint").value = config?.endpoint ?? "";
    document.getElementById("token").value = config?.token ?? "";
  } else {
    document.getElementById("translation-view").hidden = false;
    document.getElementById("source-text").value = session.text;
  }
}

document.querySelectorAll("[data-create-window]").forEach((button) => {
  button.addEventListener("click", () => api.createWindow());
});

document.querySelectorAll("[data-copy]").forEach((button) => {
  button.addEventListener("click", async () => {
    const target = document.getElementById(button.dataset.copy);
    await api.copyText(target.value ?? target.textContent ?? "");
    const previous = button.textContent;
    button.textContent = "已复制";
    setTimeout(() => { button.textContent = previous; }, 1_200);
  });
});

document.getElementById("close").addEventListener("click", () => api.closeWindow());
document.getElementById("pin").addEventListener("click", async (event) => {
  const button = event.currentTarget;
  const alwaysOnTop = await api.setAlwaysOnTop(button.textContent !== "取消置顶");
  button.textContent = alwaysOnTop ? "取消置顶" : "设为置顶";
});

initialize().catch((error) => showFatal(error instanceof Error ? error.message : String(error)));
