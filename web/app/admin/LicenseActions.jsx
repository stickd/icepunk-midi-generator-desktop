"use client";
import { useState } from "react";
import { useRouter } from "next/navigation";

export default function LicenseActions({ licenseId, status }) {
  const router = useRouter();
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");
  async function act(action) {
    const question = action === "reset"
      ? "Remove all registered devices? The license status will stay unchanged."
      : action === "revoke" ? "Revoke this license and block future activations?" : "Reactivate this license?";
    if (!window.confirm(question)) return;
    setBusy(true); setMessage("");
    try {
      const response = await fetch("/api/admin/licenses", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ licenseId, action }),
      });
      const result = await response.json();
      if (!response.ok) { setMessage(result.error || "Action failed."); return; }
      setMessage("Updated."); router.refresh();
    } catch { setMessage("Connection interrupted. Refresh to check the result."); }
    finally { setBusy(false); }
  }
  return <div>
    <button type="button" className="secondary" disabled={busy} onClick={() => act("reset")}>Reset Devices</button>{" "}
    <button type="button" className="secondary" disabled={busy}
      onClick={() => act(status === "active" ? "revoke" : "reactivate")}>
      {status === "active" ? "Revoke" : "Reactivate"}
    </button>
    {message && <p role="status">{message}</p>}
  </div>;
}

