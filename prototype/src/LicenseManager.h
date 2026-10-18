#pragma once
#include <JuceHeader.h>
class LicenseManager {
public:
    LicenseManager();
    LicenseManager(juce::File directory, juce::String publicKey, juce::String device, juce::String origin);
    bool load();
    bool verify(const juce::String& token) const;
    bool save(const juce::String& token);
    juce::String activate(juce::String key);
    static juce::String errorMessage(const juce::String& code);
    static juce::String machineId();
private:
    juce::File file;
    juce::String publicKey, deviceId, origin;
};
