import "server-only";
import { newInviteToken } from "./crypto.mjs";
import { parseInvite } from "./validation.mjs";

export async function createInvite(query, input, origin) {
  const values = parseInvite(input);
  const { token, hash } = newInviteToken();
  const [row] = await query(
    `INSERT INTO invites (token_hash, instagram_username, license_type, max_devices)
     VALUES ($1, $2, $3, $4)
     RETURNING id, instagram_username, license_type, max_devices, created_at, used_at, expires_at`,
    [hash, values.username, values.licenseType, values.maxDevices],
  );
  return { invite: row, inviteUrl: `${origin}/claim/${token}` };
}

export async function listInvites(query) {
  return query(
    `SELECT i.id, i.instagram_username, i.license_type, i.max_devices,
            i.created_at, i.used_at, i.expires_at,
            l.id AS license_id, l.email, l.status AS license_status, l.key_suffix,
            (SELECT count(*)::int FROM devices d WHERE d.license_id = l.id) AS devices_used,
            COALESCE((SELECT jsonb_agg(jsonb_build_object(
              'label', left(d.device_id, 8) || '…', 'createdAt', d.created_at,
              'lastSeenAt', d.last_seen_at) ORDER BY d.created_at)
              FROM devices d WHERE d.license_id = l.id), '[]'::jsonb) AS devices
     FROM invites i LEFT JOIN licenses l ON l.invite_id = i.id
     ORDER BY i.created_at DESC, i.id DESC LIMIT 20`,
  );
}

