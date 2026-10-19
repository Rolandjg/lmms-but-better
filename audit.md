# LMMS But Better — program audit

Audited October 18, 2026, at commit `fcf3614f2`.

**Overall assessment:** This fork adds useful music-making tools, particularly the command palette, MIDI capture, clip fades, markers, scripting, and bounce. There is substantial automated coverage, and the default theme is more visually consistent. The biggest weaknesses are reliability at workflow boundaries—saving, rendering, switching between GUI and headless use—and accessibility of the custom controls. Prioritize those before another round of features or visual redesign.

This is a prioritized audit, not an implementation plan. Each finding describes the problem, evidence, and the outcome worth prioritizing. Suggested features are identified separately from bugs.

## Scope and confidence

Reviewed project persistence, audio rendering and bounce, scripting/server dispatch, MIDI capture, command palette, timeline markers, shared widgets, selected DSP/plugin integration, tests, and CI configuration. Rendered and inspected the main window, Song Editor, sample clips, mixer, settings, Piano Roll, and an instrument window with the repository's `UiSnapshot` tool.

Validation performed:

- **23/23 CTest test executables passed** using `QT_QPA_PLATFORM=offscreen ctest --test-dir build/tests --output-on-failure -j2` (about 21 seconds). A subsequent Ninja dry run confirmed those test targets were current.
- `tests/scripted/check-strings`: **0 errors**.
- `tests/scripted/check-namespace`: **102 reported errors**, discussed below.
- `ctest --test-dir build`: **no tests discovered**, despite tests existing in `build/tests`.
- A temporary C++ probe linked against the current application objects reproduced the unnamed-save problem, false save success, missing markers in headless saves, renderer range/tail leakage, and process termination on a headless export-open error. It used a temporary directory and private configuration.
- Offscreen snapshots were generated from an empty project and the bundled `editing_note_volumes.mmp` tutorial. The tutorial produced a project-load warning; its snapshots are used for layout review, not proof of complete plugin compatibility.

**Limits:** This was a representative source and local runtime audit, not an exhaustive review of every plugin or dependency. No live screen-reader, physical MIDI device, real audio-device latency, commercial VST compatibility, Windows, or macOS testing was performed. Findings distinguish reproduced behavior, source-established problems, risks needing runtime validation, and product recommendations. Existing user project/audio files were left untouched; no application fixes are included.

## Priority order

Scores express recommended urgency, not a numerical measurement of code quality. **10 = address immediately; 8–9 = next reliability/accessibility milestone; 6–7 = next improvement cycle; 4–5 = follow-up.** Within a score, the table order is the suggested order of work. Some accessibility work can proceed independently of persistence fixes.

| Order / ID | Score | Category | Finding | Evidence strength |
|---|---:|---|---|---|
| 01 | 10/10 | Bug | Saving can report success without replacing the destination | Reproduced |
| 02 | 9/10 | Bug | Bounce on an unnamed project saves to a hidden file without Save As | Save path reproduced; bounce call verified in source |
| 03 | 9/10 | Bug | Headless project handling drops arrangement markers | Save omission reproduced; load omission verified in source |
| 04 | 9/10 | Bug | Export errors have inconsistent and unsafe completion behavior | Headless termination reproduced; other paths verified in source |
| 05 | 8/10 | Audio correctness / UX | Bounced audio is not a transparent replacement for its source | Signal path verified in source |
| 06 | 8/10 | Accessibility | Core parameter controls cannot be operated fully by keyboard | Shared implementation verified in source |
| 07 | 8/10 | Accessibility | Custom controls lack useful screen-reader semantics | Source gap; assistive-technology testing still needed |
| 08 | 7/10 | Reliability risk | Script clients can mutate a project during asynchronous rendering | Source risk; crash not reproduced |
| 09 | 7/10 | Missing capability | Plugin latency is not adequately handled across signal paths | Source gap |
| 10 | 7/10 | Concurrency bug | Audio/UI sharing includes unsynchronized data | Source-established races; symptoms not reproduced |
| 11 | 7/10 | Accessibility / UI | Small targets and fixed-size text make editing unnecessarily difficult | Snapshots and source |
| 12 | 7/10 | Engineering improvement | The namespace check is currently failing noisily | Reproduced |
| 13 | 6/10 | Feature gap | MIDI Capture does not watch Pattern Editor instrument tracks | Source-established coverage gap |
| 14 | 6/10 | Accessibility | Several marker labels have poor text contrast | Calculated from source colors; visible in snapshots |
| 15 | 6/10 | Confusing UI | Retrospective MIDI Capture looks like a record button | Snapshot and toolbar source |
| 16 | 6/10 | Engineering improvement | Standard build-root test discovery finds zero tests | Reproduced |
| 17 | 6/10 | Suggested feature | A project health/resource overview would reduce portability surprises | Product recommendation |
| 18 | 6/10 | Suggested feature | Freeze/unfreeze would complete the bounce workflow | Product recommendation |
| 19 | 6/10 | Suggested feature | Script runs need a coherent recovery/undo workflow | Source and product recommendation |
| 20 | 5/10 | UI improvement | Modern widgets hard-code their own palette | Source-established limitation |
| 21 | 4/10 | Internal renderer bug | Range rendering with tails can schedule later clips | Reproduced; current GUI avoids the problematic combination |

