#include "GeneratorEngine.h"
#include <iostream>
#include <stdexcept>
#include <cmath>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static bool same(const PreviewNote& a, const PreviewNote& b, float offset = 0)
{ return a.pitch == b.pitch && a.velocity == b.velocity && std::abs(a.start + offset - b.start) < 0.0001f && std::abs(a.duration - b.duration) < 0.0001f; }
int main()
{
    try
    {
        const juce::StringArray keys {"C major", "F# minor", "Bb major"};
        // The engine uses sharp spellings for black keys.
        for (auto key : keys)
        {
            if (key == "Bb major") key = "A# major";
            for (int seed = 0; seed < 50; ++seed)
            {
                GeneratedClip clip; clip.key = key; clip.seed = uint32_t(seed); clip.beats = 16;
                const int root = key == "C major" ? 60 : key == "F# minor" ? 54 : 58;
                clip.baseNotes = {{root,0,1,90},{root+7,0,1,95},{root+12,2,1,100},{root+7,7.75f,0.25f,90}};
                GenerationBatch batch {{}, {clip}};
                updateLivePreview(batch, {1.0f, 1.0f, 1.0f});
                auto result = batch.clips[0].notes;
                require(result.size() == 8, "note count");
                require(std::abs(result.front().start) < 0.0001f, "mutation delayed the first note");
                require(std::abs(result[1].start) < 0.0001f, "mutation moved part of the first chord");
                for (size_t i = 0; i < 4; ++i) require(same(result[i], result[i+4], 8.0f), "repeat differs");
                for (size_t i = 0; i < 4; ++i)
                {
                    const auto& n = result[i];
                    require(n.start >= 0 && n.start + n.duration <= 8.0001f, "boundary");
                    const int degree = (n.pitch - root + 120) % 12;
                    const bool minor = key.endsWith("minor");
                    const int scaleMajor[] = {0,2,4,5,7,9,11}; const int scaleMinor[] = {0,2,3,5,7,8,10};
                    const int* scale = minor ? scaleMinor : scaleMajor;
                    require(std::find(scale, scale + 7, degree) != scale + 7, "out of key");
                }
                updateLivePreview(batch, {1.0f, 1.0f, 1.0f});
                for (size_t i = 0; i < result.size(); ++i) require(same(result[i], batch.clips[0].notes[i]), "not deterministic");
                updateLivePreview(batch, {0.0f, 0.5f, 1.0f});
                for (size_t i = 0; i < 4; ++i) require(same(clip.baseNotes[i], batch.clips[0].notes[i]), "baseline changed");
            }
        }
        const auto session = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("icepunk-similar-test-" + juce::Uuid().toString());
        require(session.createDirectory().wasOk(), "session create");
        GenerationBatch original {session.getChildFile("batch-original"), {}};
        require(original.folder.createDirectory().wasOk(), "batch create");
        for (int i = 0; i < 5; ++i)
        {
            GeneratedClip c; c.file = original.folder.getChildFile("midi_" + juce::String(i+1) + ".mid");
            c.key = "C major"; c.bpm = 140; c.beats = 16; c.seed = uint32_t(i+1);
            c.baseNotes = {{60+i*2,1.5f,1,90},{64+i*2,2,1,95},{67+i*2,4,1,100},{72+i*2,7,0.5f,90}};
            GenerationBatch one {original.folder, {c}};
            updateLivePreview(one, {0,0.5f,1}); writeLiveMidi(one, {0,0.5f,1});
            c = one.clips[0]; require(c.file.loadFileAsData(c.baseFileData), "source read");
            original.clips.push_back(c);
        }
        for (int count = 1; count <= 3; ++count)
        {
            std::array<bool, 5> selected {};
            for (int i = 0; i < count; ++i) selected[size_t(i * 2)] = true;
            auto source = original;
            for (int round = 0; round < 3; ++round)
            {
                auto result = generateSimilarBatch(source, selected, {0.4f,0.6f,1}, session, [] {return false;});
                require(result.clips.size() == 5, "batch size");
                for (int i = 0; i < 5; ++i)
                {
                    auto parsed = parseMidi(result.clips[size_t(i)].file);
                    require(parsed.beats == 16 && !parsed.notes.empty(), "invalid output");
                    juce::MemoryBlock oldData, newData;
                    require(source.clips[size_t(i)].file.loadFileAsData(oldData) && result.clips[size_t(i)].file.loadFileAsData(newData), "read files");
                    if (selected[size_t(i)]) require(oldData == newData, "selected file changed");
                    else
                    {
                        require(oldData != newData, "unselected file unchanged");
                        require(std::abs(parsed.notes.front().start) < 0.0001f, "similar MIDI starts late");
                        GenerationBatch live {result.folder, {result.clips[size_t(i)]}};
                        updateLivePreview(live, {1.0f, 1.0f, 1.0f});
                        require(!live.clips[0].notes.empty() && std::abs(live.clips[0].notes.front().start) < 0.0001f,
                                "live controls delayed similar MIDI");
                        const auto& notes = live.clips[0].notes;
                        require(notes.size() % 2 == 0, "repeat note count");
                        for (size_t n = 0; n < notes.size()/2; ++n)
                            require(same(notes[n], notes[n + notes.size()/2], 8.0f), "shift broke repeat");
                    }
                }
                if (round > 0) removeOwnedBatch(source.folder, session);
                source = std::move(result);
            }
            removeOwnedBatch(source.folder, session);
        }
        bool rejected = false;
        try { generateSimilarBatch(original, {true,true,true,true,false}, {0,0,1}, session, [] {return false;}); }
        catch (...) { rejected = true; }
        require(rejected, "four references accepted");
        session.deleteRecursively();
        std::cout << "PASS similar generation with 1/2/3 references, repeat, unchanged selected files and validation\n";
        std::cout << "PASS live controls: 150 combinations, scale, timing, repeats, deterministic restoration\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
