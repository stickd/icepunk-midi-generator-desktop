#include "PresetManager.h"
#include "RuntimePaths.h"
#include <cmath>
#include <stdexcept>

namespace
{
constexpr juce::int64 maxBytes = 128 * 1024 * 1024;
void require(bool ok, const juce::String& message)
{
    if (!ok) throw std::runtime_error(message.toStdString());
}
bool number(const juce::var& v)
{
    return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite(double(v));
}
bool integer(const juce::var& v) { return v.isInt() || v.isInt64(); }
void object(const juce::var& v) { require(v.isObject(), "Incomplete analysis: expected an object."); }
void distribution(const juce::var& v)
{
    object(v);
    for (const auto& property : v.getDynamicObject()->getProperties())
        require(integer(property.value) && double(property.value) >= 0,
                "Invalid analysis distribution: expected non-negative integer counts.");
}
juce::String validName(juce::String name)
{
    name = name.trim();
    require(name.isNotEmpty() && name.length() <= 80, "Preset name must contain 1 to 80 characters.");
    require(!name.containsAnyOf("<>:\"/\\|?*") && !name.endsWithChar('.') && name != "." && name != "..",
            "Preset name contains invalid filename characters.");
    for (auto c : name) require(c >= 32 && c != 127, "Preset name contains control characters.");
    auto stem = name.upToFirstOccurrenceOf(".", false, false).toUpperCase();
    const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    require(!reserved.contains(stem), "This preset name is reserved on Windows. Choose another name.");
    return name;
}
juce::var readJson(const juce::File& file)
{
    require(file.existsAsFile(), "Cannot read preset/analysis file: " + file.getFileName());
    require(file.getSize() > 0 && file.getSize() <= maxBytes, "Preset/analysis file is empty or exceeds 128 MB.");
    juce::MemoryBlock bytes;
    require(file.loadFileAsData(bytes), "Cannot read preset/analysis file.");
    juce::var value;
    auto result = juce::JSON::parse(bytes.toString(), value);
    require(result.wasOk(), "Malformed preset/analysis JSON: " + result.getErrorMessage());
    return value;
}
void writeJson(const juce::File& file, const juce::var& value)
{
    auto json = juce::JSON::toString(value, true);
    require(json.getNumBytesAsUTF8() <= size_t(maxBytes), "Preset/analysis exceeds the 128 MB limit.");
    require(file.getParentDirectory().createDirectory().wasOk(), "Cannot create preset/analysis directory.");
    juce::TemporaryFile temporary(file);
    require(temporary.getFile().replaceWithText(json) && temporary.overwriteTargetFileWithTemporary(),
            "Cannot write preset/analysis file. Check folder permissions and disk space.");
}
}

PresetManager::PresetManager() : PresetManager(icepunkDataDirectory().getChildFile("presets")) {}
PresetManager::PresetManager(juce::File folder) : directory(std::move(folder)) {}

juce::Array<juce::File> PresetManager::getPresets() const
{
    require(directory.createDirectory().wasOk(), "Cannot create presets directory.");
    auto files = directory.findChildFiles(juce::File::findFiles, false, "*");
    files.removeIf([](const juce::File& f) { return !f.hasFileExtension("json"); });
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });
    return files;
}

