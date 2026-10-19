# Corrected Record-return hardware repeat — run03

## Identity and safety

Owner-requested retry, 2026-10-11. Canonical unchanged `STIM-BASE-2-2-01@v1`, default `teensy41-capture-serial`, host external clock300BPM, only operator track8/slot4 overwritten. Command follows [HITL_RECIPE.md](HITL_RECIPE.md), fresh output `captures/record-return-recovery-t8s4-03`.

- Git `abaa30975d2cd2650fbce6638ef9e6d3783ad5d7` + dirty repair; branch `feature/unified-rolling-playback-path`.
- Uploaded HEX SHA-256 `a9a65d2f56e52845dc777a6c3203bb1ee72b363f83ac10a761251b5baf9075f6`, matches managed metadata and pre/post upload hash.
- Preflight04: two verified Stop/drain cycles; upload SUCCESS with unchanged RAM1 variables96192/code392376/padding840/floor840/locals34880. Immediate postflash04 probe found no MIDI port while USB re-enumerated; it opened no session and wrote no latch. Bounded re-enumeration then postflash05: two verified drains.
- Run03 shutdown `complete / drains2 / ok=true`; latch absent. Device left stopped/drained. No quarantine, card rename/delete, other-slot clear or firmware/host/stimulus edit.
- Record/overdub phase stops remain button gestures. Run-end `host_realtime_stop`, one setup transport-button press.

## Device-timestamp-ordered playback result

Log `session_20261011_162812.log`; report `host_midi_automation_baseline_20261011_162925.json` under the run directory. Streamed log parsed and rows sorted by device timestamp, not text order. First return selected from first channel8 NoteOn48@tick0 after real Record stop to the next NoteOn48@tick0.

- Real Record stop28.084985s; geometry1536/1536, raw1536; stored32-note grid ticks0..1488, delta48, zero bad deltas.
- First return **32 NoteOns /32 NoteOffs**, exact expected pitch48..79/tick0..1488 sequence and equal pitch On/Off inventories. Last closure Off79@tick1528 at29.676206s; next wrapped NoteOn48@0 at29.683322s. No missing second bar.
- Ready revision0 `[768,1536)` completed28.177887s, before pass publication/request28.858327s. Boundary **Off63@76828.884952s → On64@76828.885032s** survives; all16 second-bar NoteOns are present. Tick720→768 musical output gap47.336ms (last720 CC28.837616s to first768 Off28.884952s), versus run02's847.784ms gap to wrapped0 with the entire second bar absent.
- Largest consecutive musical MO gap in first return **55.348ms**, through first actual overdub stop **68.401ms**. First NoteOn is58.321ms after stop; startup catches up tick0 and tick48 at28.143s, so this is not zero-lateness/jitter qualification.
- First Record Snapshot publication30.203254s, **after** complete first return. Its output therefore does not depend on waiting for the whole-root publication.
- First actual overdub29.523982s→31.980808s;15 target DISP rows show32→33→52 notes, largest row separation395.582ms;2 DFRAME samples. First note-count update29.889400s precedes root publication. Report display checks pass, post-stop STOPPED frame57/epoch57/visual57/buffer57.
- Ring readback `shed=5, tierA_evict=0, overflow=0, crit_rej=0`. MO shedding is possible; captured exact first-return sequence/closure inventory is not independent wire parity. DISP is change-filtered and DFRAME sampled: progress, not exact painted-content parity.

## Report verdict and remaining acceptance

Runner exit0 is **not** baseline PASS. Report stays **INCONCLUSIVE_CAPTURE (SETUP_UNCONFIRMED)** with `overdub_unmatched_open_notes` and `persistence_stage_missing:result`.

Read-only attribution reproduces the known parser discrepancy: parser first-OD stop30.537000s, actual ST stop31.980808s. Input On31@30.535781s and Off31@30.634265s straddle the false boundary. Parser-window input8On/7Off/1open; actual ST-bounded input16On/16Off/0open. No parser repair or verdict weakening performed. Persistence `result=ok` exists later at74.038269s,76.524152s and79.007647s in the full drained log, outside the baseline report's result window.

The concrete ready-before-publication missing-bar failure is no longer observed on this pinned device run. Keep S7.5b.5 — next Record-return readiness and combined timing/content/display acceptance unchecked: baseline is inconclusive; exact external MIDI/painted display parity, broader lifecycle routes and timing envelope are not established. No commit/push/PR. Detailed measurement JSON and bounded analysis script remain in session scratch (`record-return-03-analysis.json`, `analyze_record_return_03.py`); authoritative raw artifacts remain in the run directory.
