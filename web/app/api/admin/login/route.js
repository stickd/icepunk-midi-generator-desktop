import { NextResponse } from "next/server";
import { getConfig } from "../../../../lib/config.mjs";
import { database } from "../../../../lib/db.mjs";
import { cookieName, sessionCookieOptions } from "../../../../lib/auth.mjs";
import { SESSION_SECONDS } from "../../../../lib/crypto.mjs";
import { login } from "../../../../lib/login.mjs";
import { checkOrigin, readJson, failure } from "../../../../lib/http.mjs";

export const runtime = "nodejs";
export async function POST(request) {
  try {
    const config = getConfig();
    checkOrigin(request, config);
    const token = await login(database(config.databaseUrl), await readJson(request), config);
    const response = NextResponse.json({ ok: true }, { headers: { "Cache-Control": "private, no-store" } });
    response.cookies.set(cookieName(config), token, { ...sessionCookieOptions(config), maxAge: SESSION_SECONDS });
    return response;
  } catch (error) { return failure(error); }
}

