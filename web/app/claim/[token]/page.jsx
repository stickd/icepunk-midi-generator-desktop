import { getConfig } from "../../../lib/config.mjs";
import { database } from "../../../lib/db.mjs";
import { inviteState, CLAIM_MESSAGES } from "../../../lib/claim.mjs";
import ClaimForm from "./ClaimForm";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";
export const metadata = { title: "Claim your license · ICEPUNK" };

export default async function ClaimPage({ params }) {
  const { token } = await params;
  let invite;
  try {
    const config = getConfig();
    invite = await inviteState(database(config.databaseUrl), token);
  } catch {
    return <section className="card login">
      <p className="eyebrow">ICEPUNK</p><h1>Temporarily unavailable</h1>
      <p role="alert">We cannot check this invite right now. Please try again later.</p>
    </section>;
  }
  return <section className="card login">
    <p className="eyebrow">ICEPUNK / YOUR LICENSE</p>
    {invite.state === "valid" ? <>
      <h1>Invite for @{invite.username}</h1>
      <p className="muted">Enter your email to claim your personal license.</p>
      <ClaimForm token={token} />
    </> : <>
      <h1>{invite.state === "used" ? "Already claimed" : invite.state === "expired" ? "Invite expired" : "Invalid invite"}</h1>
      <p role="status">{CLAIM_MESSAGES[invite.state]}</p>
      {invite.state === "used"
        ? <p className="muted">The license key was shown once when claimed and cannot be displayed again. If you lost it, contact the seller.</p>
        : <p className="muted">Please contact the seller for help with your invite.</p>}
    </>}
  </section>;
}

