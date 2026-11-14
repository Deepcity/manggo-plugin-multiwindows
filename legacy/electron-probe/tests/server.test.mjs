import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";

const require = createRequire(import.meta.url);
const { BridgeServer } = require("../packages/companion/dist/server.js");
const token = "test-token-1234567890abcdef";

async function fixture(t) {
  const sessions = [];
  const server = new BridgeServer({
    token,
    port: 0,
    onCreateWindow: (session) => sessions.push(session),
  });
  const port = await server.start();
  t.after(() => server.stop());
  return {
    sessions,
    endpoint: `http://127.0.0.1:${port}`,
  };
}

function createRequest(endpoint, options = {}) {
  return fetch(`${endpoint}/v1/windows`, {
    method: "POST",
    ...options,
    headers: {
      "Authorization": `Bearer ${token}`,
      "Content-Type": "application/json",
      ...options.headers,
    },
  });
}

test("health endpoint reports a running companion", async (t) => {
  const { endpoint } = await fixture(t);
  const response = await fetch(`${endpoint}/health`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), {
    ok: true,
    name: "manggo-multiwindows-companion",
    version: "0.1.0",
  });
});

test("valid requests create independent window sessions", async (t) => {
  const { endpoint, sessions } = await fixture(t);
  const first = await createRequest(endpoint, { body: JSON.stringify({ text: " first " }) });
  const second = await createRequest(endpoint, { body: JSON.stringify({ text: "second" }) });
  assert.equal(first.status, 201);
  assert.equal(second.status, 201);
  assert.equal(sessions.length, 2);
  assert.equal(sessions[0].text, "first");
  assert.equal(sessions[1].text, "second");
  assert.notEqual(sessions[0].id, sessions[1].id);
});

test("invalid tokens cannot create windows", async (t) => {
  const { endpoint, sessions } = await fixture(t);
  const response = await createRequest(endpoint, {
    headers: { Authorization: "Bearer wrong" },
    body: JSON.stringify({ text: "hello" }),
  });
  assert.equal(response.status, 401);
  assert.equal(sessions.length, 0);
});

test("browser-origin and non-loopback Host requests are rejected", async (t) => {
  const { endpoint, sessions } = await fixture(t);
  const originResponse = await createRequest(endpoint, {
    headers: { Origin: "https://example.com" },
    body: JSON.stringify({ text: "hello" }),
  });
  const hostResponse = await createRequest(endpoint, {
    headers: { Host: "example.com" },
    body: JSON.stringify({ text: "hello" }),
  });
  assert.equal(originResponse.status, 403);
  assert.equal(hostResponse.status, 403);
  assert.equal(sessions.length, 0);
});

test("malformed JSON and invalid text are rejected", async (t) => {
  const { endpoint, sessions } = await fixture(t);
  const malformed = await createRequest(endpoint, { body: "{" });
  const empty = await createRequest(endpoint, { body: JSON.stringify({ text: "  " }) });
  const extra = await createRequest(endpoint, { body: JSON.stringify({ text: "hello", extra: true }) });
  assert.equal(malformed.status, 400);
  assert.equal(empty.status, 400);
  assert.equal(extra.status, 400);
  assert.equal(sessions.length, 0);
});

test("unknown routes and wrong methods are rejected", async (t) => {
  const { endpoint } = await fixture(t);
  const missing = await fetch(`${endpoint}/missing`);
  const wrongMethod = await createRequest(endpoint, { method: "PUT", body: "{}" });
  assert.equal(missing.status, 404);
  assert.equal(wrongMethod.status, 405);
  assert.equal(wrongMethod.headers.get("allow"), "POST");
});
