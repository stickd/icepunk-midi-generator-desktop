import "server-only";
// Separate global bucket; does not alter the admin login policy. A bounded
// row avoids storage growth from arbitrary guessed keys or untrusted IPs.
export async function allowActivation(query) {
  const [row] = await query(`
    INSERT INTO request_limits (key, attempts, reset_at)
    VALUES ('license-activation', 1, clock_timestamp() + interval '1 minute')
    ON CONFLICT (key) DO UPDATE SET
      attempts = CASE WHEN request_limits.reset_at <= clock_timestamp() THEN 1
                      ELSE LEAST(request_limits.attempts + 1, 121) END,
      reset_at = CASE WHEN request_limits.reset_at <= clock_timestamp()
                      THEN clock_timestamp() + interval '1 minute' ELSE request_limits.reset_at END
    RETURNING attempts <= 120 AS allowed
  `);
  return row.allowed;
}
// A single bounded row works across Vercel instances; no IP trust assumptions.
// All login attempts count, including successful ones. Never reset on success.
export async function allowLogin(query) {
  const [row] = await query(`
    INSERT INTO request_limits (key, attempts, reset_at)
    VALUES ('admin-login', 1, now() + interval '15 minutes')
    ON CONFLICT (key) DO UPDATE SET
      attempts = CASE WHEN request_limits.reset_at <= now() THEN 1
                      ELSE LEAST(request_limits.attempts + 1, 21) END,
      reset_at = CASE WHEN request_limits.reset_at <= now()
                      THEN now() + interval '15 minutes' ELSE request_limits.reset_at END
    RETURNING attempts <= 20 AS allowed
  `);
  return row.allowed;
}

