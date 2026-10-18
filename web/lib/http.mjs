import "server-only";
import { InputError } from "./validation.mjs";

export class HttpError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}
export function checkOrigin(request, config) {
  if (request.headers.get("origin") !== config.origin)
    throw new HttpError(403, "Request origin is not allowed.");
}
export async function readJson(request) {
  if (!request.headers.get("content-type")?.toLowerCase().startsWith("application/json"))
    throw new HttpError(415, "Send JSON.");
  if (!request.body) throw new HttpError(400, "Request body is required.");
  const reader = request.body.getReader();
  const chunks = [];
  let size = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > 2048) {
        await reader.cancel();
        throw new HttpError(413, "Request is too large.");
      }
      chunks.push(Buffer.from(value));
    }
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch (error) {
    if (error instanceof HttpError) throw error;
    throw new HttpError(400, "Invalid JSON.");
  } finally { reader.releaseLock(); }
}
export function json(value, status = 200, headers = {}) {
  return Response.json(value, {
    status, headers: { "Cache-Control": "private, no-store", ...headers },
  });
}
export function failure(error) {
  if (error instanceof HttpError) return json({ error: error.message }, error.status);
  if (error instanceof InputError) return json({ error: error.message }, 400);
  // Never log request bodies, credentials, tokens or DB connection strings.
  console.error("Request failed; check server configuration and database availability.");
  return json({ error: "Service unavailable. Check configuration and database setup, then try again." }, 503);
}

