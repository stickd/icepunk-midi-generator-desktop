import { redirect } from "next/navigation";
import { isAdmin } from "../../../lib/auth.mjs";
import { getConfig } from "../../../lib/config.mjs";
import LoginForm from "./LoginForm";

export const dynamic = "force-dynamic";
export default async function LoginPage() {
  let configured = true;
  let signedIn = false;
  try { signedIn = await isAdmin(getConfig()); } catch { configured = false; }
  if (signedIn) redirect("/admin");
  return <section className="card login">
    <p className="eyebrow">ICEPUNK / PRIVATE ADMIN</p>
    <h1>Welcome back.</h1>
    <p className="muted">Sign in to manage your invitations.</p>
    {configured ? <LoginForm /> : <p role="alert" className="error">
      Admin is unavailable. Configure the server environment before signing in.
    </p>}
  </section>;
}

