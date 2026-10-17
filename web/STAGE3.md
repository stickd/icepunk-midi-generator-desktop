# Stage 3: server activation and device management

No JUCE/Python changes, hardware ID generation, signed tokens, local license files, periodic checks,
emails or payments. No new application dependencies or required environment variables.

## Upgrade

From the repository's web folder, using the existing .env.local:
```powershell
npm ci
npm run db:migrate
npm test
npm run build
npm run test:http
npm run dev
```

Apply migrations on a development Neon branch first, then production before deploying the new web code.
001_initial.sql remains unchanged. 002_activation.sql preserves existing devices and uniqueness while renaming
fingerprint to device_id, adds last_seen_at, and installs activate_license/manage_license.
last_seen_at on pre-existing rows starts at migration time.
The migration ledger makes reruns a no-op and rejects edited applied migrations.
The old web code expects the former column name: do not roll it back independently of schema compatibility.

Both functions are SECURITY INVOKER with a fixed search_path. They do not grant buyers database access.
Activation requires READ COMMITTED (the normal PostgreSQL/Neon default) and fails closed at other isolation levels.
Existing Stage 2 keys continue to work.

## Activation API

POST /api/license/activate, Content-Type: application/json:
```json
{
  "licenseKey": "ICEPUNK-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX",
  "deviceId": "opaque-hashed-device-01"
}
```

Supply the actual saved key, not X placeholders. Outer whitespace is trimmed and the key is uppercased
before SHA-256 hashing, matching the canonical Stage 2 format. deviceId is case-sensitive, 16–256 ASCII
letters/digits/underscores/hyphens. It is treated as an already-hashed opaque ID; its provenance is not verified.
The server never generates hardware fingerprints. Extra client metadata is ignored; the database controls
status, Instagram, license type and maxDevices.

First and repeat activation return HTTP 200:
```json
{"ok":true,"licenseId":"UUID","instagram":"@producer","licenseType":"beta","maxDevices":2}
```

No email, hash, full key, device list or signed token is returned. Responses use no-store.
This JSON is NOT an offline license credential.

| HTTP | code | Meaning |
|---|---|---|
| 400 | INVALID_REQUEST | Invalid fields/format |
| 403 | INVALID_REQUEST | Browser origin not allowed |
| 413 / 415 | INVALID_REQUEST | Oversized body / non-JSON body |
| 401 | INVALID_LICENSE | Well-formed key does not match |
| 403 | LICENSE_REVOKED | Inactive license |
| 409 | DEVICE_LIMIT_REACHED | New ID exceeds allowance |
| 429 | RATE_LIMITED | Retry-After: 60 |
| 503 | SERVICE_UNAVAILABLE | Configuration/database/transient error |

Native clients may omit Origin. If Origin is present it MUST equal APP_ORIGIN; foreign and null origins
are rejected. No permissive CORS is enabled. Activation authenticates with the license key, never an admin cookie.
This exception applies only to activation: all admin and claim mutations retain exact-origin checks.

## Device-limit race protection

After rate limiting, the server executes one PostgreSQL function in one transaction:
1. SELECT ... FOR UPDATE locks the license row.
2. Check authoritative status; revoked licenses fail even for an existing device.
3. Look up the unique (license_id, device_id). Existing devices update last_seen_at without a new slot.
4. For a new ID, count devices and insert only below max_devices.

