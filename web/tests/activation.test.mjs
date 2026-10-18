import { test, before, beforeEach, after } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { PGlite } from "@electric-sql/pglite";
import { applyMigration } from "../lib/migrations.mjs";
import { createInvite, listInvites } from "../lib/invites.mjs";
import { claimInvite } from "../lib/claim.mjs";
import { activateLicense, activationFailure, parseActivation } from "../lib/activation.mjs";
import { manageLicense } from "../lib/license-admin.mjs";
import { allowActivation, allowLogin } from "../lib/rate-limit.mjs";

const pg = new PGlite();
const query = async (text, parameters=[]) => (await pg.query(text,parameters)).rows;
const db = { query, transaction: (statements) => pg.transaction(async tx => {
  for (const s of statements) await tx.query(s.text,s.parameters);
}) };
const migration = await readFile(new URL("../db/002_activation.sql",import.meta.url),"utf8");
before(async () => {
  await applyMigration(db,await readFile(new URL("../db/001_initial.sql",import.meta.url),"utf8"));
  await applyMigration(db,migration,"002_activation.sql");
});
beforeEach(async () => { await pg.exec("TRUNCATE feedback, devices, licenses, invites, request_limits CASCADE"); });
after(async () => { await pg.close(); });
async function fixture() {
  const {invite,inviteUrl} = await createInvite(query,{instagramUsername:"producer",licenseType:"beta"},"https://test.invalid");
  const {licenseKey} = await claimInvite(query,{token:new URL(inviteUrl).pathname.split("/").pop(),email:"buyer@example.com"});
  const [row] = await query("SELECT id FROM licenses WHERE invite_id=$1",[invite.id]);
  return {licenseKey,id:row.id};
}
const device = n => "hashed-device-id-" + n;
const activate = (licenseKey,n) => activateLicense(query,{licenseKey,deviceId:device(n)});