## Findings

### 01 — Saving can report success when the final file was not saved — 10/10

After committing a `.new` file, `DataFile::writeFile()` removes/renames the previous project and renames the new file into place without checking those results. It then returns `true`. The GUI can consequently clear the modified flag even though the requested destination was not updated. There is also a gap between moving/removing the old destination and publishing the new one.

**Reproduction:** In an isolated directory, make `blocked.mmp` a directory and create a nonempty `blocked.mmp.bak` directory to prevent the backup rename. Saving returns `true`; `blocked.mmp` remains a directory, and the new content remains in `blocked.mmp.new`.

**Prioritize:** Trustworthy save success and preservation of the last good project. This comes before convenience features because users rely on the saved indicator to close their work safely.

Evidence: [DataFile.cpp](src/core/DataFile.cpp), `writeFile()`; [Song.cpp](src/core/Song.cpp), `guiSaveProjectAs()`.

### 02 — Unnamed bounce bypasses Save As — 9/10

`bounceToAudio()` calls `Song::guiSaveProject()` when the project filename is empty. That function writes directly; it is not the main window's Save As flow. `nameWithExtension("")` becomes `.mmpz` under the default configuration. The project and bounced audio therefore land in the process working directory without the user choosing a location. Repeating this in fresh sessions can reuse the same hidden project name.

**Reproduction:** Calling the exact save method used by bounce on an unnamed song returned `true`, changed its filename to `.mmpz`, and created that hidden file in the probe's temporary working directory. The interactive bounce itself was not automated.

**Prioritize:** An explicit project destination before bouncing an unnamed song. A canceled destination choice should leave the project untouched.

Evidence: [BounceToAudio.cpp](src/gui/BounceToAudio.cpp), initial save check; [Song.cpp](src/core/Song.cpp), `guiSaveProject()`; compare [MainWindow.cpp](src/gui/MainWindow.cpp), `saveProject()`.

### 03 — Headless project handling loses arrangement markers — 9/10

The Song timeline is saved only inside `if (getGUI() != nullptr)`. Loading its state is also inside a GUI-only branch. Markers are musical project data, yet the advertised headless scripting workflow does not preserve them. A script that opens and resaves an arranged project can remove its section structure. Loop/timeline state shares the same persistence path.

**Reproduction:** Added a named marker to the live Song timeline and saved headlessly. The resulting project did not contain the marker name.

**Prioritize:** Equivalent preservation of musical project state in GUI and headless workflows, including open/save round trips.

Evidence: [Song.cpp](src/core/Song.cpp), `loadProject()` and `saveProjectFile()`; [Timeline.cpp](src/core/Timeline.cpp), `saveSettings()`.

### 04 — Export failures need one reliable error contract — 9/10

An output-file open failure calls `exit(EXIT_FAILURE)` when no GUI exists. That is especially problematic for a persistent script server: one bad export destination terminates the application instead of returning an API error. In the GUI, an open error does display a warning, but `RenderManager` then advances toward its ordinary `finished` signal. WAV buffer writes also ignore the number of frames actually written, leaving mid-export write failures insufficiently checked.

