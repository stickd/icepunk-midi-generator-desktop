#include "PresetManager.h"
#include "RuntimePaths.h"
#include <iostream>
#include <functional>

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("icepunk-presets-" + juce::Uuid().toString());
    struct Cleanup { juce::File f; ~Cleanup() { f.deleteRecursively(); } } cleanup { folder };
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* label) { ++checks; std::cout << (ok ? "PASS " : "FAIL ") << label << '\n'; if (!ok) ++failures; };
    auto rejects = [&](const std::function<void()>& fn, const char* label) {
        bool rejected = false; try { fn(); } catch (const std::exception& e) { rejected = juce::String(e.what()).isNotEmpty(); }
        check(rejected, label);
    };
    try
    {
        PresetManager manager(folder.getChildFile("presets"));
        check(manager.getPresets().isEmpty() && manager.getPresetsDirectory().isDirectory(), "missing directory created, empty library accepted");
        check(PresetManager().getPresetsDirectory() == icepunkDataDirectory().getChildFile("presets"), "runtime data directory reused");
        auto analysis = PresetManager::readAnalysis(juce::File(argv[1]));
        auto saved = manager.savePreset("Real Dataset", analysis);
        auto loaded = manager.loadPreset(saved.file);
        check(loaded.name == "Real Dataset", "valid preset name loaded");
        check(juce::JSON::toString(loaded.analysis) == juce::JSON::toString(analysis), "real analysis JSON round trip is lossless");
        auto original = saved.file.loadFileAsString();
        rejects([&] { manager.savePreset("real dataset", analysis); }, "case-insensitive duplicate rejected");
        check(saved.file.loadFileAsString() == original, "duplicate leaves existing preset unchanged");
        for (auto name : { "", "   ", "../escape", "bad:name", "bad\\name", "bad.", "CON", "com1.txt" })
            rejects([&] { manager.savePreset(name, analysis); }, "invalid portable filename rejected");
        auto unicode = manager.savePreset(juce::String::fromUTF8("Зимний пресет"), analysis);
        check(manager.loadPreset(unicode.file).name == juce::String::fromUTF8("Зимний пресет"), "Unicode name round trip");
        auto broken = folder.getChildFile("broken.json");
        broken.replaceWithText("{broken");
        rejects([&] { manager.loadPreset(broken); }, "malformed JSON rejected");
        rejects([&] { manager.loadPreset(folder.getChildFile("missing.json")); }, "missing file rejected");
        broken.replaceWithText("");
        rejects([&] { manager.loadPreset(broken); }, "empty file rejected");
        auto document = juce::JSON::parse(original);
        auto invalidDocument = [&](const juce::var& value) { broken.replaceWithText(juce::JSON::toString(value)); manager.loadPreset(broken); };
        for (const auto& version : { juce::var(2), juce::var("1"), juce::var(true), juce::var(juce::int64(4294967297LL)) })
        {
            auto copy = document.clone(); copy.getDynamicObject()->setProperty("schemaVersion", version);
            rejects([&] { invalidDocument(copy); }, "unsupported/wrong-type schema rejected");
        }
        for (auto field : { "schemaVersion", "name", "analysis" })
        {
            auto copy = document.clone(); copy.getDynamicObject()->removeProperty(field);
            rejects([&] { invalidDocument(copy); }, "missing preset field rejected");
        }
        for (auto field : { "patterns", "estimated_key", "tempo_bpm_estimate", "rhythm_position_distribution", "register_distribution" })
        {
            auto copy = analysis.clone(); copy["files"][0].getDynamicObject()->removeProperty(field);
            rejects([&] { PresetManager::validateAnalysis(copy); }, "missing analysis field rejected");
        }
        for (auto field : { "pitch", "velocity", "relative_start_beat", "duration_beats" })
        {
            auto copy = analysis.clone(); copy["files"][0]["patterns"][0]["notes"][0].getDynamicObject()->removeProperty(field);
            rejects([&] { PresetManager::validateAnalysis(copy); }, "missing note field rejected");
        }
        {
            auto copy = analysis.clone(); copy["files"][0]["patterns"][0]["notes"][0].getDynamicObject()->setProperty("pitch", "60");
            rejects([&] { PresetManager::validateAnalysis(copy); }, "wrong note field type rejected");
        }
        auto blocked = folder.getChildFile("not-a-directory"); blocked.replaceWithText("file");
        PresetManager unwritable(blocked.getChildFile("presets"));
        rejects([&] { unwritable.savePreset("Test", analysis); }, "directory/write failure reported");
        // Invalid imported presets remain visible and fail only when selected.
        manager.getPresetsDirectory().getChildFile("corrupt.json").replaceWithText("bad");
        check(manager.getPresets().size() == 3, "enumeration tolerates corrupted import");
        auto snapshot = folder.getChildFile("snapshot.json");
        PresetManager::writeAnalysis(snapshot, loaded.analysis);
        check(juce::JSON::toString(PresetManager::readAnalysis(snapshot)) == juce::JSON::toString(analysis), "generation snapshot uses unchanged analysis schema");
        // Optional integration output for the Python bridge test and documented example.
        if (argc == 3)
        {
            juce::File output(argv[2]); output.createDirectory();
            check(saved.file.copyFileTo(output.getChildFile("real-preset.json")), "export real preset fixture");
            PresetManager::writeAnalysis(output.getChildFile("loaded-analysis.json"), loaded.analysis);
        }
    }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL unexpected: " << e.what() << '\n'; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
