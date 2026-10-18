# Stage 5 — Tester portal

A private invite URL now remains the tester's personal portal:

- `https://icepunk-midi-generator-desktop.vercel.app/claim/<secure-token>`
- before claiming, it shows the Instagram invite and email form;
- after claiming, the one-time license key, current Windows download and feedback form appear;
- on later visits, it shows access, build download and feedback, but never the license key again.

## Database migration

Run this once against the production Neon database from `web`:

```powershell
npm run db:migrate
```

Migration `003_tester_portal.sql` adds `tester_builds`, optional feedback titles, and an atomic `set_active_tester_build` function. It does not alter invite, license, device, activation or signing behaviour.

## Publishing the Windows build

Sign in at `/admin`, then use **Latest Tester Build**. Enter the Windows version, a direct **HTTPS** installer URL, and optionally the file name, SHA-256 and release notes. The portal never stores or proxies installers; it links directly to the configured HTTPS host.

Only one Windows build is active. Updating it retains older metadata as inactive history and immediately changes what valid tester links display.

## Feedback

Claimed testers may submit any number of Bug, Idea or Feedback entries. Feedback is linked to their license, keeps the configured app version by default, and appears newest-first in `/admin`. The admin page has lightweight category filters.

## Security notes

The invite token remains a 32-byte random bearer secret held only by the tester link; only its SHA-256 hash is stored. A valid claimed link exposes the tester's own email and non-secret license metadata but never the full license key. Invalid tokens expose nothing useful. Build updates require the existing authenticated admin cookie plus exact-origin CSRF protection. Feedback submissions use the same exact-origin/body-size protections.

No downloads are public through the site, no binary is stored in PostgreSQL, and no download analytics are collected in this stage.