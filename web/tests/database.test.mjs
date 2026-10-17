import { test, before, after, beforeEach } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { randomBytes, createHash } from "node:crypto";
import { PGlite } from "@electric-sql/pglite";
import { applyMigration } from "../lib/migrations.mjs";
import { createInvite, listInvites } from "../lib/invites.mjs";
import { allowLogin } from "../lib/rate-limit.mjs";
import { login } from "../lib/login.mjs";
import { hashPassword, verifySession } from "../lib/crypto.mjs";

const source = await readFile(new URL("../db/001_initial.sql", import.meta.url), "utf8");
const pg = new PGlite();
const query = async (text, parameters = []) => (await pg.query(text, parameters)).rows;
const db = {
  query,
  transaction: (statements) => pg.transaction(async (tx) => {
    for (const statement of statements) await tx.query(statement.text, statement.parameters);
  }),
};
const config = {
  username: "admin", passwordHash: await hashPassword("test-password"),
  sessionSecret: randomBytes(32).toString("hex"), origin: "https://admin.example",
};
before(async () => {
  assert.equal(await applyMigration(db, source), true);
  await applyMigration(db, await readFile(new URL("../db/002_activation.sql", import.meta.url), "utf8"), "002_activation.sql");
});
beforeEach(async () => { await pg.exec("TRUNCATE feedback, devices, licenses, invites, request_limits CASCADE"); });
after(async () => { await pg.close(); });

test("real PostgreSQL migration is repeatable and detects edits", async () => {
  assert.equal(await applyMigration(db, source), false);
  assert.equal(await applyMigration(db, source.replace(/\n/g, "\r\n")), false);
  await assert.rejects(applyMigration(db, source + "\n-- changed"));
  const tables = await query("SELECT table_name FROM information_schema.tables WHERE table_schema = 'public'");
  assert.deepEqual(tables.map((r) => r.table_name).sort(),
    ["devices", "feedback", "invites", "licenses", "request_limits", "schema_migrations"]);
});

test("failed migration rolls back schema and ledger together", async () => {
  await assert.rejects(applyMigration(db, "CREATE TABLE rollback_probe (id int); SELECT missing_column;", "bad.sql"));
  assert.equal((await query("SELECT to_regclass('public.rollback_probe') AS name"))[0].name, null);
  assert.equal((await query("SELECT * FROM schema_migrations WHERE name = 'bad.sql'")).length, 0);
});

test("create invite stores only hash, defaults, timestamps and unused state; never issues a license", async () => {
  const result = await createInvite(query, { instagramUsername: "@Producer", licenseType: "standard" }, config.origin);
  const token = new URL(result.inviteUrl).pathname.split("/").pop();
  const [row] = await query("SELECT * FROM invites WHERE id = $1", [result.invite.id]);
  assert.equal(row.token_hash, createHash("sha256").update(token).digest("hex"));
  assert.equal(row.instagram_username, "producer");
  assert.equal(row.max_devices, 2);
  assert.equal(row.used_at, null);
  assert.equal(row.expires_at, null);
  assert.ok(row.created_at);
  assert.equal(JSON.stringify(row).includes(token), false);
  assert.equal((await query("SELECT * FROM licenses")).length, 0);
  const listed = await listInvites(query);
  assert.equal("token_hash" in listed[0], false);
  assert.equal("token_hash" in result.invite, false);
});

test("invalid invite does not reach SQL", async () => {
  let calls = 0;
  await assert.rejects(createInvite(async () => { calls++; }, {
    instagramUsername: "x'; DROP TABLE invites; --", licenseType: "standard",
  }, config.origin));
  assert.equal(calls, 0);
});

test("database enforces unique tokens, device bounds and future expiration consistency", async () => {
  const { invite } = await createInvite(query, { instagramUsername: "p", licenseType: "beta" }, config.origin);
  const [row] = await query("SELECT * FROM invites WHERE id = $1", [invite.id]);
  await assert.rejects(query("INSERT INTO invites (token_hash, instagram_username, license_type) VALUES ($1,'p','beta')", [row.token_hash]));
  await assert.rejects(query("UPDATE invites SET max_devices = 0 WHERE id = $1", [invite.id]));
  await assert.rejects(query("UPDATE invites SET expires_at = created_at - interval '1 day' WHERE id = $1", [invite.id]));
  await query("UPDATE invites SET expires_at = created_at + interval '7 days' WHERE id = $1", [invite.id]);
});

test("future schema enforces invite linkage, one license per invite and unique devices", async () => {
  const { invite } = await createInvite(query, { instagramUsername: "p", licenseType: "standard" }, config.origin);
  const keyHash = "a".repeat(64);
  const values = [invite.id, keyHash];
  const insert = "INSERT INTO licenses (invite_id,email,instagram_username,key_hash,key_suffix,license_type) VALUES ($1,'p@example.com','p',$2,'ABCD','standard') RETURNING id";
  const [license] = await query(insert, values);
  await assert.rejects(query(insert, [invite.id, "b".repeat(64)]));
  await query("INSERT INTO devices (license_id, device_id) VALUES ($1, 'test-device')", [license.id]);
  await assert.rejects(query("INSERT INTO devices (license_id, device_id) VALUES ($1, 'test-device')", [license.id]));
  await assert.rejects(query("INSERT INTO feedback (category,message) VALUES ('invalid','hello')"));
  await query("INSERT INTO feedback (category,message) VALUES ('idea','hello')");
});

test("login throttle is shared, bounded and resets after its window", async () => {
  const results = await Promise.all(Array.from({ length: 25 }, () => allowLogin(query)));
  assert.equal(results.filter(Boolean).length, 20);
  const [row] = await query("SELECT * FROM request_limits");
  assert.equal(row.attempts, 21);
  await query("UPDATE request_limits SET reset_at = now() - interval '1 second'");
  assert.equal(await allowLogin(query), true);
});

test("login rejects wrong credentials and returns a valid session for the owner", async () => {
  for (const input of [{ username: "wrong", password: "test-password" }, { username: "admin", password: "wrong" }])
    await assert.rejects(login(query, input, config), (e) => e.status === 401);
  const token = await login(query, { username: "admin", password: "test-password" }, config);
  assert.equal(verifySession(token, config), true);
  assert.equal((await query("SELECT attempts FROM request_limits"))[0].attempts, 3);
});

test("login fails closed on database failure and throttles before expensive password checking", async () => {
  await assert.rejects(login(async () => { throw new Error("offline"); },
    { username: "admin", password: "test-password" }, config));
  for (let i = 0; i < 20; i++) await allowLogin(query);
  await assert.rejects(login(query, { username: "admin", password: "test-password" }, config), (e) => e.status === 429);
});

