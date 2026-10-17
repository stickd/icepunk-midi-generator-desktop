import "server-only";
import { neon } from "@neondatabase/serverless";

export function database(url = process.env.DATABASE_URL) {
  if (!url) throw new Error("DATABASE_URL is required");
  const sql = neon(url);
  return (text, parameters = []) => sql.query(text, parameters, {
    fetchOptions: { signal: AbortSignal.timeout(10000) },
  });
}

