import { NextResponse } from "next/server";
import { getConfig } from "../../../../lib/config.mjs";
import { cookieName, isAdmin, sessionCookieOptions } from "../../../../lib/auth.mjs";
import { checkOrigin, HttpError, failure } from "../../../../lib/http.mjs";

export const runtime = "nodejs";
export async function POST(request) {
  try {
    const config = getConfig();
    checkOrigin(request, config);
    if (!await isAdmin(config)) throw new HttpError(401, "Sign in again.");
    const response = NextResponse.json({ ok: true }, { headers: { "Cache-Control": "private, no-store" } });
    response.cookies.set(cookieName(config), "", { ...sessionCookieOptions(config), maxAge: 0 });
    return response;
  } catch (error) { return failure(error); }
}

