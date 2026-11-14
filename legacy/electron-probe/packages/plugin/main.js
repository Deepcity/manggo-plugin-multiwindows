const DEFAULT_ENDPOINT = "http://127.0.0.1:47832";

export function shouldShowAction(selectedText) {
  return typeof selectedText === "string" && selectedText.trim().length > 0;
}

function normalizeEndpoint(value) {
  let url;
  try {
    url = new URL(String(value || DEFAULT_ENDPOINT));
  } catch {
    throw new Error("伴侣程序地址不是有效的 URL。");
  }

  if (url.protocol !== "http:" || url.hostname !== "127.0.0.1") {
    throw new Error("伴侣程序地址仅允许使用 http://127.0.0.1。");
  }
  if (url.username || url.password || url.search || url.hash) {
    throw new Error("伴侣程序地址不能包含凭据、查询参数或片段。");
  }

  return url.toString().replace(/\/$/, "");
}

function readConfig(options) {
  return options && options.config ? options.config : {};
}

export async function openInNewWindow(selectedText, options) {
  const text = String(selectedText ?? "").trim();
  if (!text) {
    throw new Error("没有可发送的选中文本。");
  }

  const config = readConfig(options);
  const endpoint = normalizeEndpoint(config.endpoint);
  const token = String(config.token ?? "").trim();
  if (token.length < 16) {
    throw new Error("请先在动作设置中填写伴侣程序显示的连接令牌。");
  }
  if (!options?.utils?.fetch) {
    throw new Error("当前 Manggo 运行时未提供网络请求能力。");
  }

  let response;
  try {
    response = await options.utils.fetch(`${endpoint}/v1/windows`, {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ text }),
    });
  } catch {
    throw new Error("无法连接 MultiWindows 伴侣程序，请先启动它并检查端口。");
  }

  if (!response.ok) {
    let detail = "";
    try {
      const data = await response.json();
      detail = String(data?.error?.message ?? "").trim();
    } catch {
      // 非 JSON 错误响应不需要暴露给插件用户。
    }
    throw new Error(
      `伴侣程序拒绝了请求（HTTP ${response.status}）${detail ? `：${detail}` : "。"}`,
    );
  }

  const result = await response.json();
  if (typeof result?.id !== "string" || !result.id) {
    throw new Error("伴侣程序返回了无法识别的响应。");
  }
}
