#include "GeneratorEngine.h"
#include "AppConfig.h"
#include "RuntimePaths.h"
#include <map>
#include <deque>
#include <stdexcept>

GeneratedClip parseMidi(const juce::File& file)
{
    auto stream = file.createInputStream();
    juce::MidiFile midi;
    if (!stream || !midi.readFrom(*stream, false) || midi.getTimeFormat() <= 0)
        throw std::runtime_error("Cannot read the generated MIDI.");
    GeneratedClip clip { file, {}, {}, 0.0 };
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

void removeOwnedBatch(const juce::File& batch, const juce::File& session)
{
    if (batch != juce::File() && batch.getParentDirectory() == session
        && batch.getFileName().startsWith("batch-"))
        batch.deleteRecursively();
}

GenerationBatch PythonGeneratorEngine::generate(juce::String targetKey, std::function<bool()> cancelled)
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
    try { batch = engine.generate(target, [this] { return threadShouldExit(); }); }
    catch (const std::exception& e) { error = juce::String::fromUTF8(e.what()); }
    if (!threadShouldExit())
        juce::MessageManager::callAsync([callback = complete, batch = std::move(batch), error]() mutable {
            callback(std::move(batch), error);
        });
}
