import "server-only";

export function getConfig(env = process.env) {
  const required = ["DATABASE_URL", "APP_ORIGIN", "ADMIN_USERNAME", "ADMIN_PASSWORD_HASH", "ADMIN_SESSION_SECRET"];
  if (required.some((key) => !env[key])) throw new Error("Missing server configuration");
  const origin = new URL(env.APP_ORIGIN);
  if (origin.origin !== env.APP_ORIGIN || !["http:", "https:"].includes(origin.protocol))
    throw new Error("APP_ORIGIN must be an exact HTTP(S) origin");
  if (env.NODE_ENV === "production" && origin.protocol !== "https:")
    throw new Error("Production APP_ORIGIN must use HTTPS");
  if (!/^[a-f0-9]{64}$/i.test(env.ADMIN_SESSION_SECRET))
    throw new Error("ADMIN_SESSION_SECRET must contain 32 random bytes encoded as hex");
  if (!/^scrypt:[a-f0-9]{32}:[a-f0-9]{128}$/.test(env.ADMIN_PASSWORD_HASH))
    throw new Error("Invalid admin password hash");
  if (env.ADMIN_USERNAME.length > 100) throw new Error("Invalid admin username");
  return {
    databaseUrl: env.DATABASE_URL, origin: origin.origin,
    username: env.ADMIN_USERNAME, passwordHash: env.ADMIN_PASSWORD_HASH,
    sessionSecret: env.ADMIN_SESSION_SECRET,
    secure: origin.protocol === "https:",
  };
}