**Reproduction:** A headless `song.render` request targeting an existing directory named `blocked.wav` terminated the probe with exit status 1 before its API reply callback ran. Disk-full behavior was not injected.

**Prioritize:** Failed, canceled, and successful exports must have distinct outcomes; failed exports should preserve the editing/server session and clearly identify incomplete files.

Evidence: [AudioFileDevice.cpp](src/core/audio/AudioFileDevice.cpp), constructor; [AudioFileWave.cpp](src/core/audio/AudioFileWave.cpp), `writeBuffer()`; [RenderManager.cpp](src/core/RenderManager.cpp), `render()`; [ScriptApi.cpp](src/core/scripting/ScriptApi.cpp), `song.render`.

### 05 — Bounce changes playback level/routing unless the user compensates — 8/10

Bounce captures the full engine output, including downstream mixer/master processing and master gain, then inserts a sample track routed to mixer channel 0. Playback of that sample passes through the master path again. Even after manually muting the source, a 50% master gain is applied once to the render and again to playback. Nonlinear master effects make the mismatch more complicated. The original source is also restored to its prior mute state, so the default result plays both source and bounce together.

**Prioritize:** Make the intended bounce signal point and source behavior clear and musically predictable. Replacement-style bounce should preserve what the user hears. Offering an additional rendered layer can remain a separate, explicit choice.

Evidence: [BounceToAudio.cpp](src/gui/BounceToAudio.cpp); [AudioEngine.cpp](src/core/AudioEngine.cpp), master-gain processing; [SampleTrack.cpp](src/tracks/SampleTrack.cpp), default mixer routing. This was traced in source, not measured in an end-to-end bounce comparison.

### 06 — Core parameters exclude keyboard-only operation — 8/10

`FloatModelEditorBase`, used by knobs and related controls, sets `Qt::ClickFocus` and implements mouse interaction without equivalent value-adjustment key handlers. These controls are skipped by normal Tab navigation. The numeric-entry action exists, but its mouse-oriented discovery does not provide a complete keyboard path. Sample fade/gain editing similarly relies on dragging handles.

**Prioritize:** A keyboard path for reaching, reading, adjusting, and resetting shared parameter controls. Fixing the shared components benefits many instruments and effects at once.

Evidence: [FloatModelEditorBase.cpp](src/gui/widgets/FloatModelEditorBase.cpp), `initUi()` and event handlers; [FloatModelEditorBase.h](include/FloatModelEditorBase.h); [SampleClipView.cpp](src/gui/clips/SampleClipView.cpp).

### 07 — Custom-drawn controls need meaningful accessibility semantics — 8/10

Knob labels, values, timeline markers, and note graphics are painted rather than represented as standard accessible controls. No application-owned `QAccessible` adapters or explicit accessible-name setup were found in the reviewed core GUI code. A visible label or tooltip does not expose a usable parameter value/range or an individual note/marker. Standard Qt widgets may still work with assistive technology; this is not a claim that the entire application is inaccessible.

**Prioritize:** Accessible names, roles, values, actions, and change announcements for shared controls, followed by navigable representations of notes, clips, and markers. Validate with an actual screen reader before considering this complete.

Evidence: [Knob.cpp](src/gui/widgets/Knob.cpp); [ModernWidgets.cpp](src/gui/widgets/ModernWidgets.cpp); [TimeLineWidget.cpp](src/gui/editors/TimeLineWidget.cpp). The command palette is a useful starting point for keyboard access, but it cannot substitute for accessible editing controls.

### 08 — Scripts can alter the project while a render is in flight — 7/10

Rendering is asynchronous and only a second `song.render` request checks `isExporting()`. General dispatch still accepts operations such as `song.new`, `track.delete`, and mixer changes. A second client—or another request on the connection—can therefore change objects while the renderer and stem queue are using them. `RenderManager` retains raw track pointers across stem renders.

**Prioritize:** A defined render-time mutation policy and stable object lifetimes. This is a source-level reliability risk; a crash or corrupted render was not reproduced, so validate the interaction before assigning a specific failure mode.

Evidence: [ScriptApi.cpp](src/core/scripting/ScriptApi.cpp), `call()`, `song.render`, and mutation handlers; [RenderManager.cpp](src/core/RenderManager.cpp), track queue; [ScriptServer.cpp](src/core/scripting/ScriptServer.cpp), request dispatch.

