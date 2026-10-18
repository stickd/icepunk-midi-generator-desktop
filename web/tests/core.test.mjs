import test from "node:test";
import assert from "node:assert/strict";
import { randomBytes, createHash } from "node:crypto";
import { getConfig } from "../lib/config.mjs";
import { hashPassword, verifyPassword, createSession, verifySession, newInviteToken, SESSION_SECONDS } from "../lib/crypto.mjs";
import { parseInvite } from "../lib/validation.mjs";
import { checkOrigin, readJson, failure } from "../lib/http.mjs";

const passwordHash = await hashPassword("a-test-password-never-used-in-production");
const env = {
  DATABASE_URL: "postgresql://test:test@example.neon.tech/test?sslmode=require",
  APP_ORIGIN: "https://admin.example", ADMIN_USERNAME: "owner",
  ADMIN_PASSWORD_HASH: passwordHash, ADMIN_SESSION_SECRET: randomBytes(32).toString("hex"),
  NODE_ENV: "production",
};
const config = getConfig(env);

test("configuration fails closed for missing, weak or insecure values", () => {
  for (const name of ["DATABASE_URL", "APP_ORIGIN", "ADMIN_USERNAME", "ADMIN_PASSWORD_HASH", "ADMIN_SESSION_SECRET"])
    assert.throws(() => getConfig({ ...env, [name]: "" }));
  assert.throws(() => getConfig({ ...env, ADMIN_SESSION_SECRET: "short" }));
  assert.throws(() => getConfig({ ...env, ADMIN_PASSWORD_HASH: "plaintext" }));
  assert.throws(() => getConfig({ ...env, APP_ORIGIN: "http://admin.example" }));
  assert.throws(() => getConfig({ ...env, APP_ORIGIN: "https://admin.example/path" }));
  assert.equal(getConfig({ ...env, NODE_ENV: "development", APP_ORIGIN: "http://localhost:3000" }).secure, false);
});

test("salted password hash accepts only the correct password", async () => {
  assert.equal(await verifyPassword("a-test-password-never-used-in-production", passwordHash), true);
  assert.equal(await verifyPassword("wrong", passwordHash), false);
  assert.notEqual(await hashPassword("a-test-password-never-used-in-production"), passwordHash);
});

test("sessions expire, resist tampering, and are invalidated by credential rotation", () => {
  const now = 1700000000000;
  const token = createSession(config, now);
  assert.equal(verifySession(token, config, now), true);
  assert.equal(verifySession(token, config, now + SESSION_SECONDS * 1000), false);
  assert.equal(verifySession(token, config, now - 1000), false);
  assert.equal(verifySession(token + "x", config, now), false);
  const [payload, signature] = token.split(".");
  const changed = Buffer.from(JSON.stringify({ ...JSON.parse(Buffer.from(payload, "base64url")), exp: 9999999999 })).toString("base64url");
  assert.equal(verifySession(changed + "." + signature, config, now), false);
  assert.equal(verifySession(token, { ...config, passwordHash: "rotated" }, now), false);
  assert.equal(verifySession(token, { ...config, username: "rotated" }, now), false);
  assert.equal(verifySession(token, { ...config, sessionSecret: randomBytes(32).toString("hex") }, now), false);
  for (const bad of [undefined, "", "x.y", token + ".extra", "a".repeat(2048)])
    assert.equal(verifySession(bad, config, now), false);
});

test("invite tokens contain 32 random bytes and are stored by SHA-256 hash", () => {
  const seen = new Set();
  for (let i = 0; i < 100; i++) {
    const { token, hash } = newInviteToken();
    assert.match(token, /^[A-Za-z0-9_-]{43}$/);
    assert.equal(Buffer.from(token, "base64url").length, 32);
    assert.equal(hash, createHash("sha256").update(token).digest("hex"));
    assert.equal(seen.has(token), false); seen.add(token);
  }
});

test("invite validation normalizes names, defaults to two devices and rejects bad input", () => {
  assert.deepEqual(parseInvite({ instagramUsername: " @Producer.1 ", licenseType: "standard" }),
    { username: "producer.1", licenseType: "standard", maxDevices: 2 });
  for (const patch of [
    { instagramUsername: "" }, { instagramUsername: "a".repeat(31) },
    { instagramUsername: "<script>" }, { instagramUsername: "a b" },
    { licenseType: "admin" }, { maxDevices: 0 }, { maxDevices: 21 },
    { maxDevices: 1.5 }, { maxDevices: "2" }, { maxDevices: null },
  ]) assert.throws(() => parseInvite({ instagramUsername: "producer", licenseType: "beta", ...patch }));
  assert.throws(() => parseInvite(null));
});

test("mutations require the exact configured origin, including login", () => {
  checkOrigin(new Request(config.origin, { headers: { origin: config.origin } }), config);
  for (const origin of [null, "null", "https://evil.example", "https://admin.example.evil.example"])
    assert.throws(() => checkOrigin(new Request(config.origin, { headers: origin ? { origin } : {} }), config),
      (error) => error.status === 403);
});

test("JSON input rejects wrong types, malformed JSON and oversized streaming bodies", async () => {
  const request = (body, type = "application/json") => new Request(config.origin, {
    method: "POST", headers: { "Content-Type": type }, body,
  });
  assert.deepEqual(await readJson(request('{"value":2}')), { value: 2 });
  await assert.rejects(readJson(request("{}","text/plain")), (e) => e.status === 415);
  await assert.rejects(readJson(request("{")), (e) => e.status === 400);
  await assert.rejects(readJson(request('{"x":"' + "x".repeat(3000) + '"}')), (e) => e.status === 413);
});

test("unexpected errors do not expose exception text or credentials", async () => {
  const response = failure(new Error("postgresql://SECRET and raw-invite-token"));
  assert.equal(response.status, 503);
  const body = await response.text();
  assert.equal(body.includes("SECRET"), false);
  assert.match(response.headers.get("cache-control"), /no-store/);
});

