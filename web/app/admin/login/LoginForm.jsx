"use client";
import { useState } from "react";
export default function LoginForm() {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  async function submit(event) {
    event.preventDefault();
    if (busy) return;
    setBusy(true); setError("");
    const data = new FormData(event.currentTarget);
    try {
      const response = await fetch("/api/admin/login", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ username: data.get("username"), password: data.get("password") }),
      });
      const result = await response.json();
      if (!response.ok) { setError(result.error || "Unable to sign in."); return; }
      window.location.assign("/admin");
    } catch { setError("Unable to connect. Try again."); }
    finally { setBusy(false); }
  }
  return <form onSubmit={submit}>
    <label>Username<input name="username" autoComplete="username" maxLength={100} required /></label>
    <label>Password<input name="password" type="password" autoComplete="current-password" maxLength={1024} required /></label>
    {error && <p role="alert" className="error">{error}</p>}
    <button disabled={busy}>{busy ? "Signing in…" : "Sign in"}</button>
  </form>;
}

