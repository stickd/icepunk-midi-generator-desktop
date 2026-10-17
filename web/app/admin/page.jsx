import { redirect } from "next/navigation";
import { isAdmin } from "../../lib/auth.mjs";
import { getConfig } from "../../lib/config.mjs";
import { database } from "../../lib/db.mjs";
import { listInvites } from "../../lib/invites.mjs";
import AdminControls from "./AdminControls";
import LicenseActions from "./LicenseActions";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";
export default async function AdminPage() {
  let config;
  try { config = getConfig(); } catch { redirect("/admin/login"); }
  if (!await isAdmin(config)) redirect("/admin/login");
  let invites = [];
  let databaseUnavailable = false;
  try { invites = await listInvites(database(config.databaseUrl)); }
  catch { databaseUnavailable = true; }
  return <>
    <header><p className="eyebrow">ICEPUNK / PRIVATE ADMIN</p><h1>Invitations</h1>
      <p className="muted">Create an invitation for someone you have approved.</p></header>
    <AdminControls />
    <section className="card">
      <h2>Recent invitations</h2>
      <p className="muted">Latest 20. Links are shown once when created.</p>
      {databaseUnavailable ? <p role="alert" className="error">
        Database unavailable. Check the connection and run the migration.
      </p> : invites.length === 0 ? <p>No invitations yet.</p> :
        <div className="table-wrap"><table><thead><tr>
          <th>Instagram</th><th>Email</th><th>Type</th><th>Devices</th>
          <th>Invite</th><th>License status</th><th>License</th><th>Claimed (UTC)</th><th>Created (UTC)</th><th>Actions</th>
        </tr></thead><tbody>{invites.map((invite) => <tr key={invite.id}>
          <td>@{invite.instagram_username}</td><td>{invite.email || "—"}</td>
          <td>{invite.license_type}</td><td>{invite.devices_used} / {invite.max_devices}
            {invite.devices.length > 0 && <details><summary>Inspect devices</summary>
              <ul>{invite.devices.map((device, index) => <li key={index}>
                {device.label}<br />Registered: {new Date(device.createdAt).toISOString()}<br />
                Last seen: {new Date(device.lastSeenAt).toISOString()}
              </li>)}</ul>
            </details>}
          </td>
          <td>{invite.used_at ? "Claimed" : invite.expires_at && new Date(invite.expires_at) <= new Date() ? "Expired" : "Unused"}</td>
          <td>{invite.license_status || "—"}</td>
          <td>{invite.key_suffix ? "ICEPUNK-••••-…-" + invite.key_suffix : "—"}</td>
          <td>{invite.used_at ? new Date(invite.used_at).toISOString().slice(0, 16).replace("T", " ") : "—"}</td>
          <td>{new Date(invite.created_at).toISOString().slice(0, 16).replace("T", " ")}</td>
          <td>{invite.license_id && <LicenseActions licenseId={invite.license_id} status={invite.license_status} />}</td>
        </tr>)}</tbody></table></div>}
    </section>
    <p className="muted">Revoking blocks future server activations. Reset removes registrations but keeps the license status.
      No offline tokens are issued or invalidated at this stage.</p>
  </>;
}

