# Stage 4 — desktop licensing (1.2.0 test integration)

No production service is deployed yet. Local verification artifacts use a disposable keypair and the reserved origin https://licensing.invalid. Do not distribute those artifacts. No Stage 5 work is included.

## Architecture
IceApplication::initialise verifies local authorization before constructing MainComponent. Without valid authorization it creates ActivationComponent. All --self-test runs also pass this gate. There is no license bypass, including Debug. MIDI generation, preview, export, settings and the Python engine are unchanged. Activation uses a worker thread and JUCE native HTTP; redirects are disabled, responses bounded, and connection timeout is 15 seconds. Closing during activation waits for the worker, so shutdown may briefly wait for network timeout.

LicenseManager is isolated from generation. It validates signatures with vendored unmodified Monocypher 4.0.3 Ed25519. It does not log or persist license keys. The server alone loads the private key; desktop binaries contain only the public key.

## Device identity (candidate v2)
Windows first reads the 64-bit HKLM\SOFTWARE\Microsoft\Cryptography MachineGuid (REG_SZ), trims/lowercases and removes GUID hyphens/braces. It requires exactly 32 hex digits and rejects all-zero/all-f values. This OS-installation identifier survives application restart/update/reinstall; OS reinstallation, cloned images or deliberate registry edits can change/duplicate it. No username, MAC, IP, path, time or generated random fallback is used.

If MachineGuid cannot be read or is invalid, Windows uses JUCE getUniqueDeviceID(), validating its nonzero signed-decimal representation without discarding its sign. JUCE hashes selected SMBIOS system manufacturer/product/UUID, baseboard manufacturer/product/version/serial/asset-tag and processor manufacturer/version/asset-tag/part-number. Firmware access can be denied in AppContainer. If both sources fail, activation fails closed. Choosing MachineGuid first avoids changing identity merely because firmware access differs between sandboxed and ordinary launches; if the selected registry source itself becomes unavailable, switching to the fallback can require reactivation.

macOS uses normalized 32-hex IOPlatformUUID via JUCE. Unsupported platforms fail closed. The transmitted identifier is SHA-256 over UTF-8 `icepunk-device-v2:<source>:<normalized-value>`, where source is `windows-machineguid`, `windows-juce-smbios` or `macos-platformuuid`. Raw identifiers never enter the HTTP request or logs. This version intentionally replaces the pre-deployment v1 scheme; old disposable v1 authorizations must be discarded/reissued. It is casual-sharing protection, not hardware attestation.

## Authorization
Response adds `authorization`: lowercase hex UTF-8 JSON, a dot, then 128 hex characters containing the 64-byte Ed25519 signature over the exact JSON bytes. JSON fields: version=1, product=icepunk-midi, licenseId, deviceId, instagram, licenseType, issuedAt (Unix seconds). Verification precedes field acceptance and checks product/version/device/metadata. Token has no expiry at this stage. Hex is used for simple bounded cross-language decoding, not encryption. No database migration is required. Signing configuration is checked before the database activation consumes a slot; retrying the same device remains idempotent.

Local file: Windows `%LOCALAPPDATA%/ICEPUNK/MIDIGenerator/licensing/license.dat`; macOS `~/Library/Application Support/ICEPUNK/MIDIGenerator/licensing/license.dat`. The existing ICEPUNK_DATA_DIR diagnostic path override remains, but never bypasses signature/device checks. Atomic temporary-file replacement; file is outside installation and session cleanup paths. Token metadata is readable; no encryption is claimed.

Offline startup never contacts the server. Revocation/reset on the server cannot invalidate existing offline authorizations. Reset permits more devices to activate while previously authorized machines remain usable offline. Rotating the verification key requires rebuilding clients and reactivation, and should be planned carefully.

## Signing configuration
From web, run `node scripts/signing-keys.mjs C:/secure/icepunk-private.env` (a NEW file outside the repository). The script writes LICENSE_SIGNING_PRIVATE_KEY as base64 PKCS8 DER and prints ICEPUNK_LICENSE_PUBLIC_KEY as raw 32-byte hex. Protect the private file with appropriate Windows ACLs; Unix creation mode is 0600. Never use the disposable integration-test keypair for testers.

