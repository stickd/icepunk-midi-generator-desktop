# ICEPUNK prototype integration plan

The working root generator and analysis JSON are the source of truth and remain unmodified.
The root CLI accepts an optional output directory and normally produces ten files. Its
`generate_best_notes(patterns, style, attempts=50)` and `write_midi` functions also work independently.

1. JUCE C++ standalone shell with one Generate button and five draggable piano-roll rows.
2. A local Python subprocess loads the original module and the current analysis JSON. It calls the
   existing generator exactly five times, writes five files, and returns a small versioned JSON response.
3. A background worker invokes this bridge. The GUI thread receives only a completed, validated batch.
4. JUCE parses actual MIDI notes/ticks for the five previews; all use a sixteen-beat timeline.
5. Use JUCE native file drag-and-drop with copy semantics and a small per-row Export fallback.
6. Keep current and previous batches in a unique marked session directory; remove owned temporary data on exit.
7. Test MIDI invariants, unchanged generator behavior, failure preservation, repeated generation and native parsing.

User-requested follow-up: after each original generation, clip the first two bars at beat 8
and repeat them at offset +8 into bars 3–4. Preserve internal bar-crossing notes and all
pitch/velocity values. This postprocessing lives in the bridge, leaving the original generator intact.

No BPM/key/bars controls. No embedded Python interpreter, audio engine, network service or VST3 packaging in V1.
