import { getConfig } from "../../../lib/config.mjs";
import { database } from "../../../lib/db.mjs";
import { testerPortalState } from "../../../lib/tester-portal.mjs";
import ClaimForm, { Portal } from "./ClaimForm";
export const dynamic = "force-dynamic";
export const runtime = "nodejs";
export const metadata = { title: "Tester Access · ICEPUNK" };
export default async function ClaimPage({ params }) {
  const { token } = await params; let portal;
  try { const config = getConfig(); portal = await testerPortalState(database(config.databaseUrl), token); }
  catch { return <section className="card login"><p className="eyebrow">ICEPUNK</p><h1>Temporarily unavailable</h1><p role="alert">We cannot check this tester link right now. Please try again later.</p></section>; }
  if (portal.state === "invalid") return <section className="card login"><p className="eyebrow">ICEPUNK / TESTER ACCESS</p><h1>Invalid tester link</h1><p>Please contact the seller for help.</p></section>;
  if (portal.state === "expired") return <section className="card login"><p className="eyebrow">ICEPUNK / TESTER ACCESS</p><h1>Invite expired</h1><p>Please contact the seller for help with your invite.</p></section>;
  return <section className="card login"><p className="eyebrow">ICEPUNK MIDI GENERATOR — TESTER ACCESS</p>{portal.state === "valid" ? <><h1>Invite for @{portal.username}</h1><p className="muted">Enter your email to claim your personal tester access.</p><ClaimForm token={token} portal={portal} /></> : <Portal token={token} portal={portal} />}</section>;
}