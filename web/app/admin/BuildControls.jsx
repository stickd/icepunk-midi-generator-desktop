"use client";
import { useState } from "react";
import { useRouter } from "next/navigation";
export default function BuildControls({ build }) {
  const router = useRouter(); const [busy, setBusy] = useState(false); const [error, setError] = useState(""); const [done, setDone] = useState("");
  async function submit(event) { event.preventDefault(); if (busy) return; setBusy(true); setError(""); setDone(""); const data = new FormData(event.currentTarget);
    try { const response = await fetch("/api/admin/build", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ version: data.get("version"), platform: "windows", downloadUrl: data.get("downloadUrl"), fileName: data.get("fileName"), sha256: data.get("sha256"), releaseNotes: data.get("releaseNotes") }) }); const result = await response.json(); if (!response.ok) { setError(result.error || "Unable to update build."); return; } setDone("Latest tester build updated."); router.refresh(); }
    catch { setError("Connection interrupted. Try again."); } finally { setBusy(false); }
  }
  return <section className="card"><h2>Latest Tester Build</h2>{build ? <p className="muted">Current: Windows {build.version} · updated {new Date(build.created_at).toISOString().slice(0,16).replace("T", " ")} UTC</p> : <p className="muted">No tester build is configured.</p>}
    <form onSubmit={submit}><div className="fields"><label>Version<input name="version" defaultValue={build?.version || ""} maxLength={64} required /></label><label>Platform<input value="Windows" disabled /></label></div>
      <label>Download URL<input name="downloadUrl" type="url" defaultValue={build?.download_url || ""} maxLength={2048} required placeholder="https://…" /></label>
      <div className="fields"><label>File name <span className="muted">(optional)</span><input name="fileName" defaultValue={build?.file_name || ""} maxLength={255} /></label><label>SHA-256 <span className="muted">(optional)</span><input name="sha256" defaultValue={build?.sha256 || ""} maxLength={64} /></label></div>
      <label>Release notes <span className="muted">(optional)</span><textarea name="releaseNotes" defaultValue={build?.release_notes || ""} maxLength={2000} /></label>
      {error && <p className="error" role="alert">{error}</p>}{done && <p role="status">{done}</p>}<button disabled={busy}>{busy ? "Updating…" : "Update Build"}</button>
    </form></section>;
}