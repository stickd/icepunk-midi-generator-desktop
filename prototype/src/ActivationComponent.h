#pragma once
#include "LicenseManager.h"
#include <thread>
class ActivationComponent final : public juce::Component, private juce::Timer {
public:
    ActivationComponent(LicenseManager& manager, std::function<void()> done) : license(manager), onSuccess(std::move(done)) {
        title.setText("iCEPUNK MIDI Generator", juce::dontSendNotification);
        prompt.setText("Enter License Key", juce::dontSendNotification);
        key.setTextToShowWhenEmpty("ICEPUNK-...", juce::Colours::grey); key.setPasswordCharacter('*');
        button.setButtonText("Activate");
        for (auto* c : std::initializer_list<juce::Component*>{ &title, &prompt, &key, &button, &status }) addAndMakeVisible(c);
        button.onClick = [this] {
            if (worker.joinable()) worker.join();
            key.setEnabled(false); button.setEnabled(false); status.setText("Activating...", juce::dontSendNotification);
            auto input = key.getText();
            worker = std::thread([this, input] { auto result = license.activate(input); { const juce::ScopedLock lock(mutex); message = result; ready = true; } });
            startTimer(100);
        };
        setSize(560, 280);
    }
    ~ActivationComponent() override { stopTimer(); if (worker.joinable()) worker.join(); }
    void resized() override {
        title.setBounds(25,20,getWidth()-50,35); prompt.setBounds(25,65,getWidth()-50,25);
        key.setBounds(25,100,getWidth()-50,30); button.setBounds(25,145,120,32);
        status.setBounds(25,185,getWidth()-50,70);
    }
private:
    void timerCallback() override {
        { const juce::ScopedLock lock(mutex); if (!ready) return; ready = false; }
        stopTimer(); if (worker.joinable()) worker.join();
        if (message.isEmpty()) {
            key.clear(); status.setText("Activation successful", juce::dontSendNotification);
            auto callback = onSuccess; juce::MessageManager::callAsync(std::move(callback));
        } else { status.setText(message, juce::dontSendNotification); key.setEnabled(true); button.setEnabled(true); }
    }
    LicenseManager& license; std::function<void()> onSuccess;
    juce::Label title, prompt, status; juce::TextEditor key; juce::TextButton button;
    std::thread worker; juce::CriticalSection mutex; bool ready=false; juce::String message;
};
