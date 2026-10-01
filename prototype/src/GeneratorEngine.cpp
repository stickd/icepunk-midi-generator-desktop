#include "GeneratorEngine.h"
#include "AppConfig.h"
#include "RuntimePaths.h"
#include <map>
#include <deque>
#include <stdexcept>

namespace
{
uint32_t mix(uint32_t value)
{
    value ^= value >> 16; value *= 0x7feb352dU;
    value ^= value >> 15; value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

float stableUnit(uint32_t seed, uint32_t index, uint32_t salt)
{
    return float(mix(seed ^ (index * 0x9e3779b9U) ^ salt)) / float(UINT32_MAX);
}

int keyRoot(const juce::String& key)
{
    const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::jmax(0, names.indexOf(key.upToFirstOccurrenceOf(" ", false, false)));
}

bool isMinor(const juce::String& key) { return key.endsWithIgnoreCase("minor"); }

int nearestPitchInKey(int pitch, const juce::String& key)
{
    const int root = keyRoot(key);
    const int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    const int* scale = isMinor(key) ? minor : major;
    pitch = juce::jlimit(36, 84, pitch);
    for (int offset = 0; offset <= 12; ++offset)
        for (int sign : { -1, 1 })
        {
            const int candidate = juce::jlimit(36, 84, pitch + sign * offset);
            for (int scaleIndex = 0; scaleIndex < 7; ++scaleIndex)
                if ((candidate - root + 120) % 12 == scale[scaleIndex]) return candidate;
        }
    return pitch;
}

std::vector<PreviewNote> transformedNotes(const GeneratedClip& clip, LiveControls controls)
{
    std::vector<PreviewNote> result;
    const auto& source = clip.baseNotes.empty() ? clip.notes : clip.baseNotes;
    controls.mutation = juce::jlimit(0.0f, 1.0f, controls.mutation);
    controls.variation = juce::jlimit(0.0f, 1.0f, controls.variation);
    controls.density = juce::jlimit(0.0f, 1.0f, controls.density);
    bool keptFirst = false;
    for (size_t i = 0; i < source.size(); ++i)
    {
        const auto& base = source[i];
        if (base.start < 0.0f || base.start >= 8.0f || base.duration <= 0.0f) continue;
        // Thin one two-bar phrase, then repeat it exactly. Keep at least one note.
        if (keptFirst && stableUnit(clip.seed, uint32_t(i), 11) >= controls.density) continue;
        keptFirst = true;
        auto note = base;
        if (stableUnit(clip.seed, uint32_t(i), 21) < controls.mutation)
        {
            const int direction = stableUnit(clip.seed, uint32_t(i), 22) < 0.5f ? -1 : 1;
            const int steps = 2 + int(controls.variation * 10.0f);
            note.pitch = nearestPitchInKey(base.pitch + direction * steps, clip.key);
        }
        if (stableUnit(clip.seed, uint32_t(i), 31) < controls.mutation * 0.7f)
        {
            const float direction = stableUnit(clip.seed, uint32_t(i), 32) < 0.5f ? -1.0f : 1.0f;
            const float shift = controls.variation < 0.5f ? 0.25f : 0.5f;
            note.start = juce::jlimit(0.0f, 7.75f, base.start + direction * shift);
            note.start = std::round(note.start * 4.0f) / 4.0f;
            note.duration = juce::jmin(base.duration, 8.0f - note.start);
        }
        if (stableUnit(clip.seed, uint32_t(i), 41) < controls.mutation)
        {
            const int amount = 5 + int(controls.variation * 30.0f);
            const int direction = stableUnit(clip.seed, uint32_t(i), 42) < 0.5f ? -1 : 1;
            note.velocity = juce::jlimit(35, 127, base.velocity + direction * amount);
        }
        const int startTick = juce::jlimit(0, 3839, int(std::round(note.start * 480.0f)));
        const int lengthTicks = juce::jlimit(1, 3840 - startTick, int(std::round(note.duration * 480.0f)));
        note.start = float(startTick) / 480.0f;
        note.duration = float(lengthTicks) / 480.0f;
        result.push_back(note);
        note.start += 8.0f;
        result.push_back(note);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.start == b.start ? a.pitch < b.pitch : a.start < b.start;
    });
    return result;
}

bool writeMidi(const GeneratedClip& clip)
{
    juce::MidiFile midi; midi.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / clip.bpm)), 0.0);
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    track.addEvent(juce::MidiMessage::programChange(1, 81), 0.0);
    for (const auto& note : clip.notes)
    {
        const double start = std::round(note.start * 480.0);
        const double end = juce::jmin(7680.0, start + juce::jmax(1.0, std::round(note.duration * 480.0)));
        track.addEvent(juce::MidiMessage::noteOn(1, note.pitch, uint8_t(note.velocity)), start);
        track.addEvent(juce::MidiMessage::noteOff(1, note.pitch), end);
    }
    track.addEvent(juce::MidiMessage::endOfTrack(), 7680.0);
    midi.addTrack(track);
    juce::MemoryOutputStream stream;
    return midi.writeTo(stream) && clip.file.replaceWithData(stream.getData(), stream.getDataSize());
}
}

