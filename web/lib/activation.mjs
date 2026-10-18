import "server-only";
import { createHash } from "node:crypto";
import { HttpError, json } from "./http.mjs";
import { allowActivation } from "./rate-limit.mjs";

export class ActivationError extends Error {
  constructor(code, status) { super(code); this.code = code; this.status = status; }
}
export function parseActivation(input) {
  if (!input || typeof input !== "object" || Array.isArray(input)
      || typeof input.licenseKey !== "string" || typeof input.deviceId !== "string")
    throw new ActivationError("INVALID_REQUEST", 400);
  const key = input.licenseKey.trim().toUpperCase();
  if (!/^ICEPUNK(?:-[A-F0-9]{4}){8}$/.test(key)
      || !/^[A-Za-z0-9_-]{16,256}$/.test(input.deviceId))
    throw new ActivationError("INVALID_REQUEST", 400);
  return { keyHash: createHash("sha256").update(key).digest("hex"), deviceId: input.deviceId };
}
export async function activateLicense(query, input) {
  const { keyHash, deviceId } = parseActivation(input);
  if (!await allowActivation(query)) throw new ActivationError("RATE_LIMITED", 429);
  const [row] = await query("SELECT public.activate_license($1, $2) AS result", [keyHash, deviceId]);
  const result = row.result;
  if (result.code) {
    const status = { INVALID_LICENSE: 401, LICENSE_REVOKED: 403, DEVICE_LIMIT_REACHED: 409 }[result.code];
    if (!status) throw new Error("Unexpected activation result");
    throw new ActivationError(result.code, status);
  }
  return result;
}
export function activationFailure(error) {
  let code = "SERVICE_UNAVAILABLE", status = 503;
  if (error instanceof ActivationError) { code = error.code; status = error.status; }
  else if (error instanceof HttpError) { code = "INVALID_REQUEST"; status = error.status; }
  const message = {
    INVALID_REQUEST: "Invalid activation request.",
    INVALID_LICENSE: "License could not be activated.",
    LICENSE_REVOKED: "License is inactive.",
    DEVICE_LIMIT_REACHED: "Device limit reached. Contact the seller.",
    RATE_LIMITED: "Too many requests. Try again later.",
    SERVICE_UNAVAILABLE: "Activation is temporarily unavailable.",
  }[code];
  return json({ ok: false, code, error: message }, status,
    status === 429 ? { "Retry-After": "60" } : {});
}

