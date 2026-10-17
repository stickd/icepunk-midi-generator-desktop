import { getConfig } from "../../../lib/config.mjs";
import { database } from "../../../lib/db.mjs";
import { claimInvite, ClaimError } from "../../../lib/claim.mjs";
import { checkOrigin, readJson, json, failure } from "../../../lib/http.mjs";

export const runtime = "nodejs";
export async function POST(request) {
  try {
    const config = getConfig();
    checkOrigin(request, config);
    const result = await claimInvite(database(config.databaseUrl), await readJson(request));
    return json(result, 201);
  } catch (error) {
    if (error instanceof ClaimError) return json({ state: error.state, error: error.message }, error.status);
    return failure(error);
  }
}

