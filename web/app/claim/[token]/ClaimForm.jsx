"use client";
import { useState } from "react";

function BuildCard({ build }) {
  if (!build) return <section className="result"><h2>Latest Windows Build</h2><p>No tester build is currently available.</p></section>;
  return <section className="result"><h2>Latest Windows Build</h2><p><strong>iCEPUNK MIDI Generator {build.version}</strong></p>
    {build.release_notes && <p className="muted">{build.release_notes}</p>}
    <a className="button" href={build.download_url}>Download for Windows</a>
    {build.file_name && <p className="muted">File: {build.file_name}</p>}
    {build.sha256 && <p className="muted hash">SHA-256: {build.sha256}</p>}
  </section>;
}

export function FeedbackForm({ token, build }) {
  const [busy, setBusy] = useState(false); const [message, setMessage] = useState(""); const [error, setError] = useState("");
  async function submit(event) {
    event.preventDefault(); if (busy) return; setBusy(true); setError("");
    const form = event.currentTarget; const data = new FormData(form);
    try { const response = await fetch("/api/tester/feedback", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ token, category: data.get("category"), title: data.get("title"), appVersion: data.get("appVersion"), message: data.get("message") }) });
      const result = await response.json(); if (!response.ok) { setError(result.error || "Unable to send feedback."); return; }
      form.reset(); setMessage("Thanks — feedback received.");
    } catch { setError("Connection interrupted. Please try again."); } finally { setBusy(false); }
  }
  return <section className="result"><h2>Send feedback</h2><form onSubmit={submit}>
    <div className="fields"><label>Category<select name="category" defaultValue="feedback"><option value="bug">Bug</option><option value="idea">Idea</option><option value="feedback">Feedback</option></select></label>
    <label>App version<input name="appVersion" maxLength={64} defaultValue={build?.version || ""} /></label></div>
    <label>Title <span className="muted">(optional)</span><input name="title" maxLength={120} /></label>
    <label>Message<textarea name="message" maxLength={5000} required /></label>
    {error && <p className="error" role="alert">{error}</p>}{message && <p role="status">{message}</p>}
    <button disabled={busy}>{busy ? "Sending…" : "Send feedback"}</button>
  </form></section>;
}

function Portal({ portal, token, oneTimeKey = "" }) {
  return <><section className="result"><h2>{oneTimeKey ? "License claimed" : "Access already claimed"}</h2>
    {oneTimeKey ? <><p>Save your key now. This is the only time it will be shown.</p><label>Your license key<input aria-label="Your license key" value={oneTimeKey} readOnly onFocus={(event) => event.target.select()} /></label></> : <p>Your personal tester access is active.</p>}
    <p><strong>@{portal.username}</strong><br />{portal.email}<br />{portal.licenseType} license</p>
  </section><BuildCard build={portal.build} /><FeedbackForm token={token} build={portal.build} /></>;
}

export default function ClaimForm({ token, portal }) {
  const [busy, setBusy] = useState(false); const [error, setError] = useState(""); const [claimed, setClaimed] = useState(null);
  async function submit(event) {
    event.preventDefault(); if (busy || claimed) return; setBusy(true); setError(""); const form = event.currentTarget; const email = new FormData(form).get("email");
    try { const response = await fetch("/api/claim", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ token, email }) }); const result = await response.json();
      if (!response.ok) { setError(result.error || "Unable to claim this invite."); return; }
      setClaimed({ username: portal.username, email, licenseType: portal.licenseType, build: result.build, key: result.licenseKey });
    } catch { setError("Connection interrupted. Reload the page before trying again."); } finally { setBusy(false); }
  }
  if (claimed) return <Portal portal={claimed} token={token} oneTimeKey={claimed.key} />;
  return <form onSubmit={submit}><label>Email<input name="email" type="email" autoComplete="email" maxLength={254} required disabled={busy} /></label>
    <p className="muted">Your key will be shown once. Save it before closing this page. No email will be sent.</p>{error && <p role="alert" className="error">{error}</p>}<button disabled={busy}>{busy ? "Claiming…" : "Claim Access"}</button></form>;
}
export { Portal, BuildCard };