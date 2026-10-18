import { readFile, readdir } from "node:fs/promises";
import { neon } from "@neondatabase/serverless";
import { applyMigration } from "../lib/migrations.mjs";

try {
  if (!process.env.DATABASE_URL) throw new Error("DATABASE_URL is required");
  const sql = neon(process.env.DATABASE_URL);
  const names = (await readdir(new URL("../db/", import.meta.url))).filter(name => /^\d+_.*\.sql$/.test(name)).sort();
  for (const name of names) {
    const source = await readFile(new URL("../db/" + name, import.meta.url), "utf8");
    const applied = await applyMigration({
      query: (text, parameters = []) => sql.query(text, parameters),
      transaction: (statements) => sql.transaction(statements.map(({ text, parameters }) => sql.query(text, parameters))),
    }, source, name);
    console.log(applied ? "Applied " + name : name + " is already applied; no changes.");
  }
} catch {
  console.error("Migration failed. Check DATABASE_URL, database access and migration history. No credentials are logged.");
  process.exitCode = 1;
}

