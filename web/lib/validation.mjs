export const LICENSE_TYPES = ["standard", "beta"];
export class InputError extends Error {}
export function parseEmail(value) {
  if (typeof value !== "string") throw new InputError("Enter a valid email address.");
  const email = value.trim();
  const parts = email.split("@");
  const [local, domain] = parts;
  if (email.length > 254 || parts.length !== 2 || !local || local.length > 64
      || !/^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+$/.test(local)
      || local.startsWith(".") || local.endsWith(".") || local.includes("..")
      || !domain || domain.split(".").length < 2
      || !domain.split(".").every((label) =>
        /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$/.test(label)))
    throw new InputError("Enter a valid email address.");
  return local + "@" + domain.toLowerCase();
}
export function parseInvite(input) {
  if (!input || typeof input !== "object" || Array.isArray(input)) throw new InputError("Invalid form.");
  const username = typeof input.instagramUsername === "string"
    ? input.instagramUsername.trim().replace(/^@/, "") : "";
  if (!/^[a-zA-Z0-9._]{1,30}$/.test(username))
    throw new InputError("Enter an Instagram username of 1–30 letters, numbers, dots or underscores.");
  if (!LICENSE_TYPES.includes(input.licenseType)) throw new InputError("Choose a valid license type.");
  const devices = input.maxDevices === undefined ? 2 : input.maxDevices;
  if (!Number.isInteger(devices) || devices < 1 || devices > 20)
    throw new InputError("Max devices must be a whole number from 1 to 20.");
  return { username: username.toLowerCase(), licenseType: input.licenseType, maxDevices: devices };
}

