import { createHash, randomUUID, timingSafeEqual } from "node:crypto";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import {
  DEFAULT_PORT,
  MAX_BODY_BYTES,
  isCreateWindowRequest,
  type WindowSession,
} from "./protocol";

interface BridgeServerOptions {
  token: string;
  port?: number;
  onCreateWindow: (session: WindowSession) => void | Promise<void>;
}

class HttpError extends Error {
  constructor(
    public readonly statusCode: number,
    message: string,
  ) {
    super(message);
  }
}

function sendJson(response: ServerResponse, statusCode: number, value: unknown): void {
  const body = JSON.stringify(value);
  response.writeHead(statusCode, {
    "Cache-Control": "no-store",
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "X-Content-Type-Options": "nosniff",
  });
  response.end(body);
}

function sendError(response: ServerResponse, statusCode: number, message: string): void {
  sendJson(response, statusCode, { error: { message } });
}

function tokensEqual(actual: string, expected: string): boolean {
  const actualDigest = createHash("sha256").update(actual).digest();
  const expectedDigest = createHash("sha256").update(expected).digest();
  return timingSafeEqual(actualDigest, expectedDigest);
}

async function readJson(request: IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of request) {
    const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    size += buffer.length;
    if (size > MAX_BODY_BYTES) {
      throw new HttpError(413, "请求体过大。");
    }
    chunks.push(buffer);
  }

  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new HttpError(400, "请求体必须是有效的 JSON。");
  }
}

export class BridgeServer {
  private readonly port: number;
  private server?: Server;

  constructor(private readonly options: BridgeServerOptions) {
    if (options.token.length < 16) {
      throw new Error("Bridge token must contain at least 16 characters.");
    }
    this.port = options.port ?? DEFAULT_PORT;
  }

  async start(): Promise<number> {
    if (this.server) throw new Error("Bridge server is already running.");

    const server = createServer((request, response) => {
      void this.handleRequest(request, response);
    });
    server.headersTimeout = 5_000;
    server.requestTimeout = 10_000;
    this.server = server;

    await new Promise<void>((resolve, reject) => {
      const onError = (error: Error) => {
        this.server = undefined;
        reject(error);
      };
      server.once("error", onError);
      server.listen(this.port, "127.0.0.1", () => {
        server.off("error", onError);
        resolve();
      });
    });

    const address = server.address() as AddressInfo;
    return address.port;
  }

  async stop(): Promise<void> {
    const server = this.server;
    if (!server) return;
    this.server = undefined;
    await new Promise<void>((resolve, reject) => {
      server.close((error) => (error ? reject(error) : resolve()));
      server.closeAllConnections();
    });
  }

  private async handleRequest(request: IncomingMessage, response: ServerResponse): Promise<void> {
    try {
      const port = (this.server?.address() as AddressInfo | null)?.port ?? this.port;
      if (request.headers.host !== `127.0.0.1:${port}` || request.headers.origin !== undefined) {
        throw new HttpError(403, "仅接受来自 Manggo 插件的本机请求。");
      }

      const url = new URL(request.url ?? "/", "http://127.0.0.1");
      if (request.method === "GET" && url.pathname === "/health") {
        sendJson(response, 200, {
          ok: true,
          name: "manggo-multiwindows-companion",
          version: "0.1.0",
        });
        return;
      }

      if (url.pathname !== "/v1/windows") {
        throw new HttpError(404, "接口不存在。");
      }
      if (request.method !== "POST") {
        response.setHeader("Allow", "POST");
        throw new HttpError(405, "仅支持 POST 请求。");
      }

      const authorization = request.headers.authorization ?? "";
      if (!tokensEqual(authorization, `Bearer ${this.options.token}`)) {
        throw new HttpError(401, "连接令牌无效。");
      }
      if (request.headers["content-type"]?.split(";", 1)[0].trim() !== "application/json") {
        throw new HttpError(415, "Content-Type 必须是 application/json。");
      }

      const body = await readJson(request);
      if (!isCreateWindowRequest(body)) {
        throw new HttpError(400, `text 必须是非空字符串，且不超过 20000 个字符。`);
      }

      const session: WindowSession = {
        id: randomUUID(),
        text: body.text.trim(),
        createdAt: new Date().toISOString(),
      };
      await this.options.onCreateWindow(session);
      sendJson(response, 201, { id: session.id });
    } catch (error) {
      const statusCode = error instanceof HttpError ? error.statusCode : 500;
      const message = error instanceof HttpError ? error.message : "伴侣程序未能创建窗口。";
      if (!response.headersSent) sendError(response, statusCode, message);
    }
  }
}
