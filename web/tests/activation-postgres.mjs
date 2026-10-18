import assert from "node:assert/strict";
import { createInvite } from "../lib/invites.mjs";
import { claimInvite } from "../lib/claim.mjs";
import { activateLicense } from "../lib/activation.mjs";
import { manageLicense } from "../lib/license-admin.mjs";

// Requires a migrated dedicated test database and independently connected
// PostgreSQL clients (pg or Neon Client). No in-memory database substitutes.
export async function activationRaces(connect) {
  const clients = await Promise.all(["s3-control","s3-blocker","s3-a","s3-b"].map(connect));
  const [control,blocker,a,b]=clients;
  await Promise.all(clients.map(c=>c.query("SET statement_timeout = '10s'")));
  const q=c=>async(text,params=[]) => (await c.query(text,params)).rows;
  let licenseId,inviteId;
  const activate=(client,key,deviceId)=>activateLicense(q(client),{licenseKey:key,deviceId});
  async function blocked(expected) {
    for(let i=0;i<150;i++){
      const [{n}]=await q(control)("SELECT count(*)::int AS n FROM pg_stat_activity WHERE application_name IN ('s3-a','s3-b') AND wait_event_type='Lock'");
      if(n===expected) return;
      await new Promise(resolve=>setTimeout(resolve,20));
    }
    throw new Error("Expected independent connections blocked on license row");
  }
  try{
    const {invite,inviteUrl}=await createInvite(q(control),{instagramUsername:"activation_race_test",licenseType:"beta"},"https://test.invalid");
    inviteId=invite.id;
    const {licenseKey}=await claimInvite(q(control),{token:new URL(inviteUrl).pathname.split("/").pop(),email:"test@example.com"});
    [{id:licenseId}]=await q(control)("SELECT id FROM licenses WHERE invite_id=$1",[inviteId]);
    await activate(control,licenseKey,"opaque-device-original");
    console.log("PostgreSQL race: competing for last device slot");
    await blocker.query("BEGIN");
    await blocker.query("SELECT id FROM licenses WHERE id=$1 FOR UPDATE",[licenseId]);
    const pending=Promise.allSettled([
      activate(a,licenseKey,"opaque-device-contender-a"),
      activate(b,licenseKey,"opaque-device-contender-b"),
    ]);
    await blocked(2);
    console.log("Both activation connections are blocked");
    await blocker.query("COMMIT");
    const results=await pending;
    assert.equal(results.filter(r=>r.status==="fulfilled").length,1);
    assert.equal(results.filter(r=>r.status==="rejected"&&r.reason.code==="DEVICE_LIMIT_REACHED").length,1);
    assert.equal((await q(control)("SELECT count(*)::int AS n FROM devices WHERE license_id=$1",[licenseId]))[0].n,2);

    // Two requests for the SAME new identifier must not create duplicates.
    await manageLicense(q(control),{licenseId,action:"reset"});
    console.log("PostgreSQL race: identical devices");
    await blocker.query("BEGIN");
    await blocker.query("SELECT id FROM licenses WHERE id=$1 FOR UPDATE",[licenseId]);
    const repeats=Promise.allSettled([
      activate(a,licenseKey,"opaque-device-identical"),
      activate(b,licenseKey,"opaque-device-identical"),
    ]);
    await blocked(2); await blocker.query("COMMIT");
    assert.ok((await repeats).every(r=>r.status==="fulfilled"));
    assert.equal((await q(control)("SELECT count(*)::int AS n FROM devices WHERE license_id=$1",[licenseId]))[0].n,1);

    // Revoke commits while activation waits: it must see revoked, not stale active.
    await blocker.query("BEGIN");
    await manageLicense(q(blocker),{licenseId,action:"revoke"});
    console.log("PostgreSQL race: revocation");
    const revoked=Promise.allSettled([activate(a,licenseKey,"opaque-device-identical")]);
    await blocked(1); await blocker.query("COMMIT");
    assert.equal((await revoked)[0].reason.code,"LICENSE_REVOKED");

    // Reset commits while activation waits: its count must see the removed rows.
    await manageLicense(q(control),{licenseId,action:"reactivate"});
    console.log("PostgreSQL race: reset");
    await activate(control,licenseKey,"opaque-device-second");
    await blocker.query("BEGIN");
    await manageLicense(q(blocker),{licenseId,action:"reset"});
    const reset=Promise.allSettled([activate(a,licenseKey,"opaque-device-after-reset")]);
    await blocked(1); await blocker.query("COMMIT");
    assert.equal((await reset)[0].status,"fulfilled");
    assert.equal((await q(control)("SELECT count(*)::int AS n FROM devices WHERE license_id=$1",[licenseId]))[0].n,1);
  } finally {
    await blocker.query("ROLLBACK").catch(()=>{});
    if(licenseId) {
      await q(control)("DELETE FROM devices WHERE license_id=$1",[licenseId]);
      await q(control)("DELETE FROM licenses WHERE id=$1",[licenseId]);
    }
    if(inviteId) await q(control)("DELETE FROM invites WHERE id=$1",[inviteId]);
    await Promise.allSettled(clients.map(c=>c.end()));
  }
}