test("activation migration is re-runnable and preserves constraints", async () => {
  assert.equal(await applyMigration(db,migration,"002_activation.sql"),false);
  const cols = await query("SELECT column_name FROM information_schema.columns WHERE table_name='devices'");
  assert.ok(cols.some(c=>c.column_name==="device_id"));
  assert.ok(cols.some(c=>c.column_name==="last_seen_at"));
  assert.ok(!cols.some(c=>c.column_name==="fingerprint"));
});
test("first and second devices succeed, third fails; existing device succeeds at capacity without duplicates",async()=>{
  const {licenseKey,id}=await fixture();
  const first=await activate(licenseKey,1);
  assert.deepEqual(first,{ok:true,licenseId:id,instagram:"@producer",licenseType:"beta",maxDevices:2});
  await activate(licenseKey,2);
  await assert.rejects(activate(licenseKey,3),e=>e.code==="DEVICE_LIMIT_REACHED");
  await query("UPDATE devices SET last_seen_at=created_at - interval '1 day'");
  assert.equal((await activate(licenseKey,1)).ok,true);
  const rows=await query("SELECT * FROM devices WHERE license_id=$1",[id]);
  assert.equal(rows.length,2);
  assert.ok(new Date(rows.find(r=>r.device_id===device(1)).last_seen_at) > new Date(rows[0].created_at) ||
    new Date(rows.find(r=>r.device_id===device(1)).last_seen_at).getTime() === new Date(rows[0].created_at).getTime());
  const admin=(await listInvites(query))[0];
  assert.equal(admin.devices_used,2); assert.equal(admin.devices.length,2);
  assert.equal(JSON.stringify(admin.devices).includes(device(1)),false);
});
test("activation fails closed outside READ COMMITTED", async () => {
  await assert.rejects(pg.transaction(async tx => {
    await tx.query("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ");
    await tx.query("SELECT public.activate_license($1,$2)",["a".repeat(64),device(1)]);
  }), /READ COMMITTED/);
});
test("invalid key fails; input normalization uses the claim hash and database metadata is authoritative",async()=>{
  const {licenseKey}=await fixture();
  await assert.rejects(activate("ICEPUNK-" + Array(8).fill("0000").join("-"),1),e=>e.code==="INVALID_LICENSE");
  const result=await activateLicense(query,{licenseKey:" "+licenseKey.toLowerCase()+" ",deviceId:device(1),maxDevices:99,status:"revoked",instagram:"attacker"});
  assert.equal(result.maxDevices,2); assert.equal(result.instagram,"@producer");
});
test("revoked license rejects even an existing device; reactivation keeps key and registrations",async()=>{
  const {licenseKey,id}=await fixture();
  await activate(licenseKey,1);
  await manageLicense(query,{licenseId:id,action:"revoke"});
  await assert.rejects(activate(licenseKey,1),e=>e.code==="LICENSE_REVOKED");
  await manageLicense(query,{licenseId:id,action:"reactivate"});
  await activate(licenseKey,1);
  assert.equal((await query("SELECT * FROM devices")).length,1);
  assert.equal((await query("SELECT * FROM licenses")).length,1);
});
test("reset deletes registrations, preserves status and allows activation afterward",async()=>{
  const {licenseKey,id}=await fixture();
  await activate(licenseKey,1); await activate(licenseKey,2);
  await manageLicense(query,{licenseId:id,action:"reset"});
  assert.equal((await query("SELECT * FROM devices")).length,0);
  assert.equal((await query("SELECT status FROM licenses"))[0].status,"active");
  await activate(licenseKey,3);
  await manageLicense(query,{licenseId:id,action:"revoke"});
  await manageLicense(query,{licenseId:id,action:"reset"});
  assert.equal((await query("SELECT status FROM licenses"))[0].status,"revoked");
});
test("malformed activation/admin requests fail before queries and errors expose no secrets",async()=>{
  let called=0;
  for(const input of [null,[],{}, {licenseKey:"secret",deviceId:device(1)},
    {licenseKey:"ICEPUNK-"+Array(8).fill("AAAA").join("-"),deviceId:"short"},
    {licenseKey:"ICEPUNK-"+Array(8).fill("AAAA").join("-"),deviceId:"x".repeat(257)},
    {licenseKey:"ICEPUNK-"+Array(8).fill("AAAA").join("-"),deviceId:"space in device ID"}])
    await assert.rejects(activateLicense(async()=>{called++;},input),e=>e.code==="INVALID_REQUEST");
  for(const input of [null,{licenseId:"bad",action:"reset"},{licenseId:"00000000-0000-0000-0000-000000000000",action:"delete"}])
    await assert.rejects(manageLicense(async()=>{called++;},input));
  assert.equal(called,0);
  const response=activationFailure(new Error("secret licenseKey and DB connection"));
  assert.equal(response.status,503); assert.equal((await response.text()).includes("secret"),false);
});
test("activation limiter is bounded, resets and does not consume the admin login bucket",async()=>{
  for(let i=0;i<120;i++) assert.equal(await allowActivation(query),true);
  assert.equal(await allowActivation(query),false);
  assert.equal(await allowLogin(query),true);
  await assert.rejects(activate("ICEPUNK-"+Array(8).fill("AAAA").join("-"),1),e=>e.code==="RATE_LIMITED");
  await query("UPDATE request_limits SET reset_at=clock_timestamp()-interval '1 second' WHERE key='license-activation'");
  assert.equal(await allowActivation(query),true);
});
test("concurrent service calls cannot exceed device capacity",async()=>{
  const {licenseKey,id}=await fixture();
  await activate(licenseKey,0);
  const results=await Promise.allSettled(Array.from({length:8},(_,i)=>activate(licenseKey,i+1)));
  assert.equal(results.filter(r=>r.status==="fulfilled").length,1);
  assert.equal((await query("SELECT * FROM devices WHERE license_id=$1",[id])).length,2);
});

