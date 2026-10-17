import "server-only";
import { cookies } from "next/headers";
import { getConfig } from "./config.mjs";
import { verifySession } from "./crypto.mjs";

export function cookieName(config) {
  return config.secure ? "__Host-icepunk-admin" : "icepunk-admin";
}
export async function isAdmin(config = getConfig()) {
  const store = await cookies();
  return verifySession(store.get(cookieName(config))?.value, config);
}
export function sessionCookieOptions(config) {
  return { httpOnly: true, secure: config.secure, sameSite: "strict", path: "/" };
}

