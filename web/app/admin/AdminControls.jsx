"use client";
import { useState } from "react";
import { useRouter } from "next/navigation";
import { LICENSE_TYPES } from "../../lib/validation.mjs";

export default function AdminControls() {
  const router = useRouter();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [inviteUrl, setInviteUrl] = useState("");
  const [copied, setCopied] = useState(false);
  async function create(event) {
    event.preventDefault();
    if (busy) return;
    setBusy(true); setError(""); setCopied(false);
    const form = event.currentTarget;
    const data = new FormData(form);
    try {
      const response = await fetch("/api/admin/invites", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          instagramUsername: data.get("instagramUsername"),
          licenseType: data.get("licenseType"), maxDevices: Number(data.get("maxDevices")),
        }),
      });
      const result = await response.json();
      if (!response.ok) { setError(result.error || "Unable to create invite."); return; }
      setInviteUrl(result.inviteUrl); form.reset(); router.refresh();
    } catch { setError("Connection lost. The invite may have been created; check the recent list before retrying."); }
    finally { setBusy(false); }
  }
  async function logout() {
    setBusy(true); setError("");
    try {
      const response = await fetch("/api/admin/logout", { method: "POST" });
      if (!response.ok && response.status !== 401) throw new Error();
      window.location.assign("/admin/login");
    } catch { setError("Could not sign out. Try again."); setBusy(false); }
  }
  async function copy() {
    try { await navigator.clipboard.writeText(inviteUrl); setCopied(true); }
    catch { setError("Select the link and copy it manually."); }
  }
  return <section className="card">
    <div className="row"><h2>Create invite</h2><button className="secondary" type="button" disabled={busy} onClick={logout}>Sign out</button></div>
    <form onSubmit={create}>
      <div className="fields">
        <label>Instagram username<input name="instagramUsername" placeholder="@producer" maxLength={31} autoComplete="off" required /></label>
        <label>License type<select name="licenseType" defaultValue="standard">
          {LICENSE_TYPES.map((type) => <option key={type} value={type}>{type}</option>)}
        </select></label>
        <label>Max devices<input name="maxDevices" type="number" min={1} max={20} step={1} defaultValue={2} required /></label>
      </div>
      <button disabled={busy}>{busy ? "Please wait…" : "Create invite"}</button>
    </form>
    {error && <p role="alert" className="error">{error}</p>}
    {inviteUrl && <div className="result" role="status">
      <label>New invite link<input aria-label="New invite link" value={inviteUrl} readOnly onFocus={(event) => event.target.select()} /></label>
      <button className="secondary" type="button" onClick={copy}>{copied ? "Copied" : "Copy invite link"}</button>
      <p>Save this link now. It cannot be recovered after leaving this page.</p>
    </div>}
    <p className="muted note">Anyone with this link can claim it once. Send it privately to the intended buyer.</p>
  </section>;
}

