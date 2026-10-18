import { getConfig } from "../../../lib/config.mjs";
import { database } from "../../../lib/db.mjs";
import { claimInvite, ClaimError } from "../../../lib/claim.mjs";
import { activeTesterBuild } from "../../../lib/tester-portal.mjs";
import { checkOrigin, readJson, json, failure } from "../../../lib/http.mjs";
export const runtime = "nodejs";
export async function POST(request) {
  try {
    const config = getConfig(); checkOrigin(request, config);
    const query = database(config.databaseUrl);
    const result = await claimInvite(query, await readJson(request));
    return json({ ...result, build: await activeTesterBuild(query) }, 201);
  } catch (error) {
    if (error instanceof ClaimError) return json({ state: error.state, error: error.message }, error.status);
    return failure(error);
  }
}