Set LICENSE_SIGNING_PRIVATE_KEY in local server .env.local / Vercel server environment. Preserve all Stage 1–3 variables documented in web/.env.example. There is no NEXT_PUBLIC private variable. Never put private material in CMake, source, installer, or desktop config. Deploy/redeploy after configuring Vercel.

CMake requires `-DICEPUNK_LICENSE_ORIGIN=https://<deployment-domain>` and `-DICEPUNK_LICENSE_PUBLIC_KEY=<64 lowercase hex>`. URL is an origin without path or trailing slash. Release compilation rejects HTTP. Debug supports an explicitly supplied http://localhost:3000 or http://127.0.0.1:3000. These are not runtime environment overrides. No source change is needed when the deployment URL becomes available.

Local Next.js: configure Neon and existing migrations as in web/README.md; add signing key; run npm run dev. Configure CMake with the local origin and matching public key, then build Debug. Claim a key from the local website and activate. Real Neon end-to-end tests still require a configured database.

Windows packaging:
`powershell -File scripts/build_windows.ps1 -LicenseOrigin https://<domain> -LicensePublicKey <public-hex> -LicenseFile C:/secure/build-machine-license.dat -JuceSource <optional JUCE source>`
LicenseFile must be a real valid authorization for the build machine and matching public key. Packaged smoke tests copy it to their temporary app-data folder; it is never bundled. Activate a build first to obtain this file. Do not set signing-hook environment variables for this unsigned test release. -SkipTests is available from the existing pipeline but does not bypass desktop licensing and is not a substitute for release verification.

macOS build variables: ICEPUNK_LICENSE_ORIGIN, ICEPUNK_LICENSE_PUBLIC_KEY, ICEPUNK_TEST_LICENSE_FILE. Build on macOS; not validated on Windows. The same packaging test uses a machine-bound authorization.

## Manual tester checklist (after production deployment and rebuild)
1. Configure Vercel/Neon/migrations, private key, and exact APP_ORIGIN. Create an invite in protected /admin, claim with an email, save the displayed key once.
2. Build 1.2.0 with actual HTTPS origin and matching public key. Verify installer BUILD_INFO/version and test artifact provenance; do not ship licensing.invalid artifacts.
3. Fresh PC A/app-data: launch -> activation only, no generator. Try malformed/unknown key, revoked key, offline server, then valid key -> saved authorization and normal UI.
4. Close/restart normally, then disconnect internet and restart: normal UI opens with the same authorization file.
5. PC B: same key activates as second device. PC C: rejected at maxDevices=2. Repeat activation on A consumes no additional slot.
6. Back up A's license.dat, edit payload or signature: activation screen. Corrupt/truncate file: activation screen. Restore original: offline startup succeeds.
7. Copy A's file to B before B independently activates (or a clean C): it must not authorize that different device. Never erase real tester data without backing it up.
8. Admin reset: registrations disappear and a new device can activate. Old offline files still work. Revoke: new activation including repeat device is rejected; old offline files still work. Reactivate restores server eligibility.
9. Generate several MIDI batches; preview; adjust existing controls; export all clips; drag MIDI into DAW; confirm Python launch and sessions cleanup. Compare settings and user files before/after restart/update.
10. Install over previous version; authorization survives. Uninstall/reinstall normally preserves user-data authorization. Test closing during activation, activation timeout, read-only app-data, and no-network startup.
11. Repeat on supported macOS hardware after a native build. Complete real fresh-device checks; synthetic fixtures do not substitute for these.

## Limits
Casual-sharing protection only. Binary patching, machine ID spoofing, copied VM identities, direct invocation of the separately shipped Python engine, or a compromised signing key are outside this protection. The standalone generator UI is gated, but the Python engine is deliberately unmodified. No periodic validation, key recovery, email verification, codesigning, anti-debugging or DRM obfuscation. Existing global activation throttling can temporarily block legitimate users. Missing hardware UUID requires support. This stage does not claim professional reverse-engineering resistance.

## Native identity tests
The candidate LicenseTests executable requires a real nonempty 64-hex identity and checks consecutive calls. It has no synthetic fallback. The Release and Debug test processes must produce the same identity. Separate explicit mismatched-device values are used only for negative signature/device-binding tests and never enter the production target. This supersedes the earlier pending test-only synthetic fallback proposal.
