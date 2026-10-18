#include "LicenseManager.h"
#include "RuntimePaths.h"
#include "AppConfig.h"
#include "monocypher-ed25519.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if !JUCE_DEBUG
static_assert(ICEPUNK_LICENSE_ORIGIN[0] == 'h' && ICEPUNK_LICENSE_ORIGIN[4] == 's', "Release requires an explicit HTTPS licensing origin");
#endif
namespace {
bool hex(const juce::String& text, juce::MemoryBlock& out) {
    if (text.isEmpty() || text.length() % 2 || !text.containsOnly("0123456789abcdef")) return false;
    out.loadFromHexString(text); return out.getSize() == size_t(text.length()/2);
}
}
LicenseManager::LicenseManager() : LicenseManager(icepunkDataDirectory(), ICEPUNK_LICENSE_PUBLIC_KEY, machineId(), ICEPUNK_LICENSE_ORIGIN) {}
LicenseManager::LicenseManager(juce::File dir, juce::String pk, juce::String device, juce::String url)
 : file(dir.getChildFile("licensing/license.dat")), publicKey(pk), deviceId(device), origin(url) {}
juce::String LicenseManager::machineId() {
    juce::String id, source;
   #if JUCE_WINDOWS
    // Prefer the OS installation identity consistently, including when firmware
    // access changes between sandboxed and ordinary launches. Never transmit it.
    wchar_t value[128]{};
    DWORD size = sizeof(value);
    const auto result = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
        L"MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value, &size);
    if (result == ERROR_SUCCESS)
        id = juce::String(value).trim().toLowerCase().removeCharacters("-{}");
    SecureZeroMemory(value, sizeof(value));
    if (id.length() == 32 && id.containsOnly("0123456789abcdef")
        && id.removeCharacters("0").isNotEmpty() && id.removeCharacters("f").isNotEmpty())
        source = "windows-machineguid";
    else {
        // JUCE returns a signed decimal hash of selected SMBIOS fields on Windows.
        id = juce::SystemStats::getUniqueDeviceID().trim();
        auto digits = id.startsWithChar('-') ? id.substring(1) : id;
        if (digits.isEmpty() || digits.length() > 19 || !digits.containsOnly("0123456789")
            || digits.removeCharacters("0").isEmpty()) return {};
        source = "windows-juce-smbios";
    }
   #elif JUCE_MAC
    id = juce::SystemStats::getUniqueDeviceID().trim().toLowerCase().removeCharacters("-");
    if (id.length() != 32 || !id.containsOnly("0123456789abcdef")
        || id.removeCharacters("0").isEmpty() || id.removeCharacters("f").isEmpty()) return {};
    source = "macos-platformuuid";
   #else
    return {};
   #endif
    return juce::SHA256(("icepunk-device-v2:" + source + ":" + id).toUTF8()).toHexString();
}
bool LicenseManager::verify(const juce::String& token) const {
    if (deviceId.isEmpty() || token.length() > 8192) return false;
    auto parts = juce::StringArray::fromTokens(token, ".", "");
    if (parts.size() != 2 || parts[1].length() != 128 || publicKey.length() != 64) return false;
    juce::MemoryBlock payload, signature, pk;
    if (!hex(parts[0], payload) || !hex(parts[1], signature) || !hex(publicKey, pk)) return false;
    if (crypto_ed25519_check(static_cast<const uint8_t*>(signature.getData()), static_cast<const uint8_t*>(pk.getData()),
          static_cast<const uint8_t*>(payload.getData()), payload.getSize()) != 0) return false;
    auto data = juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(payload.getData()), int(payload.getSize())));
    if (!data.isObject()) return false;
    return data["version"].isInt() && int(data["version"]) == 1 && data["product"].toString() == "icepunk-midi"
        && data["deviceId"].isString() && data["deviceId"].toString() == deviceId
        && data["licenseId"].isString() && data["licenseId"].toString().isNotEmpty()
        && data["instagram"].isString() && data["instagram"].toString().isNotEmpty()
        && (data["licenseType"].toString() == "standard" || data["licenseType"].toString() == "beta")
        && (data["issuedAt"].isInt64() || data["issuedAt"].isInt()) && juce::int64(data["issuedAt"]) > 0;
}
bool LicenseManager::load() { return file.existsAsFile() && file.getSize() <= 8192 && verify(file.loadFileAsString()); }
bool LicenseManager::save(const juce::String& token) {
    if (!verify(token) || !file.getParentDirectory().createDirectory()) return false;
    juce::TemporaryFile temp(file);
    return temp.getFile().replaceWithText(token) && temp.overwriteTargetFileWithTemporary();
}
juce::String LicenseManager::errorMessage(const juce::String& code) {
    if (code == "INVALID_LICENSE" || code == "INVALID_REQUEST") return "Invalid license key. Check the key and try again.";
    if (code == "LICENSE_REVOKED") return "This license has been revoked. Contact the seller.";
    if (code == "DEVICE_LIMIT_REACHED") return "Device limit reached. Contact the seller to reset devices.";
    if (code == "RATE_LIMITED") return "Too many attempts. Please wait a minute and try again.";
    return "No internet or activation server unavailable. Please try again later.";
}
juce::String LicenseManager::activate(juce::String key) {
    if (deviceId.isEmpty()) return "Cannot identify this computer. Contact support before activating.";
    bool allowed = origin.startsWith("https://");
   #if JUCE_DEBUG
    allowed = allowed || origin.startsWith("http://localhost:") || origin.startsWith("http://127.0.0.1:");
   #endif
    if (!allowed || publicKey.length() != 64) return "This build has no licensing configuration. Contact the seller.";
    key = key.trim().toUpperCase();
    auto groups = juce::StringArray::fromTokens(key, "-", "");
    if (groups.size() != 9 || groups[0] != "ICEPUNK") return errorMessage("INVALID_LICENSE");
    for (int i=1;i<groups.size();++i) if (groups[i].length()!=4 || !groups[i].containsOnly("0123456789ABCDEF")) return errorMessage("INVALID_LICENSE");
    auto body = new juce::DynamicObject(); body->setProperty("licenseKey", key); body->setProperty("deviceId", deviceId);
    int status = 0;
    auto stream = juce::URL(origin + "/api/license/activate").withPOSTData(juce::JSON::toString(juce::var(body)))
      .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
        .withHttpRequestCmd("POST").withExtraHeaders("Content-Type: application/json\r\n")
        .withConnectionTimeoutMs(15000).withNumRedirectsToFollow(0).withStatusCode(&status));
    if (!stream) return errorMessage({});
    juce::MemoryBlock bytes; stream->readIntoMemoryBlock(bytes, 16385);
    if (bytes.getSize() > 16384) return errorMessage({});
    auto response = juce::JSON::parse(bytes.toString());
    if (status != 200) return errorMessage(response["code"].toString());
    if (!verify(response["authorization"].toString())) return "Server authorization could not be verified. Contact the seller.";
    if (!save(response["authorization"].toString())) return "Cannot save authorization. Check application-data folder permissions.";
    return {};
}
