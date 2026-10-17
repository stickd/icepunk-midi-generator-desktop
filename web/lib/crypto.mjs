import "server-only";
import { createHash, createHmac, randomBytes, scrypt, timingSafeEqual } from "node:crypto";
import { promisify } from "node:util";

const derive = promisify(scrypt);
export const SESSION_SECONDS = 8 * 60 * 60;

export function equalText(a, b) {
  const left = createHash("sha256").update(a).digest();
  const right = createHash("sha256").update(b).digest();
  return timingSafeEqual(left, right);
}

export async function hashPassword(password) {
  const salt = randomBytes(16).toString("hex");
  const key = await derive(password, salt, 64);
  return `scrypt:${salt}:${key.toString("hex")}`;
}

export async function verifyPassword(password, stored) {
  if (typeof password !== "string" || password.length > 1024) return false;
  const [, salt, expected] = stored.split(":");
  const actual = await derive(password, salt, 64);
  return timingSafeEqual(actual, Buffer.from(expected, "hex"));
}

function signature(payload, config) {
  // Changing either credential invalidates all existing sessions.
  return createHmac("sha256", Buffer.from(config.sessionSecret, "hex"))
    .update(config.username).update("\0").update(config.passwordHash).update("\0")
    .update(payload).digest("base64url");
}

export function createSession(config, now = Date.now()) {
  const issuedAt = Math.floor(now / 1000);
  const payload = Buffer.from(JSON.stringify({
    v: 1, iat: issuedAt, exp: issuedAt + SESSION_SECONDS,
    nonce: randomBytes(16).toString("hex"),
  })).toString("base64url");
  return `${payload}.${signature(payload, config)}`;
}

export function verifySession(token, config, now = Date.now()) {
  if (typeof token !== "string" || token.length > 1024) return false;
  const parts = token.split(".");
  if (parts.length !== 2 || !equalText(parts[1], signature(parts[0], config))) return false;
  try {
    const data = JSON.parse(Buffer.from(parts[0], "base64url").toString("utf8"));
    const current = Math.floor(now / 1000);
    return data.v === 1 && Number.isInteger(data.iat) && Number.isInteger(data.exp)
      && data.iat <= current && data.exp > current
      && data.exp - data.iat === SESSION_SECONDS
      && typeof data.nonce === "string" && /^[a-f0-9]{32}$/.test(data.nonce);
  } catch { return false; }
}

export function newInviteToken() {
  const token = randomBytes(32).toString("base64url");
  return { token, hash: createHash("sha256").update(token).digest("hex") };
}

