import { generateKeyPairSync } from "node:crypto";
import { writeFileSync } from "node:fs";
const file = process.argv[2];
if (!file) throw new Error("Usage: node scripts/signing-keys.mjs /secure/path/private.env (outside repository)");
const { privateKey, publicKey } = generateKeyPairSync("ed25519");
writeFileSync(file, "LICENSE_SIGNING_PRIVATE_KEY=" + privateKey.export({format:"der",type:"pkcs8"}).toString("base64") + "\n", {flag:"wx",mode:0o600});
console.log("ICEPUNK_LICENSE_PUBLIC_KEY=" + publicKey.export({format:"der",type:"spki"}).subarray(-32).toString("hex"));
