import { redirect } from "next/navigation";
import { isAdmin } from "../../lib/auth.mjs";
import { getConfig } from "../../lib/config.mjs";
import { database } from "../../lib/db.mjs";
import { listInvites } from "../../lib/invites.mjs";
import { activeTesterBuild, listFeedback, FEEDBACK_CATEGORIES } from "../../lib/tester-portal.mjs";
import AdminControls from "./AdminControls";
import BuildControls from "./BuildControls";
import LicenseActions from "./LicenseActions";
export const dynamic = "force-dynamic";
export const runtime = "nodejs";
export default async function AdminPage({ searchParams }) {
  let config; try { config = getConfig(); } catch { redirect("/admin/login"); }
  if (!await isAdmin(config)) redirect("/admin/login");
  const category = (await searchParams)?.category || "all"; let invites = [], build = null, feedback = [], databaseUnavailable = false;
  try { const query = database(config.databaseUrl); [invites, build, feedback] = await Promise.all([listInvites(query), activeTesterBuild(query), listFeedback(query, category)]); } catch { databaseUnavailable = true; }
  return <><header><p className="eyebrow">ICEPUNK / PRIVATE ADMIN</p><h1>Tester portal</h1><p className="muted">Create personal links, publish the latest Windows build and review feedback.</p></header><AdminControls /><BuildControls build={build} />
    <section className="card"><h2>Recent invitations</h2><p className="muted">Latest 20. Links are shown once when created.</p>{databaseUnavailable ? <p role="alert" className="error">Database unavailable. Check the connection and run the migration.</p> : invites.length === 0 ? <p>No invitations yet.</p> : <div className="table-wrap"><table><thead><tr><th>Instagram</th><th>Email</th><th>Type</th><th>Devices</th><th>Invite</th><th>License status</th><th>License</th><th>Claimed (UTC)</th><th>Created (UTC)</th><th>Actions</th></tr></thead><tbody>{invites.map((invite) => <tr key={invite.id}><td>@{invite.instagram_username}</td><td>{invite.email || "—"}</td><td>{invite.license_type}</td><td>{invite.devices_used} / {invite.max_devices}</td><td>{invite.used_at ? "Claimed" : invite.expires_at && new Date(invite.expires_at) <= new Date() ? "Expired" : "Unused"}</td><td>{invite.license_status || "—"}</td><td>{invite.key_suffix ? "ICEPUNK-••••-…-" + invite.key_suffix : "—"}</td><td>{invite.used_at ? new Date(invite.used_at).toISOString().slice(0,16).replace("T", " ") : "—"}</td><td>{new Date(invite.created_at).toISOString().slice(0,16).replace("T", " ")}</td><td>{invite.license_id && <LicenseActions licenseId={invite.license_id} status={invite.license_status} />}</td></tr>)}</tbody></table></div>}</section>
    <section className="card"><div className="row"><h2>Tester feedback</h2><nav className="filters"><a href="/admin">All</a>{FEEDBACK_CATEGORIES.map((item) => <a key={item} href={`/admin?category=${item}`}>{item[0].toUpperCase()+item.slice(1)}s</a>)}</nav></div>{databaseUnavailable ? null : feedback.length === 0 ? <p>No feedback yet.</p> : <div className="table-wrap"><table><thead><tr><th>Tester</th><th>Email</th><th>Category</th><th>Version</th><th>Message</th><th>Created (UTC)</th></tr></thead><tbody>{feedback.map((entry) => <tr key={entry.id}><td>@{entry.instagram_username || "—"}</td><td>{entry.email || "—"}</td><td>{entry.category}</td><td>{entry.app_version || "—"}</td><td>{entry.title && <strong>{entry.title}<br /></strong>}{entry.message}</td><td>{new Date(entry.created_at).toISOString().slice(0,16).replace("T", " ")}</td></tr>)}</tbody></table></div>}</section>
    <p className="muted">Revoking blocks future server activations. Reset removes registrations but keeps the license status. Offline authorizations remain valid by Stage 4 design.</p></>;
}