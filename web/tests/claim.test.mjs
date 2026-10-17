import { test, before, after, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { PGlite } from "@electric-sql/pglite";
import { applyMigration } from "../lib/migrations.mjs";
import { createInvite, listInvites } from "../lib/invites.mjs";
import { claimInvite, inviteState, newLicenseKey } from "../lib/claim.mjs";
import { parseEmail } from "../lib/validation.mjs";

const source = await readFile(new URL("../db/001_initial.sql", import.meta.url), "utf8");
const pg = new PGlite();
const query = async (text, values = []) => (await pg.query(text, values)).rows;
before(async () => {
  await applyMigration({
    query,
    transaction: (statements) => pg.transaction(async (tx) => {
      for (const { text, parameters } of statements) await tx.query(text, parameters);
    }),
  }, source);
  await pg.exec(await readFile(new URL("../db/002_activation.sql", import.meta.url), "utf8"));
});
beforeEach(async () => { await pg.exec("TRUNCATE feedback, devices, licenses, invites CASCADE"); });
after(async () => { await pg.close(); });
async function freshInvite() {
  const result = await createInvite(query, {
    instagramUsername: "Buyer.Name", licenseType: "beta", maxDevices: 3,
  }, "https://admin.example");
  return { id: result.invite.id, token: new URL(result.inviteUrl).pathname.split("/").pop() };
}
async function counts() {
  const [row] = await query("SELECT (SELECT count(*)::int FROM licenses) AS licenses, (SELECT count(*)::int FROM invites WHERE used_at IS NOT NULL) AS used");
  return row;
}

test("valid GET is read-only and returns only the invite's username", async () => {
  const { token } = await freshInvite();
  assert.deepEqual(await inviteState(query, token), { state: "valid", username: "buyer.name" });
  assert.deepEqual(await inviteState(query, token), { state: "valid", username: "buyer.name" });
  assert.deepEqual(await counts(), { licenses: 0, used: 0 });
});

test("valid claim copies trusted metadata, normalizes email and saves exactly one license with claimed timestamp", async () => {
  const { id, token } = await freshInvite();
  const result = await claimInvite(query, {
    token, email: " Buyer+music@Example.COM ", instagramUsername: "attacker", licenseType: "standard", maxDevices: 20,
  });
  assert.equal(result.state, "success");
  assert.match(result.licenseKey, /^ICEPUNK(?:-[A-F0-9]{4}){8}$/);
  const [license] = await query("SELECT * FROM licenses");
  const [invite] = await query("SELECT * FROM invites WHERE id = $1", [id]);
  assert.equal(license.invite_id, id);
  assert.equal(license.email, "Buyer+music@example.com");
  assert.equal(license.instagram_username, "buyer.name");
  assert.equal(license.license_type, "beta");
  assert.equal(license.max_devices, 3);
  assert.equal(license.status, "active");
  assert.ok(invite.used_at);
  assert.equal(new Date(license.created_at).getTime(), new Date(invite.used_at).getTime());
  assert.deepEqual(await counts(), { licenses: 1, used: 1 });
  assert.equal(license.key_hash, createHash("sha256").update(result.licenseKey).digest("hex"));
  assert.equal(license.key_suffix, result.licenseKey.slice(-4));
  assert.equal(JSON.stringify({ license, invite }).includes(result.licenseKey), false);
  const list = await listInvites(query);
  assert.equal(list[0].email, license.email);
  assert.equal(list[0].license_status, "active");
  assert.equal(list[0].key_suffix, license.key_suffix);
  assert.equal("key_hash" in list[0], false);
  assert.equal("token_hash" in list[0], false);
});

test("malformed, unknown and modified tokens fail safely without consuming an invite", async () => {
  const { token } = await freshInvite();
  const modified = (token[0] === "A" ? "B" : "A") + token.slice(1);
  for (const value of [undefined, "", "bad", "' OR 1=1 --", "A".repeat(43), modified]) {
    assert.deepEqual(await inviteState(query, value), { state: "invalid" });
    await assert.rejects(claimInvite(query, { token: value, email: "buyer@example.com" }),
      (e) => e.status === 404 && e.state === "invalid" && e.message === "This invite is invalid.");
  }
  assert.deepEqual(await counts(), { licenses: 0, used: 0 });
});

test("expired invite fails both on opening and on submit after a valid page was loaded", async () => {
  const { id, token } = await freshInvite();
  assert.equal((await inviteState(query, token)).state, "valid");
  await query("UPDATE invites SET created_at = now() - interval '2 days', expires_at = now() - interval '1 day' WHERE id = $1", [id]);
  assert.deepEqual(await inviteState(query, token), { state: "expired" });
  await assert.rejects(claimInvite(query, { token, email: "buyer@example.com" }), (e) => e.status === 410);
  assert.deepEqual(await counts(), { licenses: 0, used: 0 });
});

test("used invite never reissues or reveals a key or email", async () => {
  const { token } = await freshInvite();
  await claimInvite(query, { token, email: "buyer@example.com" });
  assert.deepEqual(await inviteState(query, token), { state: "used" });
  for (let i = 0; i < 3; i++)
    await assert.rejects(claimInvite(query, { token, email: "someone-else@example.com" }),
      (e) => e.status === 409 && e.message === "This invite has already been claimed.");
  assert.deepEqual(await counts(), { licenses: 1, used: 1 });
  assert.equal((await query("SELECT email FROM licenses"))[0].email, "buyer@example.com");
});

test("malformed email fails before writes and leaves the invite claimable", async () => {
  const { token } = await freshInvite();
  for (const email of [null, "", "no-at", "a@@b.com", "a@b", "a b@example.com", ".a@example.com",
    "a..b@example.com", "a.@example.com", "a@-example.com", "a@example..com", "a@example.com\nBcc:x@y.com",
    "a".repeat(65) + "@example.com", "a@" + "b".repeat(64) + ".com", "x".repeat(255)]) {
    await assert.rejects(claimInvite(query, { token, email }), /valid email/);
  }
  assert.equal(parseEmail("name+tag@EXAMPLE.com"), "name+tag@example.com");
  assert.deepEqual(await counts(), { licenses: 0, used: 0 });
  assert.equal((await inviteState(query, token)).state, "valid");
});

test("concurrent claim callers produce exactly one license and one returned key", async () => {
  const { token } = await freshInvite();
  // PGlite serializes database access; this verifies simultaneous service calls.
  // The optional Neon test covers genuinely separate PostgreSQL connections.
  const results = await Promise.allSettled(Array.from({ length: 12 }, (_, i) =>
    claimInvite(query, { token, email: "buyer" + i + "@example.com" })));
  assert.equal(results.filter((r) => r.status === "fulfilled").length, 1);
  const failures = results.filter((r) => r.status === "rejected");
  assert.equal(failures.length, 11);
  assert.ok(failures.every((r) => r.reason.status === 409 && r.reason.state === "used"));
  assert.deepEqual(await counts(), { licenses: 1, used: 1 });
});

test("license insertion failure rolls back invite consumption atomically", async () => {
  const { id, token } = await freshInvite();
  // Deliberately inconsistent fixture exercises the UNIQUE(invite_id) backstop.
  await query(`INSERT INTO licenses (invite_id,email,instagram_username,key_hash,key_suffix,license_type)
    VALUES ($1,'existing@example.com','buyer.name',$2,'ABCD','beta')`, [id, "a".repeat(64)]);
  await assert.rejects(claimInvite(query, { token, email: "new@example.com" }));
  assert.equal((await query("SELECT used_at FROM invites WHERE id = $1", [id]))[0].used_at, null);
  assert.deepEqual(await counts(), { licenses: 1, used: 0 });
});

test("keys have 128 random bits, a fixed readable format, hash and masked suffix", () => {
  const keys = new Set();
  for (let i = 0; i < 100; i++) {
    const { key, hash, suffix } = newLicenseKey();
    assert.match(key, /^ICEPUNK(?:-[A-F0-9]{4}){8}$/);
    assert.equal(Buffer.from(key.slice(8).replaceAll("-", ""), "hex").length, 16);
    assert.equal(hash, createHash("sha256").update(key).digest("hex"));
    assert.equal(suffix, key.slice(-4));
    assert.equal(keys.has(key), false); keys.add(key);
  }
});

