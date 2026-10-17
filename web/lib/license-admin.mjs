import "server-only";
import { HttpError } from "./http.mjs";

export async function manageLicense(query, input) {
  if (!input || typeof input.licenseId !== "string"
      || !/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i.test(input.licenseId)
      || !["reset", "revoke", "reactivate"].includes(input.action))
    throw new HttpError(400, "Invalid license action.");
  const [row] = await query("SELECT public.manage_license($1::uuid, $2) AS found", [input.licenseId, input.action]);
  if (!row.found) throw new HttpError(404, "License not found.");
  return { ok: true };
}

