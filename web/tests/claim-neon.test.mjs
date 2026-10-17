import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { database } from "../lib/db.mjs";
import { createInvite } from "../lib/invites.mjs";
import { claimInvite } from "../lib/claim.mjs";

// Explicit opt-in only. Never fall back to the application's DATABASE_URL.
// Use a migrated, dedicated test Neon branch; each HTTP query is independent.
test("Neon: simultaneous claims on independent connections issue one license", {
  skip: !process.env.TEST_DATABASE_URL && "Set TEST_DATABASE_URL to a migrated test Neon branch for real multi-connection verification.",
  timeout: 60000,
}, async () => {
  const query = database(process.env.TEST_DATABASE_URL);
  const { invite, inviteUrl } = await createInvite(query, {
    instagramUsername: "stage2_concurrency_test", licenseType: "beta", maxDevices: 2,
  }, "https://test.invalid");
  try {
    const token = new URL(inviteUrl).pathname.split("/").pop();
    const results = await Promise.allSettled(Array.from({ length: 12 }, (_, i) =>
      claimInvite(database(process.env.TEST_DATABASE_URL), { token, email: "test" + i + "@example.com" })));
    const winners = results.filter((r) => r.status === "fulfilled");
    assert.equal(winners.length, 1);
    const losers = results.filter((r) => r.status === "rejected");
    assert.equal(losers.length, 11);
    assert.ok(losers.every((r) => r.reason.status === 409));
    const rows = await query("SELECT key_hash FROM licenses WHERE invite_id = $1", [invite.id]);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].key_hash, createHash("sha256").update(winners[0].value.licenseKey).digest("hex"));
  } finally {
    // Only remove this test's own fixture, never truncate shared tables.
    await query("DELETE FROM licenses WHERE invite_id = $1", [invite.id]);
    await query("DELETE FROM invites WHERE id = $1", [invite.id]);
  }
});

