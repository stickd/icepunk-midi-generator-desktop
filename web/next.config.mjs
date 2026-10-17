import { fileURLToPath } from "node:url";
/** @type {import('next').NextConfig} */
const nextConfig = {
  turbopack: { root: fileURLToPath(new URL(".", import.meta.url)) },
  poweredByHeader: false,
  // Invite paths contain bearer secrets; suppress Next.js dev access logs.
  logging: { incomingRequests: false },
  async headers() {
    return [{
      source: "/:path*",
      headers: [
        { key: "X-Content-Type-Options", value: "nosniff" },
        { key: "X-Frame-Options", value: "DENY" },
        { key: "Referrer-Policy", value: "no-referrer" },
        { key: "Permissions-Policy", value: "camera=(), microphone=(), geolocation=()" },
        { key: "X-Robots-Tag", value: "noindex, nofollow" },
      ],
    }, {
      source: "/admin/:path*",
      headers: [{ key: "Cache-Control", value: "private, no-store, max-age=0" }],
    }, {
      source: "/claim/:path*",
      headers: [{ key: "Cache-Control", value: "private, no-store, max-age=0" }],
    }];
  },
};
export default nextConfig;