### 09 — Latency handling should precede more mastering effects — 7/10

The Limiter intentionally delays audio and exposes DSP latency internally. Its dry path is aligned locally, which is good, but there is no corresponding host-wide compensation path in the reviewed effect/mixer integration. VST3 explicitly leaves `kLatencyChanged` unhandled. Parallel or layered signals with different plugin delays can therefore become misaligned; offline rendering needs the same accounting.

**Prioritize:** Correct timing across tracks, buses, and exports, including changes to a plugin's latency. This is more valuable than adding further lookahead/oversampling effects before their timing is accounted for.

Evidence: [LimiterDsp.h](plugins/Limiter/LimiterDsp.h), `latency()`; [Limiter.cpp](plugins/Limiter/Limiter.cpp), dry delay; [Vst3Plugin.cpp](plugins/Vst3Base/Vst3Plugin.cpp), restart handling; [Mixer.cpp](src/core/Mixer.cpp). Audible impact was not benchmarked.

### 10 — Audio/UI concurrency relies on unsafe assumptions — 7/10

`ScopeBuffer` writes plain float arrays on the audio thread while UI paint code reads them. Making the ring index atomic does not synchronize concurrent access to those array elements. The comment that torn reads are harmless does not make these accesses safe under the C++ memory model. Separately, renderer progress/cancel fields are `volatile`, rather than synchronized cross-thread state.

**Prioritize:** Safe audio-to-UI snapshots and reliable cancellation without adding blocking work to the audio callback. No crash or missed cancellation was observed; the finding is the data race itself.

Evidence: [ModernWidgets.h](include/ModernWidgets.h), `ScopeBuffer`; [ModernWidgets.cpp](src/gui/widgets/ModernWidgets.cpp), `ModernStereoScope::paintEvent()`; [ProjectRenderer.h](include/ProjectRenderer.h), `m_progress`/`m_abort`; [ProjectRenderer.cpp](src/core/ProjectRenderer.cpp).

### 11 — Editing targets and text are too small in important places — 7/10

Sample handles are drawn at 8 pixels across, with hit detection limited to a 12-pixel-high top band and roughly 14 pixels horizontally. The gain handle disappears on short clips. Timeline labels use a fixed 10-pixel font, and modern knob labels also use a fixed small-font helper. Snapshots show substantial differences between readable settings/menu text and tiny editor/instrument labels. Display scaling alone does not provide independently adjustable text or comfortable editing targets.

**Prioritize:** Comfortable control sizing, scalable labels, and a keyboard/numeric clip-properties path. Start with frequent operations—clip gain/fades, mixer controls, timeline markers—before low-use decorative elements.

Evidence: [SampleClipView.cpp](src/gui/clips/SampleClipView.cpp), handle constants and `handleAt()`; [TimeLineWidget.cpp](src/gui/editors/TimeLineWidget.cpp), `markerFont()`; [Knob.cpp](src/gui/widgets/Knob.cpp), `updateFixedSize()`; inspected UI snapshots.

### 12 — Namespace checking currently produces 102 failures — 7/10

The local check reports 102 errors. Most concern bundled Steinberg VST3 SDK files being treated as LMMS-owned source; others concern fork-owned files, including `BounceToAudio.h` and VST3 platform code. This same command is invoked by the checks workflow. These are not 102 runtime bugs, but a noisy failing gate makes genuine regressions harder to distinguish.

**Prioritize:** A meaningful, passing check that separates third-party code from application conventions. Preserve the checker rather than normalizing permanent red results.

Evidence: [check-namespace](tests/scripted/check-namespace), exclusions; [checks.yml](.github/workflows/checks.yml); local execution.

### 13 — MIDI Capture misses Pattern Editor instruments — 6/10

`MidiCapture` watches tracks added to the Song container and existing Song tracks. It does not subscribe to the Pattern Store's instrument tracks. That narrows the README's broad expectation that LMMS remembers what was just played, particularly for users composing through the Pattern Editor.

**Prioritize:** Consistent capture coverage across supported instrument locations, with clear feedback about which track/take will be recovered. Physical-device behavior was not tested.

