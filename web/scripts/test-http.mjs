import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { createServer } from "node:net";
import { randomBytes } from "node:crypto";
import { fileURLToPath } from "node:url";
import { hashPassword, createSession } from "../lib/crypto.mjs";

// Exercise the actual production Next.js routes. No real DB or secrets are used.
const reservation = createServer();
reservation.listen(0, "127.0.0.1");
await once(reservation, "listening");
const port = reservation.address().port;
await new Promise((resolve) => reservation.close(resolve));
const base = `http://127.0.0.1:${port}`;
const origin = "https://admin.example";
const config = {
  username: "http-test-admin",
  passwordHash: await hashPassword(randomBytes(24).toString("hex")),
  sessionSecret: randomBytes(32).toString("hex"),
};
const child = spawn(process.execPath, [
  fileURLToPath(new URL("../node_modules/next/dist/bin/next", import.meta.url)),
  "start", "--hostname", "127.0.0.1", "--port", String(port),
], {
  cwd: fileURLToPath(new URL("..", import.meta.url)),
  env: {
    ...process.env, NODE_ENV: "production", APP_ORIGIN: origin,
    DATABASE_URL: "postgresql://test:test@127.0.0.1:1/test",
    ADMIN_USERNAME: config.username, ADMIN_PASSWORD_HASH: config.passwordHash,
    ADMIN_SESSION_SECRET: config.sessionSecret, NODE_OPTIONS: "",
  },
  windowsHide: true, stdio: ["ignore", "pipe", "pipe"],
});
let output = "";
child.stdout.on("data", (chunk) => { output = (output + chunk).slice(-4000); });
child.stderr.on("data", (chunk) => { output = (output + chunk).slice(-4000); });
const closed = once(child, "exit");
const request = (path, options = {}) => fetch(base + path, {
  redirect: "manual", signal: AbortSignal.timeout(10000), ...options,
});
const cookie = "__Host-icepunk-admin=" + createSession(config);
const headers = { origin, "content-type": "application/json" };
try {
  let ready = false;
  for (let i = 0; i < 100; i++) {
    if (child.exitCode !== null) throw new Error("Next.js exited during startup: " + output);
    try { if ((await request("/admin/login")).status === 200) { ready = true; break; } } catch {}
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  assert.ok(ready, "Production server did not start: " + output);
  const anonymous = await request("/admin");
  assert.equal(anonymous.status, 307);
  assert.equal(anonymous.headers.get("location"), "/admin/login");
  assert.equal((await anonymous.text()).includes("Recent invitations"), false);
  const loginPage = await request("/admin/login");
  assert.equal(loginPage.status, 200);
  assert.match(await loginPage.text(), /Sign in to manage/);
  assert.match(loginPage.headers.get("cache-control"), /no-store/);
  assert.equal(loginPage.headers.get("x-frame-options"), "DENY");

  for (const route of ["/api/admin/login", "/api/admin/invites", "/api/admin/logout", "/api/admin/licenses", "/api/claim"]) {
    for (const suppliedOrigin of [undefined, "https://evil.example"]) {
      const response = await request(route, {
        method: "POST", headers: suppliedOrigin ? { origin: suppliedOrigin } : {},
      });
      assert.equal(response.status, 403, route + " must reject cross-origin/missing Origin");
    }
  }
  const noAuth = await request("/api/admin/invites", { method: "POST", headers, body: "{}" });
  assert.equal(noAuth.status, 401);
  assert.match(noAuth.headers.get("cache-control"), /no-store/);
  const tampered = await request("/api/admin/invites", {
    method: "POST", headers: { ...headers, cookie: cookie + "tampered" }, body: "{}",
  });
  assert.equal(tampered.status, 401);
  const expiredCookie = "__Host-icepunk-admin=" + createSession(config, Date.now() - 9 * 60 * 60 * 1000);
  const expired = await request("/api/admin/invites", {
    method: "POST", headers: { ...headers, cookie: expiredCookie }, body: "{}",
  });
  assert.equal(expired.status, 401);
  const invalidInvite = await request("/api/admin/invites", {
    method: "POST", headers: { ...headers, cookie }, body: '{"instagramUsername":"<script>"}',
  });
  assert.equal(invalidInvite.status, 400);
  const wrongType = await request("/api/admin/invites", {
    method: "POST", headers: { origin, cookie, "content-type": "text/plain" }, body: "{}",
  });
  assert.equal(wrongType.status, 415);
  const tooLarge = await request("/api/admin/invites", {
    method: "POST", headers: { ...headers, cookie }, body: JSON.stringify({ x: "a".repeat(3000) }),
  });
  assert.equal(tooLarge.status, 413);
  const logout = await request("/api/admin/logout", { method: "POST", headers: { origin, cookie } });
  assert.equal(logout.status, 200);
  const cleared = logout.headers.get("set-cookie");
  for (const expected of [/__Host-icepunk-admin=/, /Max-Age=0/i, /HttpOnly/i, /Secure/i, /SameSite=strict/i, /Path=\//])
    assert.match(cleared, expected);
  assert.equal((await request("/api/admin/invites")).status, 405);
  for (const action of ["reset", "revoke", "reactivate"]) {
    const denied = await request("/api/admin/licenses", {
      method: "POST", headers, body: JSON.stringify({ licenseId: "00000000-0000-0000-0000-000000000000", action }),
    });
    assert.equal(denied.status, 401);
  }
  for (const originHeaders of [{}, { origin }]) {
    const malformed = await request("/api/license/activate", {
      method: "POST", headers: { ...originHeaders, "content-type": "application/json" }, body: "{}",
    });
    assert.equal(malformed.status, 400);
    assert.equal((await malformed.json()).code, "INVALID_REQUEST");
    assert.match(malformed.headers.get("cache-control"), /no-store/);
  }
  const wrongOrigin = await request("/api/license/activate", {
    method: "POST", headers: { origin: "https://evil.example", "content-type": "application/json" }, body: "{}",
  });
  assert.equal(wrongOrigin.status, 403);
  assert.equal((await request("/api/license/activate")).status, 405);
  const invalidPage = await request("/claim/invalid-token");
  assert.equal(invalidPage.status, 200);
  assert.match(await invalidPage.text(), /This invite is invalid/);
  assert.match(invalidPage.headers.get("cache-control"), /no-store/);
  assert.equal(invalidPage.headers.get("referrer-policy"), "no-referrer");
  const invalidClaim = await request("/api/claim", {
    method: "POST", headers, body: JSON.stringify({ token: "invalid", email: "buyer@example.com" }),
  });
  assert.equal(invalidClaim.status, 404);
  assert.deepEqual(await invalidClaim.json(), { state: "invalid", error: "This invite is invalid." });
  assert.match(invalidClaim.headers.get("cache-control"), /no-store/);
  const invalidEmail = await request("/api/claim", {
    method: "POST", headers, body: JSON.stringify({ token: "A".repeat(43), email: "not-an-email" }),
  });
  assert.equal(invalidEmail.status, 400);
  assert.equal((await request("/api/claim")).status, 405);
  // Neither ordinary nor RSC-prefetch requests may expose admin license metadata.
  const privateRsc = await request("/admin", { headers: { RSC: "1", "Next-Router-Prefetch": "1" } });
  assert.equal(privateRsc.status, 307);
  assert.equal((await privateRsc.text()).includes("License status"), false);
  console.log("PASS: production HTTP admin/claim security, protected license actions and activation validation/origin policy.");
} finally {
  child.kill();
  await closed;
}

