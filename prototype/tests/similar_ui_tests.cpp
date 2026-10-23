#include "MainComponent.h"
#include <iostream>
#include <stdexcept>
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    try
    {
        MainComponent component { juce::File(argv[1]) };
        GenerationBatch batch; batch.clips.resize(5);
        component.setBatchForTest(std::move(batch));
        check(component.generateTextForTest() == "GENERATE", "initial label");
        for (int i = 0; i < 3; ++i) component.toggleReferenceForTest(i);
        check(component.referenceCountForTest() == 3, "selection count");
        check(component.generateTextForTest() == "GENERATE SIMILAR", "similar label");
        component.toggleReferenceForTest(3);
        check(component.referenceCountForTest() == 3, "fourth reference accepted");
        component.toggleReferenceForTest(1);
        check(component.referenceCountForTest() == 2, "deselect failed");
        component.clearReferencesForTest();
        check(component.referenceCountForTest() == 0 && component.generateTextForTest() == "GENERATE", "clear failed");
        std::cout << "PASS selection limit, deselection, clear and button labels\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