Evidence: [MidiCapture.cpp](src/gui/MidiCapture.cpp), constructor and `watchTrack()`; [README.md](README.md), MIDI Capture description.

### 14 — Marker labels have insufficient contrast on several presets — 6/10

Marker text color is chosen using a grayscale threshold rather than actual text/background contrast. With the supplied colors and current white-text selection, calculated contrast is approximately **3.23:1 for Intro, 2.32:1 for Verse, and 3.79:1 for Chorus**. These are weak combinations for the small, 10-pixel labels. The labels help users avoid relying solely on color, but they must be readable.

**Prioritize:** Readable marker text for every preset, hover state, and custom color. A practical acceptance target is at least 4.5:1 for this small text; this audit is not a formal accessibility-conformance certification.

Evidence: [TimeLineWidget.cpp](src/gui/editors/TimeLineWidget.cpp), preset colors, `markerFont()`, and `paintMarkers()`. Ratios were calculated locally from the specified sRGB colors.

### 15 — The Capture MIDI button looks like ordinary recording — 6/10

The toolbar uses a small REC-style icon for retrospective capture, and it is disabled until a take is available. A new user can reasonably interpret this as a broken or unavailable record control. The tooltip explains the feature well, but requires hover and prior curiosity. Recording, capture, and Piano Roll recording already have distinct behavior that deserves distinct presentation.

**Prioritize:** Make “recover what I just played” visibly distinct from “start recording now,” and show why capture is unavailable or what it will recover.

Evidence: [MainWindow.cpp](src/gui/MainWindow.cpp), `captureButton`; inspected main-window snapshot; [README.md](README.md).

### 16 — Build-root CTest discovery silently finds no tests — 6/10

CTest is included in the tests subdirectory, while the top-level build does not establish a root test tree. Running the conventional build-root command prints “No tests were found.” CI uses `build/tests`, so this does **not** mean CI skips the suite; it means local developers and generic tooling can mistakenly perform a test-free validation.

**Prioritize:** Reliable default discovery and an obvious documented test command. Add regression coverage for the persistence/export boundaries above; the passing suite currently does not protect those cases.

Evidence: [CMakeLists.txt](CMakeLists.txt), tests inclusion; [tests/CMakeLists.txt](tests/CMakeLists.txt), `include(CTest)`; [build.yml](.github/workflows/build.yml); local execution.

### 17 — Suggested feature: project health and resource overview — 6/10

The program already supports project bundles and reports some load errors. Build on those capabilities with one reviewable view of required samples/plugins, missing dependencies, external paths, and generated bounce files. Include an obvious way to prepare a portable project. This would help users understand what must accompany a project when moving it to another machine.

**Priority rationale:** Useful after save/bounce correctness is fixed. It addresses practical project longevity, rather than adding another sound generator. This is a product recommendation, not a claim that bundling is absent.

Evidence/context: [DataFile.cpp](src/core/DataFile.cpp), bundle/resource handling; [BounceToAudio.cpp](src/gui/BounceToAudio.cpp), project-adjacent generated audio; [Song.cpp](src/core/Song.cpp), load error reporting.

### 18 — Suggested feature: reversible freeze/unfreeze — 6/10

Current bounce creates an additional sample track, but does not provide a managed relationship between the source and rendered result. A freeze workflow could let users save CPU, audition a replacement, return to editable MIDI/plugins, and regenerate changed material without manually managing duplicate tracks and files.

**Priority rationale:** Add this after fixing bounce level/routing and save location. A reversible workflow has substantially more value than adding another rendering shortcut on top of ambiguous playback behavior.

Evidence/context: [BounceToAudio.cpp](src/gui/BounceToAudio.cpp).

### 19 — Suggested feature: recoverable script edits — 6/10

The scripting API is powerful enough to replace projects and make many edits rapidly. Its batch operation executes individual commands and returns per-command results; it is not an all-or-nothing transaction. A failed batch can leave a partially edited project. Existing per-object journalling is not equivalent to one coherent “undo this script run” action.

**Prioritize:** A comprehensible checkpoint/undo story for a script or batch, and clear reporting of partial success. This particularly benefits the advertised agent-driven workflow. It should not require every successful script to trigger a confirmation dialog.

