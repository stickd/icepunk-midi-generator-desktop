#include "RuntimePaths.h"

juce::File icepunkDataDirectory()
{
    auto override = juce::SystemStats::getEnvironmentVariable("ICEPUNK_DATA_DIR", {});
    if (override.isNotEmpty() && juce::File::isAbsolutePath(override)) return juce::File(override);
   #if JUCE_WINDOWS
    auto base = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    return (base.isEmpty() ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          : juce::File(base)).getChildFile("ICEPUNK/MIDIGenerator");
   #elif JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Application Support/ICEPUNK/MIDIGenerator");
   #else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ICEPUNK/MIDIGenerator");
   #endif
}

juce::File icepunkPackagedEngine(const juce::File& root)
{
   #if JUCE_WINDOWS
    return root.getChildFile("engine/icepunk-engine.exe");
   #else
    return root.getChildFile("engine/icepunk-engine");
   #endif
}

juce::File icepunkRuntimeRoot()
{
    auto executableDirectory = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
   #if JUCE_MAC
    auto root = executableDirectory.getSiblingFile("Resources");
   #else
    auto root = executableDirectory;
   #endif
   #if ICEPUNK_DEVELOPMENT_ENGINE
    if (!icepunkPackagedEngine(root).existsAsFile())
    {
        auto source = executableDirectory;
        for (int i = 0; i < 8; ++i)
        {
            if (source.getChildFile("prototype/bridge/generate.py").existsAsFile()) return source;
            source = source.getParentDirectory();
        }
    }
   #endif
    return root;
}
