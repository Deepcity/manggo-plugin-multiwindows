export const DEFAULT_PORT = 47832;
export const MAX_TEXT_LENGTH = 20_000;
export const MAX_BODY_BYTES = 64 * 1024;

export interface CreateWindowRequest {
  text: string;
}

export interface WindowSession {
  id: string;
  text: string;
  createdAt: string;
}

export function isCreateWindowRequest(value: unknown): value is CreateWindowRequest {
  if (!value || typeof value !== "object") return false;
  const text = (value as Record<string, unknown>).text;
  return (
    typeof text === "string" &&
    text.trim().length > 0 &&
    text.length <= MAX_TEXT_LENGTH &&
    Object.keys(value).every((key) => key === "text")
  );
}