Evidence: [ScriptApi.cpp](src/core/scripting/ScriptApi.cpp), `api.batch` and mutation handlers; [ScriptConsole.cpp](src/gui/ScriptConsole.cpp).

### 20 — Modern widgets do not consistently follow user theme choices — 5/10

`modern::windowColor()`, text, accent, background, and other helpers return hard-coded default-theme colors. Some controls apply inline styles built from these values. They look coherent in the current default theme, but cannot automatically adapt to a different or high-contrast palette. This creates a second styling system alongside the existing theme files.

**Prioritize:** Shared theme behavior and readable focus/selection states across themes. Address this alongside scalable controls, rather than designing more independent palettes.

Evidence: [ModernWidgets.h](include/ModernWidgets.h), palette helpers; [CommandPalette.cpp](src/gui/CommandPalette.cpp), inline styling; [style.css](data/themes/default/style.css).

### 21 — Internal range-plus-tail rendering can include later clips — 4/10

The tail loop continues calling `renderNextPeriod()` while the song remains playing. It does not prevent the sequencer from starting material after the requested range. In a probe, a first clip with amplitude 0.1 occupied ticks 0–48 and a second with amplitude 0.7 began at tick 48. Rendering only 0–48 with tails enabled produced a peak of 0.7, demonstrating inclusion of the second clip.

**Scope qualification:** The export dialog currently disables tails for loop-marker/loop exports, and single-clip bounce mutes the other source clips. Therefore this is a demonstrated renderer-contract bug, **not** a demonstrated normal GUI loop-export failure. Do not rank it above the user-facing save issues.

**Prioritize:** Resolve the range/tail contract before exposing more range-with-tail options. Tail processing should have a defined boundary between existing sound decay and newly scheduled material.

Evidence: [ProjectRenderer.cpp](src/core/ProjectRenderer.cpp), tail loop; [Song.cpp](src/core/Song.cpp), `processNextBuffer()`; [ExportProjectDialog.cpp](src/gui/modals/ExportProjectDialog.cpp), `setRenderTail()` condition.

## What is good and worth preserving

- **The command palette solves a real discoverability problem.** It gathers menu actions, editor tools, instruments, effects, tracks, and markers, offers fuzzy matching and recent commands, and has tests for useful workflows. Expand its coverage while keeping normal editing accessible.
- **The new features fit actual composition work.** Retrospective capture, humanization, markers, fades, and bounce reduce interruptions between an idea and an arrangement. Their integration matters more than a larger feature count.
- **The test base is substantial.** Tests cover DSP, rendering, script APIs, MIDI pitch, clip fades, GUI behavior, and a purpose-built VST3 test plugin. All 23 executables passed locally. The next investment should be boundary and failure-path coverage, rather than dismissing the existing suite.
- **The default visual direction is coherent.** Restrained dark panels, consistent green accents, and shared modern widget helpers provide a recognizable hierarchy. The largest remaining UI gains are readability, operation, and discoverability.
- **There are useful safeguards already.** Bounce reserves a unique output file and normally cleans it up on cancellation; script handles detect deleted objects; the script server binds to loopback and requires a random authentication token; project-bundle loading checks for local plugin paths. Preserve these while strengthening error handling.
- **Developer tooling is helpful.** The snapshot utility and scripting examples make repeatable investigation possible. The scripting reference is unusually thorough for a fork and should stay synchronized with real GUI/headless behavior.

## Suggested milestones

1. **Trustworthy projects and exports:** 01–05, with focused save failure, unnamed-bounce, headless round-trip, and export-error checks. Resolve the script/render mutation policy in 08 during this work.
2. **An operable, readable interface:** 06–07 and 11, then marker contrast and capture discoverability in 14–15. Prioritize shared components over individual plugin reskins.
3. **Reliable integration and maintenance:** latency/concurrency in 09–10; restore useful checks and default test discovery in 12 and 16. Close MIDI Capture coverage in 13.
4. **Finish the workflows:** resource overview, reversible freeze, and recoverable scripting in 17–19. Fold theme consistency into UI work. Keep 21 as an explicit prerequisite for future range/tail features.

The recommended next work is a reliability/accessibility pass on the capabilities already present. More instruments or decorative UI changes would provide less immediate value than making saves, bounce, export, and everyday controls dependable.
