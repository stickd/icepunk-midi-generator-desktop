import "server-only";
import { createHash, randomBytes } from "node:crypto";
import { HttpError } from "./http.mjs";
import { parseEmail } from "./validation.mjs";

export const CLAIM_MESSAGES = {
  invalid: "This invite is invalid.",
  expired: "This invite has expired.",
  used: "This invite has already been claimed.",
};
export class ClaimError extends HttpError {
  constructor(state) {
    super({ invalid: 404, expired: 410, used: 409 }[state], CLAIM_MESSAGES[state]);
    this.state = state;
  }
}
export function validInviteToken(token) {
  return typeof token === "string" && /^[A-Za-z0-9_-]{43}$/.test(token);
}
function tokenHash(token) {
  return createHash("sha256").update(token).digest("hex");
}
export function newLicenseKey() {
  const random = randomBytes(16).toString("hex").toUpperCase();
  const key = "ICEPUNK-" + random.match(/.{4}/g).join("-");
  return { key, hash: createHash("sha256").update(key).digest("hex"), suffix: key.slice(-4) };
}
export async function inviteState(query, token) {
  if (!validInviteToken(token)) return { state: "invalid" };
  const [row] = await query(`
    SELECT instagram_username,
      CASE WHEN used_at IS NOT NULL THEN 'used'
           WHEN expires_at IS NOT NULL AND expires_at <= clock_timestamp() THEN 'expired'
           ELSE 'valid' END AS state
    FROM invites WHERE token_hash = $1
  `, [tokenHash(token)]);
  if (!row) return { state: "invalid" };
  // Public responses never include email, license data or either secret/hash.
  return row.state === "valid"
    ? { state: "valid", username: row.instagram_username }
    : { state: row.state };
}

// A data-modifying CTE is ONE atomic SQL statement, including implicit rollback.
// Lock first, then check the locked row's current state and wall-clock expiry.
// This also rejects an invite that expires while waiting for another lock.
// Only the winner supplies a row to INSERT. UNIQUE licenses(invite_id)
// additionally prevents duplicate issuance.
export const CLAIM_SQL = `
  WITH locked AS MATERIALIZED (
    SELECT id, used_at, expires_at FROM invites WHERE token_hash = $1 FOR UPDATE
  ), claimed AS (
    UPDATE invites i SET used_at = clock_timestamp()
    FROM locked
    WHERE i.id = locked.id AND locked.used_at IS NULL
      AND (locked.expires_at IS NULL OR locked.expires_at > clock_timestamp())
    RETURNING i.id, i.instagram_username, i.license_type, i.max_devices, i.used_at
  )
  INSERT INTO licenses
    (invite_id, email, instagram_username, key_hash, key_suffix,
     license_type, status, max_devices, created_at)
  SELECT id, $2, instagram_username, $3, $4, license_type, 'active', max_devices, used_at
  FROM claimed
  RETURNING id
`;

export async function claimInvite(query, input) {
  if (!input || !validInviteToken(input.token)) throw new ClaimError("invalid");
  const email = parseEmail(input.email);
  const license = newLicenseKey();
  const rows = await query(CLAIM_SQL, [tokenHash(input.token), email, license.hash, license.suffix]);
  if (!rows.length) {
    // A fresh statement sees the winning transaction's committed state.
    const current = await inviteState(query, input.token);
    if (current.state === "valid") throw new HttpError(409, "Invite changed. Reload the page and try again.");
    throw new ClaimError(current.state);
  }
  // The only place the raw key leaves the server, and only after DB success.
  return { state: "success", licenseKey: license.key };
}

