import "server-only";
import { createHash } from "node:crypto";
import { HttpError } from "./http.mjs";
import { InputError } from "./validation.mjs";
import { validInviteToken } from "./claim.mjs";

const tokenHash = (token) => createHash("sha256").update(token).digest("hex");
export const FEEDBACK_CATEGORIES = ["bug", "idea", "feedback"];

function cleanText(value, limit, field, required = false) {
  if (value === undefined || value === null || value === "") {
    if (required) throw new InputError(`${field} is required.`);
    return null;
  }
  if (typeof value !== "string") throw new InputError(`Invalid ${field.toLowerCase()}.`);
  const text = value.trim();
  if ((required && !text) || text.length > limit) throw new InputError(`${field} must be ${required ? "between 1 and " : "up to "}${limit} characters.`);
  return text || null;
}

export function parseBuild(input) {
  if (!input || typeof input !== "object" || Array.isArray(input)) throw new InputError("Invalid build form.");
  const version = cleanText(input.version, 64, "Version", true);
  const platform = input.platform === "windows" ? "windows" : null;
  if (!platform) throw new InputError("Choose a valid platform.");
  const downloadUrl = cleanText(input.downloadUrl, 2048, "Download URL", true);
  let url; try { url = new URL(downloadUrl); } catch { throw new InputError("Enter a valid HTTPS download URL."); }
  if (url.protocol !== "https:") throw new InputError("Download URL must use HTTPS.");
  const sha256 = cleanText(input.sha256, 64, "SHA-256");
  if (sha256 && !/^[a-fA-F0-9]{64}$/.test(sha256)) throw new InputError("SHA-256 must contain 64 hexadecimal characters.");
  return { version, platform, downloadUrl: url.toString(), fileName: cleanText(input.fileName, 255, "File name"), sha256: sha256?.toUpperCase() || null, releaseNotes: cleanText(input.releaseNotes, 2000, "Release notes") };
}

export function parseFeedback(input) {
  if (!input || typeof input !== "object" || Array.isArray(input)) throw new InputError("Invalid feedback form.");
  if (!FEEDBACK_CATEGORIES.includes(input.category)) throw new InputError("Choose a valid feedback category.");
  return { category: input.category, title: cleanText(input.title, 120, "Title"), appVersion: cleanText(input.appVersion, 64, "App version"), message: cleanText(input.message, 5000, "Message", true) };
}

export async function activeTesterBuild(query) {
  const [build] = await query(`SELECT version, platform, download_url, file_name, sha256, release_notes, created_at FROM tester_builds WHERE active AND platform = 'windows' ORDER BY created_at DESC LIMIT 1`);
  return build || null;
}

export async function testerPortalState(query, token) {
  if (!validInviteToken(token)) return { state: "invalid", build: null };
  const [row] = await query(`
    SELECT i.instagram_username, i.license_type, i.used_at, i.expires_at,
           l.id AS license_id, l.email
    FROM invites i LEFT JOIN licenses l ON l.invite_id = i.id
    WHERE i.token_hash = $1
  `, [tokenHash(token)]);
  const build = await activeTesterBuild(query);
  if (!row) return { state: "invalid", build };
  if (row.used_at && row.license_id) return { state: "claimed", username: row.instagram_username, email: row.email, licenseType: row.license_type, build };
  if (row.expires_at && new Date(row.expires_at) <= new Date()) return { state: "expired", build };
  return { state: "valid", username: row.instagram_username, licenseType: row.license_type, build };
}

export async function submitFeedback(query, input) {
  if (!input || !validInviteToken(input.token)) throw new HttpError(404, "This tester link is invalid.");
  const value = parseFeedback(input);
  const [license] = await query(`SELECT l.id, l.instagram_username FROM invites i JOIN licenses l ON l.invite_id = i.id WHERE i.token_hash = $1 AND i.used_at IS NOT NULL`, [tokenHash(input.token)]);
  if (!license) throw new HttpError(409, "Claim access before sending feedback.");
  await query(`INSERT INTO feedback (license_id, instagram_username, app_version, category, title, message) VALUES ($1, $2, $3, $4, $5, $6)`, [license.id, license.instagram_username, value.appVersion, value.category, value.title, value.message]);
  return { ok: true };
}

export async function setActiveTesterBuild(query, input) {
  const build = parseBuild(input);
  const [row] = await query(`SELECT version, platform, download_url, file_name, sha256, release_notes, created_at FROM set_active_tester_build($1, $2, $3, $4, $5, $6)`, [build.version, build.platform, build.downloadUrl, build.fileName, build.sha256, build.releaseNotes]);
  return row;
}

export async function listFeedback(query, category = "all") {
  const allowed = ["all", ...FEEDBACK_CATEGORIES];
  const filter = allowed.includes(category) ? category : "all";
  return query(`SELECT f.id, f.category, f.title, f.message, f.app_version, f.created_at, l.email, COALESCE(l.instagram_username, f.instagram_username) AS instagram_username FROM feedback f LEFT JOIN licenses l ON l.id = f.license_id WHERE ($1 = 'all' OR f.category = $1) ORDER BY f.created_at DESC LIMIT 100`, [filter]);
}