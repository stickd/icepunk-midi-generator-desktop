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

bool isPitchInKey(int pitch, const juce::String& key)
{
    const int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    const int* scale = isMinor(key) ? minor : major;
    const int degree = (pitch - keyRoot(key) + 120) % 12;
    return std::find(scale, scale + 7, degree) != scale + 7;
}

int moveDiatonic(int pitch, int direction, int steps, const juce::String& key)
{
    // Keep bass, middle and high notes near their original register. Walking
    // scale degrees avoids the wide chromatic jumps of the old control.
    const int lower = juce::jmax(36, pitch - 7);
    const int upper = juce::jmin(84, pitch + 7);
    int result = pitch;
    while (steps > 0)
    {
        int next = result + direction;
        while (next >= lower && next <= upper && !isPitchInKey(next, key)) next += direction;
        if (next < lower || next > upper) break;
        result = next;
        --steps;
    }
    return result;
}

std::vector<PreviewNote> transformedNotes(const GeneratedClip& clip, LiveControls controls)
{
    std::vector<PreviewNote> result;
    const auto& source = clip.baseNotes.empty() ? clip.notes : clip.baseNotes;
    controls.mutation = juce::jlimit(0.0f, 1.0f, controls.mutation);
    controls.variation = juce::jlimit(0.0f, 1.0f, controls.variation);
    controls.density = juce::jlimit(0.0f, 1.0f, controls.density);
    float firstOnset = 8.0f;
    for (const auto& note : source)
        if (note.start >= 0.0f && note.start < 8.0f && note.duration > 0.0f)
            firstOnset = juce::jmin(firstOnset, note.start);
    bool keptFirst = false;
    for (size_t i = 0; i < source.size(); ++i)
    {
        const auto& base = source[i];
        if (base.start < 0.0f || base.start >= 8.0f || base.duration <= 0.0f) continue;
        // Thin one two-bar phrase, then repeat it exactly. Keep at least one note.
        if (keptFirst && stableUnit(clip.seed, uint32_t(i), 11) >= controls.density) continue;
        keptFirst = true;
        auto note = base;
        // Notes sharing an onset use the same musical decision, keeping
        // chords together. Seeds remain fixed while controls are adjusted.
        const auto onset = uint32_t(juce::jmax(0, int(std::round(base.start * 480.0f))));
        if (stableUnit(clip.seed, onset, 21) < controls.mutation)
        {
            const int direction = stableUnit(clip.seed, onset, 22) < 0.5f ? -1 : 1;
            const int maxSteps = 1 + int(controls.variation * 2.99f);
            const int steps = 1 + int(stableUnit(clip.seed, onset, 23) * 0.9999f * maxSteps);
            note.pitch = moveDiatonic(base.pitch, direction, steps, clip.key);
        }
        if (std::abs(base.start - firstOnset) > 1.0f / 960.0f
            && stableUnit(clip.seed, onset, 31) < controls.mutation * controls.variation * 0.45f)
        {
            const float direction = stableUnit(clip.seed, onset, 32) < 0.5f ? -1.0f : 1.0f;
            // Move at most one sixteenth; do not quantize unchanged source timing.
            note.start = juce::jlimit(0.0f, 7.75f, base.start + direction * 0.25f);
            note.duration = juce::jmin(base.duration, 8.0f - note.start);
        }
        if (stableUnit(clip.seed, onset, 41) < controls.mutation * 0.8f)
        {
            const int amount = 2 + int(stableUnit(clip.seed, uint32_t(i), 43) * (3.0f + controls.variation * 9.0f));
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
    // Similar variants must begin immediately even when a reference starts late
    // or live mutation shifts its first attack. Move both repetitions equally.
    if (clip.alignStart && !result.empty())
    {
        const float first = result.front().start;
        for (auto& note : result) note.start -= first;
    }
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
            clip.seed = uint32_t(juce::Random::getSystemRandom().nextInt());
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

GenerationBatch generateSimilarBatch(const GenerationBatch& source, const std::array<bool, 5>& selected,
                                     LiveControls controls, const juce::File& session, std::function<bool()> cancelled)
{
    if (source.clips.size() != 5) throw std::runtime_error("Generate five patterns first.");
    std::vector<int> references;
    for (int i = 0; i < 5; ++i) if (selected[size_t(i)]) references.push_back(i);
    if (references.empty() || references.size() > 3) throw std::runtime_error("Select one to three references.");
    auto folder = session.getChildFile("batch-" + juce::Uuid().toString());
    if (folder.createDirectory().failed()) throw std::runtime_error("Cannot create the output folder.");
    GenerationBatch result {folder, {}};
    try
    {
        int nextReference = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (cancelled()) throw std::runtime_error("Generation cancelled.");
            const int ref = selected[size_t(i)] ? i : references[size_t(nextReference++ % int(references.size()))];
            auto clip = source.clips[size_t(ref)];
            auto destination = folder.getChildFile("midi_" + juce::String(i + 1) + ".mid");
            if (selected[size_t(i)])
            {
                if (!clip.file.copyFileTo(destination)) throw std::runtime_error("Cannot preserve selected MIDI.");
                clip.file = destination;
            }
            else
            {
                clip.file = destination;
                clip.alignStart = true;
                // Work from the actual selected notes, including any live edits.
                clip.baseNotes = clip.notes;
                clip.baseFileData.reset();
                bool changed = false;
                for (int attempt = 0; attempt < 24 && !changed; ++attempt)
                {
                    clip.seed = uint32_t(juce::Random::getSystemRandom().nextInt());
                    LiveControls similarity {juce::jmax(0.45f, controls.mutation),
                                             juce::jmax(0.25f, controls.variation), 1.0f};
                    clip.notes = transformedNotes(clip, similarity);
                    // Vary a few note lengths while respecting the two-bar seam;
                    // apply the same change to both repetitions of the phrase.
                    for (auto& n : clip.notes)
                    {
                        const float localStart = n.start >= 8.0f ? n.start - 8.0f : n.start;
                        const auto onset = uint32_t(std::round(localStart * 480.0f));
                        if (stableUnit(clip.seed, onset, 71) < similarity.mutation * similarity.variation * 0.3f)
                        {
                            const float factor = stableUnit(clip.seed, onset, 72) < 0.5f ? 0.75f : 1.25f;
                            n.duration = std::round(juce::jmax(1.0f / 480.0f, juce::jmin(n.duration * factor, 8.0f - localStart)) * 480.0f) / 480.0f;
                        }
                    }
                    // Ensure at least one pitch or onset differs, not just velocity.
                    if (clip.notes.size() != clip.baseNotes.size()) changed = true;
                    else for (size_t n = 0; n < clip.notes.size(); ++n)
                        if (clip.notes[n].pitch != clip.baseNotes[n].pitch
                            || std::abs(clip.notes[n].start - clip.baseNotes[n].start) > 0.001f)
                        { changed = true; break; }
                }
                if (!changed) throw std::runtime_error("Could not create a different variation. Try another reference.");
                if (!writeMidi(clip)) throw std::runtime_error("Could not save similar MIDI.");
                auto checked = parseMidi(clip.file);
                if (checked.notes.size() != clip.notes.size()) throw std::runtime_error("Invalid similar MIDI.");
                clip.baseNotes = clip.notes;
                if (!clip.file.loadFileAsData(clip.baseFileData)) throw std::runtime_error("Could not read similar MIDI.");
            }
            result.clips.push_back(std::move(clip));
        }
        return result;
    }
    catch (...) { removeOwnedBatch(folder, session); throw; }
}

void GenerationWorker::run()
{
    GenerationBatch batch;
    juce::String error;
    try { batch = similar ? generateSimilarBatch(references, selection, controls, sessionRoot, [this] { return threadShouldExit(); })
                          : engine.generate(target, profile, [this] { return threadShouldExit(); }); }
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
