import { getConfig } from "../../../../lib/config.mjs";
import { database } from "../../../../lib/db.mjs";
import { isAdmin } from "../../../../lib/auth.mjs";
import { setActiveTesterBuild } from "../../../../lib/tester-portal.mjs";
import { checkOrigin, readJson, json, HttpError, failure } from "../../../../lib/http.mjs";
export const runtime = "nodejs";
export async function POST(request) {
  try { const config = getConfig(); checkOrigin(request, config); if (!await isAdmin(config)) throw new HttpError(401, "Sign in again."); return json(await setActiveTesterBuild(database(config.databaseUrl), await readJson(request))); }
  catch (error) { return failure(error); }
}