GeneratedClip parseMidi(const juce::File& file)
{
    auto stream = file.createInputStream();
    juce::MidiFile midi;
    if (!stream || !midi.readFrom(*stream, false) || midi.getTimeFormat() <= 0)
        throw std::runtime_error("Cannot read the generated MIDI.");
    GeneratedClip clip { file, {}, {}, {}, {}, 146.0, 0.0, 0 };
    const double tpq = midi.getTimeFormat();
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        std::map<std::pair<int, int>, std::deque<juce::MidiMessage>> active;
        const auto* track = midi.getTrack(t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& m = track->getEventPointer(i)->message;
            clip.beats = juce::jmax(clip.beats, m.getTimeStamp() / tpq);
            if (m.isNoteOn()) active[{m.getChannel(), m.getNoteNumber()}].push_back(m);
            else if (m.isNoteOff())
            {
                auto& queue = active[{m.getChannel(), m.getNoteNumber()}];
                if (queue.empty()) continue;
                auto start = queue.front(); queue.pop_front();
                clip.notes.push_back({m.getNoteNumber(), float(start.getTimeStamp() / tpq),
                    float((m.getTimeStamp() - start.getTimeStamp()) / tpq), start.getVelocity()});
            }
        }
        for (const auto& item : active)
            if (!item.second.empty()) throw std::runtime_error("Generated MIDI has an unfinished note.");
    }
    if (std::abs(clip.beats - 16.0) > 0.00001 || clip.notes.empty())
        throw std::runtime_error("Expected a non-empty four-bar MIDI.");
    for (const auto& n : clip.notes)
        if (n.start < 0 || n.duration <= 0 || n.start + n.duration > 16.0001f)
            throw std::runtime_error("Generated note is outside the four-bar phrase.");
    return clip;
}

void updateLivePreview(GenerationBatch& batch, LiveControls controls)
{
    for (auto& clip : batch.clips)
        clip.notes = transformedNotes(clip, controls);
}

void writeLiveMidi(GenerationBatch& batch, LiveControls controls)
{
    for (auto& clip : batch.clips)
    {
        const bool baseline = controls.mutation == 0.0f && controls.density == 1.0f;
        if (baseline && clip.baseFileData.getSize() != 0)
        {
            if (!clip.file.replaceWithData(clip.baseFileData.getData(), clip.baseFileData.getSize()))
                throw std::runtime_error("Could not restore the MIDI file.");
        }
        else if (!writeMidi(clip)) throw std::runtime_error("Could not update the MIDI file.");
    }
}

void removeOwnedBatch(const juce::File& batch, const juce::File& session)
{
    if (batch != juce::File() && batch.getParentDirectory() == session
        && batch.getFileName().startsWith("batch-"))
        batch.deleteRecursively();
}

GenerationBatch PythonGeneratorEngine::generate(juce::String targetKey, juce::File analysisFile, std::function<bool()> cancelled)
{
    auto batchFolder = sessionRoot.getChildFile("batch-" + juce::Uuid().toString());
    juce::ChildProcess process;
    juce::StringArray args;
    auto packaged = icepunkPackagedEngine(projectRoot);
    if (packaged.existsAsFile()) args.add(packaged.getFullPathName());
   #if ICEPUNK_DEVELOPMENT_ENGINE
    else
    {
        auto python = juce::SystemStats::getEnvironmentVariable("ICEPUNK_PYTHON", "python");
        auto bridge = projectRoot.getChildFile("prototype/bridge/generate.py");
        if (!bridge.existsAsFile()) throw std::runtime_error("Generator engine is missing. Please reinstall ICEPUNK.");
        args.addArray({python, "-B", bridge.getFullPathName()});
    }
   #else
    else
    {
        juce::Logger::writeToLog("Engine missing: " + packaged.getFullPathName());
        throw std::runtime_error("Generator engine is missing. Please reinstall ICEPUNK.");
    }
   #endif
    args.addArray({"--output-dir", batchFolder.getFullPathName()});
    if (targetKey.isNotEmpty()) args.addArray({"--target-key", targetKey});
    if (analysisFile != juce::File())
    {
        if (!analysisFile.existsAsFile()) throw std::runtime_error("Custom MIDI profile is missing. Analyze the folder again.");
        args.addArray({"--analysis-file", analysisFile.getFullPathName()});
    }
    juce::Logger::writeToLog("Starting engine: " + args[0]);
    if (!process.start(args))
    {
        juce::Logger::writeToLog("Engine process failed to start: " + args[0]);
        throw std::runtime_error("Cannot start the generator. Please reinstall ICEPUNK or check the application logs.");
    }
    try
    {
        auto started = juce::Time::getMillisecondCounterHiRes();
        while (process.isRunning())
        {
            if (cancelled() || juce::Time::getMillisecondCounterHiRes() - started > 30000)
            {
                process.kill(); process.waitForProcessToFinish(2000);
                throw std::runtime_error(cancelled() ? "Generation cancelled." : "Generation timed out. Please try again.");
            }
            juce::Thread::sleep(20);
        }
        auto text = process.readAllProcessOutput();
        juce::Logger::writeToLog("Engine exit=" + juce::String(process.getExitCode()) + " response=" + text.substring(0, 8192));
        auto response = juce::JSON::parse(text);
        if (process.getExitCode() != 0 || !bool(response["ok"]))
        {
            auto error = response["error"].toString();
            if (error.isEmpty()) error = "Generation failed. Please retry. Details are in the application logs.";
            throw std::runtime_error(error.toStdString());
        }
        auto* clips = response["clips"].getArray();
        if (clips == nullptr || clips->size() != 5) throw std::runtime_error("Expected exactly five MIDI files.");
        GenerationBatch batch { batchFolder, {} };
        for (int i = 1; i <= 5; ++i)
        {
            auto clip = parseMidi(batchFolder.getChildFile("midi_" + juce::String(i) + ".mid"));
            if (int(clip.notes.size()) != int((*clips)[i - 1]["notes"]))
                throw std::runtime_error("Preview note count differs from the generated MIDI.");
            clip.key = (*clips)[i - 1]["key"].toString();
            clip.bpm = double((*clips)[i - 1]["bpm"]);
            clip.baseNotes = clip.notes;
            clip.file.loadFileAsData(clip.baseFileData);
            clip.seed = 0x1ce00000U + uint32_t(i) * 0x9e3779b9U;
            batch.clips.push_back(std::move(clip));
        }
        return batch;
    }
    catch (const std::exception& error)
    {
        juce::Logger::writeToLog("Generation error: " + juce::String::fromUTF8(error.what()));
        removeOwnedBatch(batchFolder, sessionRoot);
        throw;
    }
}

