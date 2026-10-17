import "server-only";
import { createPrivateKey, sign } from "node:crypto";
export function signingKey(env = process.env) {
  const key = createPrivateKey({ key: Buffer.from(env.LICENSE_SIGNING_PRIVATE_KEY || "", "base64"), format: "der", type: "pkcs8" });
  if (key.asymmetricKeyType !== "ed25519") throw new Error("Invalid signing configuration");
  return key;
}
export function authorization(result, deviceId, key) {
  const payload = Buffer.from(JSON.stringify({ version: 1, product: "icepunk-midi", licenseId: result.licenseId,
    deviceId, instagram: result.instagram, licenseType: result.licenseType, issuedAt: Math.floor(Date.now() / 1000) }));
  return payload.toString("hex") + "." + sign(null, payload, key).toString("hex");
}
