import assert from "node:assert/strict";
import test from "node:test";
import { openInNewWindow, shouldShowAction } from "../packages/plugin/main.js";

const validConfig = {
  endpoint: "http://127.0.0.1:47832",
  token: "1234567890abcdef",
};

test("precheck only accepts non-empty selected text", () => {
  assert.equal(shouldShowAction("hello"), true);
  assert.equal(shouldShowAction("  x  "), true);
  assert.equal(shouldShowAction("   "), false);
  assert.equal(shouldShowAction(""), false);
  assert.equal(shouldShowAction(undefined), false);
});

test("action posts trimmed text to the loopback companion", async () => {
  let request;
  await openInNewWindow("  hello  ", {
    config: validConfig,
    utils: {
      fetch: async (url, init) => {
        request = { url, init };
        return new Response(JSON.stringify({ id: "window-1" }), {
          status: 201,
          headers: { "Content-Type": "application/json" },
        });
      },
    },
  });

  assert.equal(request.url, "http://127.0.0.1:47832/v1/windows");
  assert.equal(request.init.method, "POST");
  assert.equal(request.init.headers.Authorization, "Bearer 1234567890abcdef");
  assert.deepEqual(JSON.parse(request.init.body), { text: "hello" });
});

test("action rejects non-loopback endpoints before sending", async () => {
  let requested = false;
  await assert.rejects(
    openInNewWindow("hello", {
      config: { ...validConfig, endpoint: "https://example.com" },
      utils: { fetch: async () => { requested = true; } },
    }),
    /仅允许/,
  );
  assert.equal(requested, false);
});

test("action requires a useful bridge token", async () => {
  await assert.rejects(
    openInNewWindow("hello", {
      config: { ...validConfig, token: "short" },
      utils: { fetch: async () => { throw new Error("must not request"); } },
    }),
    /连接令牌/,
  );
});

test("connection failures produce an actionable message", async () => {
  await assert.rejects(
    openInNewWindow("hello", {
      config: validConfig,
      utils: { fetch: async () => { throw new TypeError("offline"); } },
    }),
    /请先启动/,
  );
});

test("HTTP errors do not leak the configured token", async () => {
  await assert.rejects(
    openInNewWindow("hello", {
      config: validConfig,
      utils: {
        fetch: async () => new Response(JSON.stringify({ error: { message: "连接令牌无效。" } }), {
          status: 401,
          headers: { "Content-Type": "application/json" },
        }),
      },
    }),
    (error) => {
      assert.match(error.message, /401/);
      assert.doesNotMatch(error.message, /1234567890abcdef/);
      return true;
    },
  );
});