void GenerationWorker::run()
{
    GenerationBatch batch;
    juce::String error;
    try { batch = engine.generate(target, profile, [this] { return threadShouldExit(); }); }
    catch (const std::exception& e) { error = juce::String::fromUTF8(e.what()); }
    if (!threadShouldExit())
        juce::MessageManager::callAsync([callback = complete, batch = std::move(batch), error]() mutable {
            callback(std::move(batch), error);
        });
}

DatasetAnalysis PythonGeneratorEngine::analyzeDataset(juce::File folder, juce::File profile, std::function<bool()> cancelled)
{
    if (!folder.isDirectory()) throw std::runtime_error("Choose a folder containing MIDI files.");
    juce::ChildProcess process;
    juce::StringArray args;
    auto packaged = icepunkPackagedEngine(projectRoot);
    if (packaged.existsAsFile()) args.add(packaged.getFullPathName());
   #if ICEPUNK_DEVELOPMENT_ENGINE
    else
    {
        auto python = juce::SystemStats::getEnvironmentVariable("ICEPUNK_PYTHON", "python");
        auto bridge = projectRoot.getChildFile("prototype/bridge/generate.py");
        if (!bridge.existsAsFile()) throw std::runtime_error("Generator engine is missing. Please reinstall ICEPUNK.");
        args.addArray({python, "-B", bridge.getFullPathName()});
    }
   #else
    else throw std::runtime_error("Generator engine is missing. Please reinstall ICEPUNK.");
   #endif
    args.addArray({"--analyze-dir", folder.getFullPathName(), "--profile-file", profile.getFullPathName()});
    if (!process.start(args)) throw std::runtime_error("Cannot start the custom dataset analyzer.");
    auto started = juce::Time::getMillisecondCounterHiRes();
    while (process.isRunning())
    {
        if (cancelled() || juce::Time::getMillisecondCounterHiRes() - started > 120000)
        {
            process.kill(); process.waitForProcessToFinish(2000);
            throw std::runtime_error(cancelled() ? "Analysis cancelled." : "Analysis timed out. Try a smaller folder.");
        }
        juce::Thread::sleep(30);
    }
    auto response = juce::JSON::parse(process.readAllProcessOutput());
    if (process.getExitCode() != 0 || !bool(response["ok"]))
    {
        auto error = response["error"].toString();
        throw std::runtime_error((error.isEmpty() ? "Custom dataset analysis failed." : error).toStdString());
    }
    DatasetAnalysis result;
    result.filesFound = int(response["files_found"]);
    result.filesAnalyzed = int(response["files_analyzed"]);
    result.filesFailed = int(response["files_failed"]);
    result.patterns = int(response["patterns"]);
    result.profile = profile;
    if (!profile.existsAsFile()) throw std::runtime_error("Custom profile was not saved.");
    return result;
}

void DatasetAnalysisWorker::run()
{
    DatasetAnalysis result;
    juce::String error;
    try { result = engine.analyzeDataset(folder, profile, [this] { return threadShouldExit(); }); }
    catch (const std::exception& e) { error = juce::String::fromUTF8(e.what()); }
    if (!threadShouldExit())
        juce::MessageManager::callAsync([callback = complete, result = std::move(result), error]() mutable {
            callback(std::move(result), error);
        });
}
