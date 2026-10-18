import { test } from "node:test";
import assert from "node:assert/strict";
import { generateKeyPairSync, verify } from "node:crypto";
import { authorization, signingKey } from "../lib/authorization.mjs";
test("offline authorization is device-bound Ed25519 signed payload; tampering fails", () => {
 const {privateKey,publicKey}=generateKeyPairSync("ed25519");
 const key=signingKey({LICENSE_SIGNING_PRIVATE_KEY:privateKey.export({format:"der",type:"pkcs8"}).toString("base64")});
 const token=authorization({licenseId:"test",instagram:"@tester",licenseType:"standard"},"opaque-device",key);
 const [p,s]=token.split(".").map(x=>Buffer.from(x,"hex"));
 assert(verify(null,p,publicKey,s));
 const data=JSON.parse(p); assert.equal(data.deviceId,"opaque-device"); assert.equal(data.version,1); assert(data.issuedAt>0);
 assert(!token.includes("ICEPUNK-"));
 p[0]^=1; assert(!verify(null,p,publicKey,s)); p[0]^=1; s[0]^=1; assert(!verify(null,p,publicKey,s));
});
test("missing or non Ed25519 signer fails closed",()=>{
 assert.throws(()=>signingKey({}));
 const {privateKey}=generateKeyPairSync("ec",{namedCurve:"prime256v1"});
 assert.throws(()=>signingKey({LICENSE_SIGNING_PRIVATE_KEY:privateKey.export({format:"der",type:"pkcs8"}).toString("base64")}));
});
