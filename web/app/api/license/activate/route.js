import { signingKey, authorization } from "../../../../lib/authorization.mjs";
import { getConfig } from "../../../../lib/config.mjs";
import { database } from "../../../../lib/db.mjs";
import { activateLicense, activationFailure, parseActivation } from "../../../../lib/activation.mjs";
import { checkOrigin, readJson, json } from "../../../../lib/http.mjs";

export const runtime = "nodejs";
export async function POST(request) {
  try {
    const config = getConfig();
    // Desktop clients do not send Origin. Browser requests still require the
    // configured origin; this endpoint never authenticates via admin cookies.
    if (request.headers.has("origin")) checkOrigin(request, config);
    const input = await readJson(request);
    // Validate input before configuration; resolve the signer before consuming a device slot.
    parseActivation(input);
    const key = signingKey();
    const result = await activateLicense(database(config.databaseUrl), input);
    return json({ ...result, authorization: authorization(result, input.deviceId, key) });
  } catch (error) { return activationFailure(error); }
}

