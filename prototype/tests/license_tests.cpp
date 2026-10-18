#include "LicenseManager.h"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3 && argc != 5 && argc != 6) return 2;
    auto folder = juce::File(argv[1]);
    auto publicKey = juce::String(argv[2]);
    auto device = LicenseManager::machineId();
    if (device.length() != 64 || !device.containsOnly("0123456789abcdef") || device != LicenseManager::machineId()) { std::cerr << "FAIL real machine identity" << std::endl; return 3; }
    std::cout << "PASS real machine identity present and stable" << std::endl;
    folder.getChildFile("device.txt").replaceWithText(device);
    // Local integration mode: keep the claimed key out of command lines and logs.
    // This executable is test-only; activation still uses production LicenseManager.
    if (argc == 6 && juce::String(argv[3]) == "--activate-file") {
        LicenseManager online(folder.getChildFile("data"), publicKey, device, argv[4]);
        auto message = online.activate(juce::File(argv[5]).loadFileAsString().trim());
        if (message.isEmpty() && online.load()) { std::cout << "PASS native backend activation and signed response" << std::endl; return 0; }
        std::cerr << "FAIL native backend activation: " << message << std::endl; return 1;
    }
    auto tokenFile = folder.getChildFile("token.txt");
    if (!tokenFile.existsAsFile()) return 0;
    auto token = tokenFile.loadFileAsString();
    auto dir = folder.getChildFile("data");
    LicenseManager manager(dir, publicKey, device, "https://licensing.invalid");
    if (argc == 5) { LicenseManager online(dir, publicKey, device, argv[3]); auto message = online.activate("ICEPUNK-1111-1111-1111-1111-1111-1111-1111-1111"); if (juce::String(argv[4]) == "success") return message.isEmpty() && online.load() ? 0 : 1; return message == LicenseManager::errorMessage(argv[4]) ? 0 : 1; }
    int failures = 0;
    auto check = [&](bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl; if (!ok) ++failures; };
    check(!manager.load(), "missing token requires activation");
    check(manager.verify(token), "Node Ed25519 signature verifies in C++");
    check(!manager.verify("garbage"), "malformed token rejected");
    check(!manager.verify(token.substring(0,2) + "00" + token.substring(4)), "payload tampering rejected");
    auto changed = token.dropLastCharacters(2) + (token.endsWith("00") ? "01" : "00");
    check(!manager.verify(changed), "signature tampering rejected");
    LicenseManager other(dir, publicKey, "other-device", "https://licensing.invalid");
    check(!other.verify(token), "other machine rejected");
    LicenseManager unidentified(dir, publicKey, {}, "http://localhost:3000");
    check(!unidentified.verify(token), "missing device identity rejects authorization");
    check(unidentified.activate("unused").contains("Cannot identify"), "missing device identity blocks activation before HTTP");
   #if !JUCE_DEBUG
    LicenseManager insecure(dir, publicKey, device, "http://localhost:3000");
    check(insecure.activate("unused").contains("no licensing configuration"), "Release rejects HTTP loopback before request");
   #endif
    check(manager.save(token), "authorization saved");
    LicenseManager restart(dir, publicKey, device, "https://licensing.invalid");
    check(restart.load(), "restart loads offline without server");
    check(!other.load(), "copied file rejected");
    check(LicenseManager::errorMessage("INVALID_LICENSE").contains("Invalid"), "invalid key message");
    check(LicenseManager::errorMessage("LICENSE_REVOKED").contains("revoked"), "revoked message");
    check(LicenseManager::errorMessage("DEVICE_LIMIT_REACHED").contains("limit"), "limit message");
    dir.getChildFile("licensing/license.dat").replaceWithText("corrupted");
    check(!restart.load(), "corrupt storage rejected");
    return failures ? 1 : 0;
}