The function is VOLATILE. At READ COMMITTED its later statements obtain fresh snapshots after waiting
for the lock, so the second activation sees the first registration.
Counting devices in an old outer-query snapshot would not provide this guarantee.
The unique pair constraint also prevents duplicate registrations. SQL failures roll back the transaction.
[PostgreSQL snapshot behavior](https://www.postgresql.org/docs/current/xfunc-volatility.html).

Reset/revoke/reactivate acquire the SAME license row lock. They therefore serialize against activation.
A reset can legitimately be followed by a new activation; a revoke cannot undo an already completed activation,
but subsequent activations reject after it commits. Arbitrary direct SQL writes are trusted operations and can
bypass these application-level device-limit rules.

## Admin

The authenticated /admin list shows used/max devices, email, type, status, claimed date and masked key suffix.
Expand Inspect devices for shortened IDs and registration/last-seen timestamps.
SQL selects only the first eight device-ID characters for display; no full ID is sent to the browser.

- Reset Devices: UI confirmation, remove registrations, preserve key and status.
  Active stays active; resetting a revoked license does NOT reactivate it.
- Revoke: UI confirmation, mark revoked, retain key/license row and registrations.
- Reactivate: UI confirmation, mark active; same key and existing registrations work again.

POST /api/admin/licenses accepts {licenseId, action}, with reset/revoke/reactivate.
Each action requires the existing admin session and exact Origin. There is no public device-list or extra status API.

Revocation only prevents future SERVER activations in Stage 3. It does not disable a running desktop
app or invalidate an offline token. No offline tokens are issued at this stage.

## Manual browser/API checklist

1. Apply migration, run the app, sign in to /admin.
2. Create/claim a fresh test invite with max devices 2; save the key.
3. In PowerShell, read the key at a prompt (avoid putting it in command history):
```powershell
$activationKey = Read-Host 'Paste the saved test license key'
function Test-Activation([string]$deviceIdentifier) {
    $payload = @{ licenseKey = $activationKey; deviceId = $deviceIdentifier } | ConvertTo-Json
    try {
        Invoke-RestMethod -Method Post -Uri 'http://localhost:3000/api/license/activate' -ContentType 'application/json' -Body $payload
    } catch { $_.ErrorDetails.Message }
}
Test-Activation 'opaque-hashed-device-01'
Test-Activation 'opaque-hashed-device-02'
Test-Activation 'opaque-hashed-device-03'
Test-Activation 'opaque-hashed-device-01'
```
Expect success, success, DEVICE_LIMIT_REACHED, success. Use the HTTPS deployment URL when testing deployment.
Do not share terminal transcripts containing keys.

4. Refresh /admin: expect 2 / 2 and two shortened identifiers. Repeat device 1; count stays 2.
5. Revoke, activate device 1: LICENSE_REVOKED. Reactivate, repeat: success with the same key and two rows.
6. Reset and confirm: 0 / 2, still active. Activate device 3: success, now 1 / 2.
7. Revoke then reset: status remains revoked and activation fails until reactivated.
8. Send {} or a malformed deviceId: INVALID_REQUEST. Send a foreign Origin: rejection.
9. Sign out: /admin redirects. Anonymous POST /api/admin/licenses for each action returns 401 with the correct
   Origin, or 403 for absent/foreign Origin.
10. On a dedicated test system only, 121 valid-shaped activation attempts in a minute reach RATE_LIMITED.
    Do not exhaust this global bucket on production.
11. Remove-Variable activationKey when finished.

npm test covers database behavior; npm run test:http checks the production HTTP guards.
For real independent-connection races, set TEST_DATABASE_URL to an already-migrated dedicated Neon test branch:
```powershell
node --conditions=react-server --test tests/activation-neon.test.mjs
```
The helper waits for actual PostgreSQL row-lock contention, testing last-slot allocation, same-device
deduplication, revoke versus activation and reset versus activation. It also supports native pg clients.
It deletes only its own invite/license/devices. Run on a quiet test branch because activation throttling remains enabled.

## Security limits

- One bounded global activation bucket in request_limits: 120 valid-shaped attempts/minute, including wrong keys.
  Invalid bodies fail before database work. No Redis, IP tracking, or unbounded per-guessed-key rows.
  An attacker can exhaust the shared bucket and temporarily deny legitimate activations; use hosting-level abuse protection
  as traffic warrants it. The existing separate admin login bucket remains 20 attempts/15 minutes.
- Clients can lie about deviceId. Stronger binding awaits desktop integration; the server stores only the supplied opaque ID.
- Origin checks do not authenticate non-browser clients. Possession of the key is the credential.
- Existing stateless admin sessions, lack of MFA, bearer invite links and one-time-key recovery limitations remain.
- Application code never logs full keys or request bodies. Do not enable body capture in hosting/APM tooling.
  Hosting access logs may still contain bearer invite URLs.
- No live Neon deployment or real desktop integration is implied by local database/build checks.

