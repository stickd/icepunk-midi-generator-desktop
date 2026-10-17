import { randomBytes, scryptSync } from "node:crypto";
// Generate a strong random password instead of accepting secrets in command history.
const password = randomBytes(24).toString("base64url");
const salt = randomBytes(16).toString("hex");
const hash = scryptSync(password, salt, 64).toString("hex");
console.log("Save this password in a password manager. It is printed only by this setup command.");
console.log("Admin password: " + password);
console.log("\nPut the following values in .env.local / Vercel environment settings:");
console.log("ADMIN_PASSWORD_HASH=scrypt:" + salt + ":" + hash);
console.log("ADMIN_SESSION_SECRET=" + randomBytes(32).toString("hex"));

