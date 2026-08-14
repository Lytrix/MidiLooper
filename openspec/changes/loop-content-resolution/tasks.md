## 1. Docs and gates (this session)

- [x] 1.1 Architecture plan — [`loop_event_sourced_resolution_architecture.md`](../../../docs/Plans/loop_event_sourced_resolution_architecture.md)
- [x] 1.2 DEC-037 in DECISION_LOG; NAMING.md vocabulary
- [x] 1.3 Close `loop-effective-event-source` tasks 4.1 / 4.2; update CURRENT_WORK, PROJECT_STATE, DELIVERABLE_TRACKING
- [x] 1.4 This OpenSpec change (proposal, design, specs, ARCHITECTURE-REVIEW, tasks)

## 2. Stage 0 — Canonical fixture

- [x] 2.1 Native suite `test/test_loop_content_resolution/` with one growing fixture: 64 or 128 bars, 45+ passes, multiple channels, same-pitch overlaps, shorten/extend/delete/move, wrap
- [x] 2.2 Counters on every run: events in history, passes in history, events in query window, candidate events, resolution operations, elapsed µs
- [x] 2.3 Baseline comparison path: same fixture through `LoopPasses::materializeToEventVector` + `reconstructDisplayNotes` (correctness oracle)

## 3. Stages 1–5 — Native resolveWindow / resolveState

- [x] 3.1 Module `LoopContentResolution` with `resolveState` / `resolveWindow` primary; `resolveNotes` derived; pin `RawMidiEvent` / `EditAction` / `ResolvedEvent` (alias/`MidiEvent` shape, no fourth synonym)
- [x] 3.2 Stage 1: one pass NOTE_ON/OFF → window + state; notes as projection
- [x] 3.3 Stage 2: two overlapping same-pitch passes (DEC-031/032 layer semantics)
- [x] 3.4 Stage 3: DELETE via EditAction
- [x] 3.5 Stage 4: SHORTEN / EXTEND
- [x] 3.6 Stage 5: MOVE (tick / pitch)
- [x] 3.7 Disabled pass excluded from active set; content unchanged
- [x] 3.8 Determinism: cold vs warm cache identical `ResolvedEvent` sequences
- [x] 3.9 `pio test -e native` including this suite

## 4. Stages 6–8 — Index and checkpoints

- [x] 4.1 Tick (and NoteId as needed) index; candidate find MUST NOT walk every pass list
- [x] 4.2 Prove `commit P(N)` does not traverse P0…P(N-1) except indexed affected regions (complexity gate on the canonical fixture)
- [x] 4.3 In-RAM checkpoints at `checkpointIntervalTicks`; measure the interval on the fixture (do not persist)
- [x] 4.4 `resolveState` at a high tick starts from a checkpoint, never from tick 0
- [x] 4.5 Note spanning two chunks still resolves (chunks are not boundaries)
- [x] 4.6 Loop switch: warm destination `resolveState` at the live playhead; replay ≤ `checkpointIntervalTicks`; no checkpoint rebuild; no tick-0 scan

## 5. Stage 9 — Device gate (after native 2–4)

- [ ] 5.1 `035414`-class worst-case latency: no multi-second MIDI stall, no multi-second OLED stall
- [ ] 5.2 Overdub entry remains cheap (3b copy); no `VCACHE,full` on the normal path; no full materialize after commit
- [ ] 5.3 Record worst-case µs, not only totals

## 6. Production swap (only after all three gates + user approval)

- [ ] 6.1 Dirty overdub fallback → `resolveWindow`; keep 3b clean-cache copy
- [ ] 6.2 Idle visual slices gather via `resolveWindow` (range-dirty bars)
- [ ] 6.3 Long-loop playback gather → `resolveWindow` / `ResolvedEvent`
- [ ] 6.4 Short-loop playback / NOTE_EDIT hydrate last
- [ ] 6.5 Do **not** delete `materializeToEventVector`; do **not** call resolution from `handleMidiInput`

## Out of scope

- Persisted D3 checkpoint (`StorageManager`)
- D4 `LoadLoopJob` publication
- Stage 3b GUS replacement
- Overlay picker, interval reservation, RC-J
- Replacing `NoteGeometryResolver`
