import { getConfig } from "../../../../lib/config.mjs";
import { database } from "../../../../lib/db.mjs";
import { submitFeedback } from "../../../../lib/tester-portal.mjs";
import { checkOrigin, readJson, json, failure } from "../../../../lib/http.mjs";
export const runtime = "nodejs";
export async function POST(request) {
  try { const config = getConfig(); checkOrigin(request, config); return json(await submitFeedback(database(config.databaseUrl), await readJson(request)), 201); }
  catch (error) { return failure(error); }
}