void PresetManager::validateAnalysis(const juce::var& analysis)
{
    object(analysis);
    auto* files = analysis["files"].getArray();
    require(files != nullptr && !files->isEmpty(), "Incomplete analysis: files must be a non-empty array.");
    bool usable = false;
    for (const auto& file : *files)
    {
        object(file);
        require(file.hasProperty("estimated_key") && file.hasProperty("tempo_bpm_estimate"),
                "Incomplete analysis: missing key or tempo.");
        auto key = file["estimated_key"];
        if (!key.isVoid())
        {
            require(key.isString(), "Invalid analysis key.");
            auto parts = juce::StringArray::fromTokens(key.toString(), " ", "");
            const juce::StringArray roots { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            require(parts.size() == 2 && roots.contains(parts[0]) && (parts[1] == "minor" || parts[1] == "major"), "Invalid analysis key.");
        }
        auto bpm = file["tempo_bpm_estimate"];
        require(bpm.isVoid() || (number(bpm) && double(bpm) > 0 && double(bpm) <= 1000), "Invalid analysis tempo.");
        distribution(file["rhythm_position_distribution"]);
        distribution(file["register_distribution"]);
        auto* patterns = file["patterns"].getArray();
        require(patterns != nullptr, "Incomplete analysis: missing patterns array.");
        for (const auto& pattern : *patterns)
        {
            object(pattern);
            distribution(pattern["register_distribution"]);
            auto* notes = pattern["notes"].getArray();
            require(notes != nullptr, "Incomplete analysis: missing notes array.");
            for (const auto& note : *notes)
            {
                object(note);
                require(integer(note["pitch"]) && double(note["pitch"]) >= 0 && double(note["pitch"]) <= 127,
                        "Invalid analysis note pitch (expected 0-127).");
                require(integer(note["velocity"]) && double(note["velocity"]) >= 1 && double(note["velocity"]) <= 127,
                        "Invalid analysis note velocity (expected 1-127).");
                auto start = note["relative_start_beat"], duration = note["duration_beats"];
                require(number(start) && double(start) >= 0 && double(start) < 8,
                        "Invalid analysis note start (expected a position in a two-bar phrase).");
                require(number(duration) && double(duration) > 0 && double(duration) <= 100000,
                        "Invalid analysis note duration.");
                if (note.hasProperty("register"))
                    require(note["register"].isString() && juce::StringArray{"bass", "mid", "top"}.contains(note["register"].toString()), "Invalid analysis note register.");
            }
            if (notes->size() >= 2 && notes->size() <= 40) usable = true;
        }
    }
    require(usable, "Analysis contains no usable two-bar patterns with 2 to 40 notes.");
}

juce::var PresetManager::readAnalysis(const juce::File& file)
{
    auto analysis = readJson(file);
    validateAnalysis(analysis);
    return analysis;
}

AnalysisPreset PresetManager::loadPreset(const juce::File& file) const
{
    auto json = readJson(file);
    require(json.isObject(), "Preset must be a JSON object.");
    require(integer(json["schemaVersion"]) && double(json["schemaVersion"]) == 1,
            "Unsupported or missing preset schemaVersion. Expected version 1.");
    require(json["name"].isString(), "Preset is missing its name.");
    auto name = validName(json["name"].toString());
    validateAnalysis(json["analysis"]);
    return { name, json["analysis"], file };
}

AnalysisPreset PresetManager::savePreset(juce::String name, const juce::var& analysis) const
{
    name = validName(name);
    validateAnalysis(analysis);
    // Single-instance app, plus interprocess lock for concurrent cooperating writers.
    juce::InterProcessLock lock("ICEPUNK-presets-" + juce::String::toHexString(directory.getFullPathName().hashCode64()));
    require(lock.enter(1000), "Presets are being saved by another process. Try again.");
    struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock { lock };
    auto target = directory.getChildFile(name + ".json");
    for (const auto& file : getPresets())
    {
        require(!file.getFileName().equalsIgnoreCase(target.getFileName()), "A preset with this name already exists. Choose another name.");
        // A copied/renamed preset can also have the same display name.
        juce::String existingName;
        try { existingName = loadPreset(file).name; }
        catch (const std::exception&) {} // Invalid imports do not block saving another name.
        require(!existingName.equalsIgnoreCase(name), "A preset with this display name already exists. Choose another name.");
    }
    require(!target.exists(), "A file with this preset name already exists.");
    auto* json = new juce::DynamicObject();
    juce::var document(json);
    json->setProperty("schemaVersion", 1); json->setProperty("name", name); json->setProperty("analysis", analysis);
    writeJson(target, document);
    return { name, analysis, target };
}

void PresetManager::writeAnalysis(const juce::File& file, const juce::var& analysis)
{
    validateAnalysis(analysis);
    writeJson(file, analysis);
}
