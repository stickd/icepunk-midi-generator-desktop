import "server-only";
import { allowLogin } from "./rate-limit.mjs";
import { equalText, verifyPassword, createSession } from "./crypto.mjs";
import { HttpError } from "./http.mjs";

export async function login(query, input, config) {
  if (!input || typeof input.username !== "string" || input.username.length > 100
      || typeof input.password !== "string" || input.password.length > 1024)
    throw new HttpError(400, "Enter your admin username and password.");
  if (!await allowLogin(query))
    throw new HttpError(429, "Too many login attempts. Try again in 15 minutes.");
  // Always verify the password, even when the username is wrong.
  const passwordMatches = await verifyPassword(input.password, config.passwordHash);
  if (!equalText(input.username, config.username) || !passwordMatches)
    throw new HttpError(401, "Invalid username or password.");
  return createSession(config);
}

