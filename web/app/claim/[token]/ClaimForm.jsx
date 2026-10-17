"use client";
import { useEffect, useRef, useState } from "react";

export default function ClaimForm({ token }) {
  const submitting = useRef(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [terminal, setTerminal] = useState(false);
  const [licenseKey, setLicenseKey] = useState("");
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    // Back/forward navigation must re-check the invite, not resurrect a form/key.
    const refresh = (event) => { if (event.persisted) window.location.reload(); };
    window.addEventListener("pageshow", refresh);
    return () => window.removeEventListener("pageshow", refresh);
  }, []);

  async function submit(event) {
    event.preventDefault();
    if (submitting.current || terminal || licenseKey) return;
    submitting.current = true; setBusy(true); setError("");
    const email = new FormData(event.currentTarget).get("email");
    try {
      const response = await fetch("/api/claim", {
        method: "POST", cache: "no-store",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ token, email }),
      });
      const result = await response.json();
      if (!response.ok) {
        setTerminal(["invalid", "expired", "used"].includes(result.state));
        setError(result.error || "Unable to claim this invite.");
        return;
      }
      setLicenseKey(result.licenseKey);
    } catch {
      setError("The connection was interrupted. Your claim may have completed. Reload to check the invite; if already claimed and you did not receive a key, contact the seller. Keys cannot be recovered.");
    } finally {
      submitting.current = false; setBusy(false);
    }
  }
  async function copy() {
    try { await navigator.clipboard.writeText(licenseKey); setCopied(true); setError(""); }
    catch { setError("Please select the key and copy it manually."); }
  }
  if (licenseKey) return <div className="result" role="status">
    <h2>License claimed</h2>
    <p>Save your key now. This is the only time it will be shown.</p>
    <label>Your license key<input aria-label="Your license key" value={licenseKey} readOnly
      onFocus={(event) => event.target.select()} /></label>
    <button type="button" onClick={copy}>{copied ? "Copied" : "Copy license key"}</button>
    {error && <p className="error" role="alert">{error}</p>}
  </div>;
  if (terminal) return <div role="status"><p>{error}</p>
    <p className="muted">Contact the seller if you need help. A previously issued key cannot be shown again.</p>
  </div>;
  return <form onSubmit={submit}>
    <label>Email<input name="email" type="email" autoComplete="email" maxLength={254} required disabled={busy} /></label>
    <p className="muted">Your key will be shown once. Save it before closing this page. No email will be sent.</p>
    {error && <p role="alert" className="error">{error}</p>}
    <button disabled={busy}>{busy ? "Claiming…" : "Claim license"}</button>
  </form>;
}

