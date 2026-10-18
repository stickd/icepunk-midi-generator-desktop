import { createHash } from "node:crypto";
export const ledgerSql = `CREATE TABLE IF NOT EXISTS schema_migrations (
  name text PRIMARY KEY, checksum text NOT NULL, applied_at timestamptz NOT NULL DEFAULT now()
)`;
export function migrationStatements(source) {
  // Procedural migrations explicitly separate top-level statements so function
  // bodies (which contain semicolons) are passed to PostgreSQL intact.
  const separator = source.includes("-- statement-breakpoint") ? "-- statement-breakpoint" : ";";
  return source.split(separator).map((part) => part.trim()).filter(Boolean);
}
export function checksum(source) {
  return createHash("sha256").update(source.replace(/\r\n/g, "\n")).digest("hex");
}

export async function applyMigration(db, source, name = "001_initial.sql") {
  const digest = checksum(source);
  await db.query(ledgerSql);
  const rows = await db.query("SELECT checksum FROM schema_migrations WHERE name = $1", [name]);
  if (rows.length) {
    if (rows[0].checksum !== digest) throw new Error("Applied migration changed");
    return false;
  }
  await db.transaction([
    ...migrationStatements(source).map((text) => ({ text, parameters: [] })),
    { text: "INSERT INTO schema_migrations (name, checksum) VALUES ($1, $2)", parameters: [name, digest] },
  ]);
  return true;
}

