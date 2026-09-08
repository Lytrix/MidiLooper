# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-09-08 (Hydrate 1 select neighborhood)

Authority hygiene: execution bound class is a gated property — [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md), [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property). Architecture-checkpoint question 3 is a hard stop for new unbounded interval work. No interval reservation / `RuntimeWorkBudget::admit`.

---

## Now implementing

**Hydrate 1 — select neighborhood around `selectedTick` (firmware in tree).**
`SelectNavigation::buildSelectNavigationSlots` emits only the ±1 16th
neighborhood (existing start-tick / pitch order). Fader maps pitchbend to
a 16th in `navLength`, then queries that neighborhood. Native PASS
(**1783** succeeded, **1** skipped). RAM1 locals **7008**. Device recapture
not done. Not `resolveState`. Not hydrate 2–5 / 4a. DEC-055 Stage 3 PASS
[`221906`](../../captures/session_20260908_221906.log). Do not start
DEC-054 Stage 2, later **E:** overlay-delta entries, or rename
`NoteEditCurrentState`.
Plans:
[`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md),
[`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md),
[`note_edit_overlay_delta_architecture.md`](../Plans/note_edit_overlay_delta_architecture.md),
[`note_edit_overlay_delta_refinement.md`](../Plans/note_edit_overlay_delta_refinement.md).

**Prior — DEC-054 Stage 1 firmware in tree — device recapture failed cadence.**
Select undo warm coalesces, settles 80 ms, and runs after a completed
display frame. [`204458`](../../captures/session_20260908_204458.log)
early select `DFRAME` is in band; back-and-forth select is 3.1–4.6 s /
30 frames (7 warms / 363 ms `focus_snap` in one window). Option B is
not the proposed closeout if DEC-055 is accepted. Plans:
[`note_edit_display_playing_stall_architecture.md`](../Plans/note_edit_display_playing_stall_architecture.md),
[`note_edit_display_playing_stall_refinement.md`](../Plans/note_edit_display_playing_stall_refinement.md).
Do not start Stage 2, hydrate Stage 4a, raise `N`, `T_RT_reserve`, 4.5,
or 4.6.

**Prior — OpenSpec 4.4h — display fallback/identity — Stage 1 shipped.**
`rebuildDisplayNotesInWindow` on a current root copies span notes
(`tryCopyCurrentSnapshotDisplayNotesInWindow`). Overlay gather is
rejected. Live-capture merge keeps the existing gather. RAM1 locals
**6848**; native **1774 succeeded, 1 skipped**. Device
[`201106`](../../captures/session_20260908_201106.log): second overdub
open `from=snapshot` `root_rev=5` `play_rev=5` `notes=114`. NOTE_EDIT
open `current=1` `root_rev=6` **137** / `only_*=0`. `DFRAME` **137**
after second stop through session end. First overdub open
`from=visual` is before `pending=0`. Do not start hydrate Stage 4a,
raise `N`, 4.5, or 4.6.
Plan:
[`display_fallback_identity_snapshot_refinement.md`](../Plans/display_fallback_identity_snapshot_refinement.md).

**Prior — OpenSpec 4.4g — analysis/validation leftover oracles — shipped.**
`validateAndCleanupMidiEvents` uses `Loop::copyIdleValidateEvents`
(current-root span-filtered snapshot events). Overdub analysis
current-root gather already skips pass flatten (4.4a). SEVT / `take_only`
unchanged. RAM1 locals **6848**; native **1773 succeeded, 1 skipped**.
Device [`193700`](../../captures/session_20260908_193700.log): second
overdub open `from=snapshot` `root_rev=7` `play_rev=7` `notes=159`.
[`194039`](../../captures/session_20260908_194039.log): continuation
NOTE_EDIT open `current=1` `root_rev=8` **177** / `only_*=0`. Close
`edits=3`, no `live capture baked`.
[`194438`](../../captures/session_20260908_194438.log): Stage 1 idle
validate at 600.015 s on current `root_rev=1` (record-to-STOPPED + 60 s).
Log-only 2 orphans, 116 events remaining; `DFRAME` **58**. Both overdub
opens `from=snapshot` on a current root.
Plan:
[`analysis_validation_oracle_snapshot_refinement.md`](../Plans/analysis_validation_oracle_snapshot_refinement.md).

**Prior — OpenSpec 4.4f — leftover NOTE_EDIT oracles.** Current-root open
skips `assignMissingNoteIds(loop.midiEvents())`. E: rows undo/redo fallback
uses `Loop::tryCopyNoteEditOpenedSnapshotEvents` (span NoteIds; drops
event-only 64). Pass rematerialize stays the no-current-root fallback.
`take_only` unchanged. RAM1 locals **6848**; native **1772 succeeded, 1 skipped**.
Device [`191346`](../../captures/session_20260908_191346.log): both
`current=1` opens `only_*=0` (120 then 119).
[`192155`](../../captures/session_20260908_192155.log): `current=0` open
keeps pass rematerialize (`only_store=5`); three `current=1` `root_rev=41`
opens are **141** / `only_*=0`.
Plan:
[`note_edit_leftover_oracle_snapshot_refinement.md`](../Plans/note_edit_leftover_oracle_snapshot_refinement.md).

**Prior — Wrap-head vs last-Off pairing RC1–RC3** — accepted without
174646 recapture. Native shipped; [`185428`](../../captures/session_20260908_185428.log)
RC3 analog PASS. Plan:
[`wrap_head_last_off_pairing_bugfix.md`](../Plans/wrap_head_last_off_pairing_bugfix.md).

**Prior — 4.4e Stage 3 + close bake closed.**

**Prior — NOTE_EDIT close bake — snapshot baseline:**
Empty-id close no longer treats snapshot-store vs pass rematerialize as
live-capture rows. Device PASS
[`174646`](../../captures/session_20260908_174646.log): no
`live capture baked`; empty third exit has no `NoteEditPassClosed`;
reopen stays `current=1` `root_rev=13` notes **126**. Plan:
[`note_edit_close_bake_snapshot_bugfix.md`](../Plans/note_edit_close_bake_snapshot_bugfix.md).

**Prior — OpenSpec 4.4e Stage 3 — commit rematerialize from the same root:**
`commitEditAction` persist/replay/session-store reload and
`materializedLoopEventsForNoteEditFocus` use `Loop::copyNoteEditCommitBase`:
pinned open-root span-filtered snapshot events plus this session’s edit
rows. Device PASS
[`173531`](../../captures/session_20260908_173531.log): both `current=1`
opens `only_*=0` (99 then 97). `overlap_baseline_diff` Delete noteId=56
and noteId=11 are on that root. `take_only` 227 vs rematerialize 196 is
capture-only. `current=0` `only_store=1` id `80` is not this gate.
RAM1 locals **6848**; native **1767 succeeded, 1 skipped**.

**Prior — OpenSpec 4.4e Stage 2 — NOTE_EDIT event membership (same root):**
`Loop::rematerializeEditView` on a current published root loads
`EditSession.store` from that root’s event objects whose `NoteId`s are in
`resolveDisplayNotes(*snapshot)` span membership. Device PASS
[`171444`](../../captures/session_20260908_171444.log): both `current=1`
opens `only_visual=0` `only_store=0` (116 then 114). `current=0` opens
after `playback_rev=16` vs `root_rev=11` stay on `passes.materialize`.
Plan:
[`note_edit_committed_oracle_snapshot_bugfix.md`](../Plans/note_edit_committed_oracle_snapshot_bugfix.md).

**Prior — NOTE_EDIT close — NoteRange snapshot reuses pitch:**
`appendEditRowChange` does not write `row.pitch` for `NoteRange`.
Close publish reuses the parent pitch, or the earlier same-`NoteId`
change when Pitch then NoteRange share one `tryPublishNoteEditSnapshot`.
RAM1 locals **6848**; native **1765 succeeded, 1 skipped**. Device PASS
[`163849`](../../captures/session_20260908_163849.log): moved note
`DNTE,63,1584,1584,287`; no pitch-0 `DNTE`; reopen **95**.
Plan:
[`note_edit_snapshot_note_range_pitch_bugfix.md`](../Plans/note_edit_snapshot_note_range_pitch_bugfix.md).

**Prior — OpenSpec 4.4e Stage 1 — NOTE_EDIT visual baseline from current snapshot:**
`resolveDisplayNotes(*snapshot)` copies note-span membership
(`appendSnapshotNotesForBarRange`). Event-flat reconstruct is not the
display interpreter (`155257` 99 vs 97). RAM1 locals **6848**; native
**1763 succeeded, 1 skipped**. Device PASS
[`162207`](../../captures/session_20260908_162207.log):
STOPPED / spans / `stored_notes` / open visual **98**. Wrap-held append
after current-root spans is skipped. Do not start Stage 2/3.
Plan:
[`note_edit_committed_oracle_snapshot_bugfix.md`](../Plans/note_edit_committed_oracle_snapshot_bugfix.md).

**Prior — OpenSpec 4.5a — post-stop snapshot publication convergence:**
Closed. `refreshViewportAfterOverdubStop` holds the last OVERDUBBING
composed frame (`why=stop,from=hold`); no pass-gather on the stop stack.
Drain + `sessionExitQueued` inserts an extra Snapshot turn after every
non-Snapshot owner. First new committed source is the current published
`LoopSnapshot`. Device
[`145633`](../../captures/session_20260908_145633.log): display child 0,
`post_stop` `root_rev == playback_rev`, next open `from=snapshot`.
RAM1 locals **6848**; native **1759 succeeded, 1 skipped**. Plan:
[`overdub_stop_snapshot_publication_convergence_refinement.md`](../Plans/overdub_stop_snapshot_publication_convergence_refinement.md).

**Prior — OpenSpec 4.4c — span-cache snapshot provenance:**
Closed. `overdubSourceSpanCacheReady()` stays a storage/coverage check.
Reuse at open/hold/prewarm requires `overdubSourceSpanCacheReusable()`,
which matches the current published revision when a root is current.
Fallback prewarm at the same `playbackRevision` no longer hides that
root (`135337`). OpenSpec 4.4c. `DEC-052`. RAM1 locals **6848**; native
**1757 succeeded, 1 skipped**.

**Next within 4.4:** NOTE_EDIT, overdub analysis, validation, and remaining
display identity/fallback caller migration remain open. Each sub-slice
requires the existing phase gate and its own rollback/verification evidence.

**Prior — OpenSpec 3.5 / 3.6 closed (yield proof parked):**
3.5 wrap-journal publication is in tree. 3.6 native wrap-journal parity and
device wrap-retain / reserve telemetry
[`101815`](../../captures/session_20260908_101815.log) /
[`104353`](../../captures/session_20260908_104353.log) stand. Remaining-
interval yield device proof is parked: `kSnapshotIdleReserveYieldEnabled`
is false after [`115043`](../../captures/session_20260908_115043.log)
(`remaining=0` until STOPPED). `allowSnapshotIdleReserve`,
`DIAG,snapshot_idle,yield`, and `!SNAPSHOT_YIELD` stay compiled.
`T_RT_safety_margin` stays unfrozen. N = 2. Plan:
[`snapshot_publication_reserve_admission_refinement.md`](../Plans/snapshot_publication_reserve_admission_refinement.md).

**Prior — OpenSpec 3.5 / 3.6 wrap publication notes:**
3.5 firmware is in tree: settled wraps queue bounded mutation journals;
session-exit starts at most one journal-capped fragment; publication waits
until journals, queued passes, construction, and companions complete.
`queueLoadedSnapshotPublish()` stays off after restore (`023222` /
[`024731`](../../captures/session_20260908_024731.log)). This slice does
**not** reconstruct a root from every loaded pass. Task 3.6 native wrap-journal
parity is in tree. Device wrap-retain recapture
[`101815`](../../captures/session_20260908_101815.log): both overdub stops
were still `seal=skipped`, but journals were kept (`exit=1`), `fragment`
advanced (`0/3→2/3`, `0/2→1/2`), and `root_rev` caught `playback_rev`
(`pending=0` `root_rev=3` `playback_rev=3`). Prior fail class
[`094930`](../../captures/session_20260908_094930.log)/`094228` discarded
in-flight wrap journals and left `root_rev` at 1. Host HITL overall still
FAIL for known non-wrap issues (`dispatch`, 2304-vs-1536, SEVT window,
second-overdub first-note). Focused native **83/83**. `T_RT_reserve` policy
is [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary)
(`T_RT_reserve_ceiling` 10,000 µs; per-turn `reserve_us`; margin not
frozen). Capture-gated `DIAG,snapshot_idle,reserve` is in
`InvestigationCaptureLog` (FLASHMEM): child emit arms one pending start;
`handleMidiInput()` enter pairs `reserve_us`. Unpaired overwrite emits
`reserve_us=0`. Device recapture
[`104353`](../../captures/session_20260908_104353.log): 1864 `reserve`
lines, `reserve_us` 102–4779 µs (none above the 10 ms ceiling), wrap
`pending=0` `root_rev=3`, both overdub `seal=skipped` still captured with
full `seal_fx_*` sequence. Serial close drain 309 trailing lines. Remaining-
interval admission is now in tree on `MidiServiceDrain` /
`Track::processDeferredIdleMaintenance`: two-unit cost must fit
`min(10000, remaining − margin)` or the turn yields. Capture-gated
`DIAG,snapshot_idle,yield` plus `!SNAPSHOT_YIELD` one-shot inject.
`SESSION_CAPTURE_SNAPSHOT_IDLE=1` is on `teensy41-capture-serial` for this
gate. Snapshot idle cardinality is **N = 2**; do not revert to 1. STOPPED
timings are excluded from the 3.6 ceiling. [`110034`](../../captures/session_20260908_110034.log)
live `reserve_us` max 5871 µs (zero over 10 ms); the 36 / 42 / 95 ms
samples are after `PLAYING → STOPPED`. Host: RAM1 locals **6848**; focused admission/yield native PASS;
full native **1753/1754** (1 skipped). Device yield HITL
[`115043`](../../captures/session_20260908_115043.log):
`snapshot_reserve_admission.py` PASS (`yield_count=6389`, first yield
`RECORDING` `root_rev=0`, complete `root_rev=9` `pending=0`). Every live
yield has `remaining=0`; the first `snapshot_idle` construction is after
`PLAYING → STOPPED`; `pending=0` is that STOPPED publish. No yield after
STOPPED. Host base still FAIL for the known non-slice checks
(`record_first_note_too_late`, 2304-vs-1536, `dispatch`, SEVT window).
Yield reject is parked (`kSnapshotIdleReserveYieldEnabled` is false).
`DEC-047`. `DEC-053`.

**Prior — Loaded hydration stability — device PASS [`024731`](../../captures/session_20260908_024731.log):**
Title exit waits for boot restore only. Adopt/restore do not auto-queue
loaded snapshot publication. After title, Stage 2.1 fallback fills from
passes (`root_rev,0`). Zero `ld=1`. Session-exit publication unchanged.
Plan: [`loaded_hydration_stability_bugfix.md`](../Plans/loaded_hydration_stability_bugfix.md).

**Prior — Visual-cache Stage 2.1 — snapshot membership copy:**
When `publishedSnapshotIsCurrent()`, `rebuildVisualCacheIdleSlice` copies
`LoopSnapshotNote` membership for the dirty bar range. Fallback / prepared
fill stays when no current root exists. Host: RAM1 locals **6848**. Native
**1727/1728** (1 skipped).

**Prior — Overdub MIDI boundary — Stage 4a device attribution [`235930`](../../captures/session_20260907_235930.log):**
Tags emit. Residual ≤ 24 µs; slot ≤ 26 µs. `tracks` ≈ `clk` ≈ `usbclk`.
OD1 peak `tracks` 13149 µs (T4 9007 + T6 3932) under `service_period`
20824. Dominant attributed span on that peak is `track_order` 7974 µs,
not rebuild multiplicity. Ring A is not authorized from this log.
`playCommittedLoopMidi` stays consumer, not optimization owner. Plan:
[`overdub_midi_boundary_timing_refinement.md`](../Plans/overdub_midi_boundary_timing_refinement.md).

**Prior — Overdub-stop first PLAYING frame parity — HITL PASS [`230846`](../../captures/session_20260907_230846.log):**
The first `OVERDUBBING → PLAYING` display frame now preserves the effective
committed handoff while deferred save starts. `refreshViewportAfterOverdubStop`
finalizes from `LoopContentResolution::resolveDisplayNotes` after cache
adoption, and `resolveDisplayNotesByDrawSource` preserves that
revision-matched handoff before normal source/cache rebuilding resumes. The
cache is a reuse seed, not committed-content authority. RAM1 locals **6848**;
focused native **162/162**; full native **1724/1725** (1 skipped). HITL
`DISP` and `dnte` frame/effective counts match on the first and settled frames
(`98`/`98`). RC closed.
Plan: [`overdub_display_snapshot_handoff_bugfix.md`](../Plans/overdub_display_snapshot_handoff_bugfix.md).

**Prior — First STOPPED D# next-On flash — HITL PASS [`225234`](../../captures/session_20260907_225234.log) ([`220319`](../../captures/session_20260907_220319.log) /
[`222432`](../../captures/session_20260907_222432.log) /
[`223121`](../../captures/session_20260907_223121.log)):**
`src,effective` is correct while `src,frame` still uses stale cache geometry.
The `f53cc59a` companion-replay workaround regressed to all-bars lengthening
in [`223121`]. It is reverted. [`224901`] showed the preserved overdub-stop
handoff still bypassed the direct resolver. STOPPED committed display now
calls `LoopContentResolution::resolveDisplayNotes` before that handoff,
reusing the same final-state code as `src,effective`. `af65a70e` remains
because it fixed the deferred effective/rebuild pairing path. RAM1 locals
**6848**. Focused native **232/232**; full native **1724/1725** (1 skipped).
HITL [`225234`] passes: `src,effective` and `src,frame` have matching
11-row D# geometry and `DISP` counts. RC closed.
Plan: [`overdub_stopped_frame_last_occupy_bugfix.md`](../Plans/overdub_stopped_frame_last_occupy_bugfix.md).

**Prior — Overdub same-start A loss and D# Off pairing ([`183043`](../../captures/session_20260907_183043.log) / [`185822`](../../captures/session_20260907_185822.log) / [`192322`](../../captures/session_20260907_192322.log) / [`195409`](../../captures/session_20260907_195409.log) / [`204112`](../../captures/session_20260907_204112.log) / [`211440`](../../captures/session_20260907_211440.log)):**
Stage 1–3, RC-A, overlay, and pending-paint device PASS [`211440`].
Stop D# Off pairing host in tree: deferred visual-cache fallback/full
rebuild, last-resort committed/window reconstruct (capture inactive), and
`dnte,src,effective` use `LoopContentResolution::resolveDisplayNotes` so
committed D# Offs `768→1440`, `1536→2208`, `2304→2976` survive flatten.
`Track::finalizePendingNotes` still closes one genuinely held final D#.
RC-B unmatched NoteOff admission is not started. Native pin
`test_visual_cache_keeps_dsharp_committed_offs_after_rebuild_211440`.
`teensy41-capture-serial` RAM1 locals **6848**. HITL recapture open. Plans:
[`overdub_same_start_note_pairing_bugfix.md`](../Plans/overdub_same_start_note_pairing_bugfix.md),
[`overdub_bar1_compose_miss_bugfix.md`](../Plans/overdub_bar1_compose_miss_bugfix.md),
[`overdub_overlay_identity_bugfix.md`](../Plans/overdub_overlay_identity_bugfix.md),
[`overdub_pending_paint_frame_drop_bugfix.md`](../Plans/overdub_pending_paint_frame_drop_bugfix.md),
[`overdub_stop_dsharp_noteoff_pairing_bugfix.md`](../Plans/overdub_stop_dsharp_noteoff_pairing_bugfix.md).

**Prior — Overdub occupy wrap `norowid` / currentTick temp Off ([`174151`](../../captures/session_20260907_174151.log)):**
Wrap cursor walk offers `(prev, L-1]` then `[0, S]` to
`applyPlaybackEvent`. Occupy span cache / last-pass merge recover
omitted committed NoteOn identities with that same untagged-Off / LIFO
rule (reconstruct can defer Off@`L-1`). Incomplete `visualCache` is a
cache miss, not absence. Paint tails stay downstream. Host pins PASS.
Native **1704/1705** (1 skipped). RAM1 locals **6848**. HITL recapture
open. Plan: [`overdub_occupy_wrap_norowid_noteoff_bugfix.md`](../Plans/overdub_occupy_wrap_norowid_noteoff_bugfix.md).

**Prior — Overdub display snapshot handoff ([`170537`](../../captures/session_20260907_170537.log)):**
Compose the paint window from a complete sealed committed source, then
cache for reuse. `visualCache.notes` is not committed-content authority.
Just-sealed overdub rows join the committed prefix before Hide/Shorten.
A smaller current snapshot must not replace a complete handoff (`id=112`).
MIDI-boundary Stage 3/4 stays out of scope.
Focused native PASS (`test_display_window_utils`, `test_loop_take_survival`,
`test_pending_note_change`, `test_overdub_source_view`). Full native PASS:
1700 succeeded, 1 skipped.
`teensy41-capture-serial` RAM1 locals **6848**. Full native + HITL open.
Plan: [`overdub_display_snapshot_handoff_bugfix.md`](../Plans/overdub_display_snapshot_handoff_bugfix.md).

**Prior — Overdub MIDI boundary timing — Stage 2 (USB batch composition):**
`MidiBatchComposition` plus `RuntimeTimingTelemetry` window tags
`usb_batch`, `usb_bkt_size`, `usb_bkt_clk`, `usb_bkt_disp`. Dispatch
order is unchanged. Native **1689/1690** (1 skipped). RAM1 locals **6848**.
Slave-clock [`170537`](../../captures/session_20260907_170537.log):
`clockrate` 47–48, `service_period` ≈ 20824 µs, `late_clk` = 0.
`usb_bkt_clk` all in 1–8; `clk_batch` max 3 (OD1/OD2 max 1); saturated 0.
OD2 `usbclk`/`tracks` 26.3 ms; `extclk_arrival` 66.6 ms / overCount 240.
Stage 3 FIFO is not authorized — no large clock batch. Internal-clock
occupy [`170249`](../../captures/session_20260907_170249.log) HITL **FAIL**.
Plan: [`overdub_midi_boundary_timing_refinement.md`](../Plans/overdub_midi_boundary_timing_refinement.md).

**Prior — Stage 1 (clock attribution):**
`extclk_arrival`, `extclk_dispatch`, `clk_batch`, `service_period` on
`RuntimeTimingTelemetry`. Occupy HITL [`165045`](../../captures/session_20260907_165045.log)
emits those tags (`service_period=2666`; clock-batch fields 0). Native
**1686/1687**. RAM1 locals **6848**. Stages 3–4 not started.

**Prior — One-frame drop after each next bar ([`142336`](../../captures/session_20260907_142336.log)) — device PASS [`143701`](../../captures/session_20260907_143701.log):**
After `004710` pending-first ordering, `applyCapturePlayheadTails`
still matched open holds by pitch+start only and rewrote a closed
same-start sibling. Capture preview also dropped assigned `NoteId`s
because append fed the raw input event into `applyCaptureEventToPreview`.
Tails now prefer `NoteId`; append feeds the stored `toAppend` event.
Host pins `test_same_pass_playhead_tails_keep_closed_bar_four_end` and
`test_append_capture_preview_keeps_assigned_note_id`. Device
[`143701`](../../captures/session_20260907_143701.log): shortened long
rows stay painted across each next `BAR` (`hidden,0`).
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Incoming long note hidden until NoteOff ([`004710`](../../captures/session_20260906_004710.log)):**
Overlap resolve writes pending transforms on NoteOn, but paint applied tails
before pending. A same-start closed row absorbed the open-hold tail update,
then pending Hide/Shorten removed that row, so the long incoming note was
missing until NoteOff. Paint now applies pending first and tails second in
both live resolve and draw-source gather. Host pin
`test_pending_then_tails_keeps_incoming_long_same_start_004710`.
Device: incoming long notes must appear at NoteOn and grow to `currentTick`
while overlapping earlier shorter notes.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Earlier remnant dropped the new long occupy ([`003622`](../../captures/session_20260906_003622.log)):**
`002708` covering-occupy drop treated the older longer remnant
(`768–1344`) as the occupy that covers the new long Add (`816–1344`).
Paint kept the short keeper and removed the long Add. An ungrown open
hold on pitch 69 also dropped later A notes for a frame. Covering now
requires the earliest row to be the shortest keeper at its start, and
the hold must actually cover the inner. Host pins
`test_earlier_remnant_does_not_drop_new_long_occupy_003622` and
`test_ungrown_earlier_hold_does_not_drop_later_same_pitch_003622`.
`002708` inners under a true long keeper still drop. Device: long occupy
stays; record A notes do not vanish for a frame each bar.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Long occupy leaves inner shortened notes ([`002708`](../../captures/session_20260906_002708.log)):**
After a second pass, a long occupy (`864–1344`) covers already-shortened
inners (`1056–1200`, `1296–1335`). Stop effective hid them; the frame
still painted them. Same-start Hide drop kept `1296–1335` as a shorter
keeper because the occupy starts earlier. Hide now drops an inner when
an earlier same-pitch occupy covers it. Hide absorbs a later Length of
the same NoteId. Host pin
`test_long_occupy_hides_inner_shortened_notes_002708`. Device: a long
occupy that covers several shortened notes must not show those inners
on the next `DISP` after the occupy NoteOn, including after wrap.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Pending Shorten overwritten by sealed Length ([`002200`](../../captures/session_20260906_002200.log)):**
After wrap 2, occupy of already-sealed `73@768–1247` at `hs=1056` writes
pending `768–1055`. Paint applied pending first, then sealed Length
`1247`, so `painted_pending_shorten` never logged 73 and OLED kept the
older end until stop. Stop effective/frame already have `768–1055`.
Sealed overlay now applies first; this-lap pending wins. Host pin
`test_pending_shorten_wins_over_older_sealed_length_002200`. Device:
after two wraps, a further occupy Shorten must show `holdStart-1` on the
next `DISP` before the next `BAR`.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Same-start Hide remnant ([`001145`](../../captures/session_20260906_001145.log)):**
After several occupy Shortens, a later same-start occupy Hides the
previous Add (`63@1152–1440` `hs=1152`). Hide removes that NoteId, but
the closed remnant stayed because the new occupy is an open hold and
shortest-wins never dominates it. That remnant is the old note that
returns before the new occupy lengthens. Hide now drops closed rows
whose end is at least the hidden source end. Shorter keepers
(`201956` 768–864) and open holds stay. Host pins
`test_same_start_hide_drops_closed_remnant_keeps_open_hold_001145` and
`test_same_start_hide_drops_remnant_when_hold_present_001145`. A Hide
whose target was never in the frame still leaves that row (`RC3`). Device: a
same-start Hide must not show the hidden source on the next `DISP`
before the occupy Add grows.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Later occupy Shorten remnant ([`000402`](../../captures/session_20260906_000402.log)):**
`painted_pending_shorten` already rewrites later NoteIds (id 73 after
id 71; id 77 after id 75). OLED still shows the original length until
overdub stop because `drawAllNotes` also draws a longer same-pitch+start
sealed-preview remnant. Sibling drop now removes closed remnants at
named Hide/Shorten pitch+starts, including the live-capture suffix.
Open holds stay. Occupying Adds start at hold, not at the source start.
Gather upsert during capture is unchanged (`150650`). Host pins
`test_pending_shorten_drops_suffix_longer_remnant_000402` and
`test_later_occupy_shorten_drops_suffix_remnant_000402`. Device: every
occupy Shorten, not only the first, must show `holdStart-1` on the next
`DISP` before the next `BAR`, before and after wrap.
Plan: [`overdub_wrap_sealed_shorten_paint_bugfix.md`](../Plans/overdub_wrap_sealed_shorten_paint_bugfix.md).

**Prior — Wrap sealed Shorten paint ([`235546`](../../captures/session_20260905_235546.log)):**
First sealed Shorten stayed after wrap; later ones did not. RC-C:
`recoverMissingSealedShortenSources` skipped a Length companion when
snapshot/source-view/visual missed that NoteId. Fallback is the sealed
Length row itself (`74@1536–1967` stayed; `76@2304–2735` must recover).
Host pin `test_wrap_seal_keeps_later_painted_shorten_235546`. Prior RC-A
suffix apply and RC-B recover owner stay.

**Wrap-held head occupy Shorten ([`225200`](../../captures/session_20260905_225200.log)) — Stage 1:**
Pin Failure A passed: wrap-held `69@4560→760` id 96 stays in source view
and `collectOverdubSourceHoldParticipantIds(48, 69)` contains it. Failure
B was occupancy: `startTick < holdStart` wrote Hide (`4560 < 48` is
false). `resolveOccupancyAtNoteOn` now Shortens a wrap-held row present
at a head hold (`hold > 0`, `hold != start`) to `hold-1`.
`composedShortenDegradesToHide` keeps wrap-held Shorten (`end < start`).
Linear Shorten/Hide and `hs=0` Hide are unchanged. Host pin
`test_wrap_held_record_row_occupies_head_225200`. Focused
`test_pending_note_change` + `test_overdub_source_view` PASS.
`teensy41-capture-serial` RAM1 locals **6848**. Native **1669/1672**
(pre-existing playhead-tails fail; one skipped). Device recapture:
post-wrap occupy of that wrap-held row at `hs=48` must be `a>=1` and
`painted_pending_shorten` before the next `BAR`. `63@hs=0` `n=0,a=0`
stays correct. Do not start Stage 2 (stop-hop foreign frame).
Plan: [`overdub_wrap_head_occupy_shorten_bugfix.md`](../Plans/overdub_wrap_head_occupy_shorten_bugfix.md).

**Prior — Wrap NoteOn Shorten recovery ([`223435`](../../captures/session_20260905_223435.log)):**
Post-wrap1 occupies wrote pending Shorten (`as=1536,ae=2208,hs=1824` /
`hs=1872`) but the next paint omitted `painted_pending_shorten`. Later
same-lap `as=2304,ae=2976,hs=2592` painted id 72; stop sealed
`oh shorten=2`. Recovery required VisualCache to still hold the same
`NoteId`. After wrap both compose and VisualCache can miss that identity
while source-view still has the row. `classifyPendingShortenSource` now
recovers whenever compose lacks the target and an authoritative row
exists. VisualCache identity remains `authoritative_absence` evidence
only. Host pins in `test_display_window_utils` and
`test_pending_note_change`. `teensy41-capture-serial` RAM1 locals
**6848**. Native **1668/1671** (pre-existing playhead-tails fail; one
skipped). Device recapture required: first post-wrap Shorten hold must
paint before the next `BAR`.
Plan: [`overdub_paint_wrap_noteon_shorten_bugfix.md`](../Plans/overdub_paint_wrap_noteon_shorten_bugfix.md).
Parent: [`overdub_paint_noteon_shorten_paint_bugfix.md`](../Plans/overdub_paint_noteon_shorten_paint_bugfix.md).

**Prior — reported unfixed undo/reappear after stop ([`221758`](../../captures/session_20260905_221758.log)):**
User reports that a previously undone overdub pass reappears after
overdub stop. That capture boots empty and contains **no undo or redo**.
Not this wrap-recovery RC. Blocked on a capture that contains the undo
and the reappearing notes.

**Prior — Overdub paint NoteOn Shorten on the first paint ([`221758`](../../captures/session_20260905_221758.log), paint gate met):**
[`215658`](../../captures/session_20260905_215658.log) wrote pending Shorten
at NoteOn (`as=768,ae=1440,hs=1104`) but the next paint still matched
VisualCache geometry. Live capture now invokes
`recoverMissingPendingShortenSources`, then applies pending on the paint
copy only. Compose stays original. Device: first occupy Shorten
`65928297` `63@2304–2928` `hs=2784` → `painted_pending_shorten` `65977618`
id **114** `2304–2783` before `BAR,19200,25`; row stays through the next
BAR. Same-start Hides do not emit. No `shorten_src` required. Parent
source recovery and Stage 1+2 stay closed.
Host: focused suites PASS; `teensy41-capture-serial` RAM1 locals **6848**;
native **1667/1670** (pre-existing playhead-tails fail).
Plan: [`overdub_paint_noteon_shorten_paint_bugfix.md`](../Plans/overdub_paint_noteon_shorten_paint_bugfix.md).
Parent: [`overdub_paint_shorten_source_recovery_bugfix.md`](../Plans/overdub_paint_shorten_source_recovery_bugfix.md).

**Prior — pending Shorten source recovery ([`212700`](../../captures/session_20260905_212700.log)):**
Compose can drop a Shorten target when the selected current snapshot
membership omits a row that VisualCache still has (`old,84,4560,4608` →
`new,0,0,0`). Hide can look correct from that absence; Shorten cannot.
`resolveDisplayNotesByDrawSource` recovers the authoritative committed
row by `NoteId` before the paint copy. VisualCache is mismatch evidence
only — never copied. Authoritative absence stays absent. Hide is not
recovered. Stage 1 paint-copy and Stage 2 wrap-append stay closed.
Host: focused suites PASS; `teensy41-capture-serial` RAM1 locals **6848**;
native **1663/1666** (pre-existing playhead-tails fail). Device recapture
superseded by [`215658`](../../captures/session_20260905_215658.log).
Plan: [`overdub_paint_shorten_source_recovery_bugfix.md`](../Plans/overdub_paint_shorten_source_recovery_bugfix.md).
Parent Stage 1+2: [`overdub_paint_noteon_pending_handoff_bugfix.md`](../Plans/overdub_paint_noteon_pending_handoff_bugfix.md).

**Prior — NoteOn pending handoff + wrap-append source guard:**
Stage 1 ([`202821`](../../captures/session_20260905_202821.log)): NoteOn
`pendingNoteChanges_` advances the live paint representation. Compose stays
untransformed; the next paint consumes a pending-painted copy. Overview does
not read a clean VisualCache while pending/overlay exists. Resolver, ledger,
and VisualCache publication stay unchanged.
Stage 2 ([`203406`](../../captures/session_20260905_203406.log)):
`applyDeferredVisualCacheWrapAppend` appends only when the deferred
`playbackRevision` still matches. A mismatch obtains the current-source
frame (`append_skip`, then `full` if the cache is not already current).
Host: `teensy41-capture-serial` RAM1 locals **6848**; native **1660/1661**
(pre-existing playhead-tails fail). Device recapture open.
Plan: [`overdub_paint_noteon_pending_handoff_bugfix.md`](../Plans/overdub_paint_noteon_pending_handoff_bugfix.md).
Do not start Bug 4, Bug 1, or Bug 3 until device recapture closes.

**Prior — Bug 2 occupy On paint named ([`202821`](../../captures/session_20260905_202821.log)):**
Occupy `#CAP,30465195` `63` `as=1536 ae=2208 hs=2016` → next draw-source
`after_pending` and `after_tails` are `id=73` end **2015**. Forks A, B
(at that On), and C are false. First-loop `id=0` + tails growth is the
open-hold skip. Do not patch `applyCapturePlayheadTails`. Plan:
[`overdub_occupy_paint_191913_findings_bugfix.md`](../Plans/overdub_occupy_paint_191913_findings_bugfix.md).

**Prior — Stage 0 RC3 NoteId Off pair reverted ([`191913`](../../captures/session_20260905_191913.log)):**
Restore exact pre-`44f47e94` reconstruct LIFO. Keep `175026` `52@664`
(E=144 vs occupy 363) as a parked finding, not a production pairing
contract. Diagnostic `diagnostic_only_reconstruct_175026_tagged_off_pairing`
is `TEST_IGNORE`. Do not add NoteId pairing fallbacks. 2304 is a legal
loop. Documented order after this pin: Bug 4 → 1 → 2 → 3. Plan:
[`overdub_occupy_paint_191913_findings_bugfix.md`](../Plans/overdub_occupy_paint_191913_findings_bugfix.md).

**Prior — RC3 tagged-Off reconstruct pair (reverted) ([`175026`](../../captures/session_20260905_175026.log) `52@664`):**
`44f47e94` paired tagged Offs by `NoteId`. That fixed one reconstruct
mismatch and is reverted. Parked diagnostic documents LIFO=144 vs
desired 376. Plan:
[`overdub_paint_shorten_live_stop_bugfix.md`](../Plans/overdub_paint_shorten_live_stop_bugfix.md).

**Prior — RC1 pending overlay eligibility ([`172734`](../../captures/session_20260905_172734.log)):**
`applyPendingHideAndShortenToNotes` skips only open capture holds
(`capturePreviewNoteIsOpenHold`). Closed prefix and sealed/closed suffix
rows take pending Hide/Shorten by `NoteId`. `liveCaptureBegin` stays the
sibling-drop / Hide-by-overlap prefix end. No pitch+start fallback.
Host `test_pending_shorten_172734_open_hold_eligibility_matrix`. Device
recapture required: live frame must show this-pass Shorten (`63@680→1207`)
before the next BAR.

**Prior — Capture-serial volume + undo labels ([`213738`](../../captures/session_20260901_213738.log)):**
`SESSION_CAPTURE_SNAPSHOT_IDLE`, `SESSION_CAPTURE_BPM`, and
`SESSION_CAPTURE_DRAW_SRC` default **0** on `teensy41-capture-serial`.
Re-enable independently or via `teensy41-capture-serial-investigation-diag`.
`DISP` / `DFRAME` / `DIAG,clockrate` / `ODUB,stop,*` / `ST` / `VCACHE` stay on.
`TrackUndo::undoForLoop` / `redoForLoop` log `Record undone` /
`Overdub undone` (and the other `UndoEntryKind` names) instead of calling
every non-clear apply `Overdub undone`. HITL still matches `Overdub undone`
/ `Overdub redone` for `OverdubPassAdded`. Not this slice: undo paint
(RC5b keep-larger) or OD1-stop `stale_range` visual shrink. **3.6 `T_RT_reserve`** stays open under [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary);
turn `SESSION_CAPTURE_SNAPSHOT_IDLE=1` back on when emitting `reserve_us`.

**Prior — RC1 occupy-paint closed; RC2 / RC3 withdrawn ([`172213`](../../captures/session_20260905_172213.log)):**
RC1d-d sealed closed Off shipped. Occupy-paint verifier **PASS** [`172322`](../../captures/host_midi_hitl_occupy_paint_20260905_172322.json): `rc1b.mismatches=[]`. **RC2 / RC3 withdrawn** — [`140729`](../../captures/session_20260903_140729.log) `77@336` / `69@5328` cannot be remade. Do not start sealed upsert or admission-miss from occupy-paint. Plan: [`overdub_paint_effective_frame_parity_bugfix.md`](../Plans/overdub_paint_effective_frame_parity_bugfix.md).

**Prior — occupy hold shared capture phase ([`162652`](../../captures/session_20260905_162652.log)):**
`Track::snapshotOverlapHoldCandidates` and `Track::collectOverlapHoldPlaybackNoteOn` compute `holdStart` via `Track::capturePhaseTick`, the same frame as capture storage and source-view ticks. Origin-0 `tickPhaseInLoop(..., 0, loopLength)` is off by `startLoopTick` and produced missed occupy, wrong-frame Shorten, and spurious occupy when transport was already running. Host pin `test_occupy_nonzero_start_loop_shortens_at_storage_hold`. HITL occupy_paint required in both transport conditions (`startLoopTick == 0` and nonzero). Plan: [`overdub_occupy_hold_phase_frame_bugfix.md`](../Plans/overdub_occupy_hold_phase_frame_bugfix.md).

**Prior — RC1d-c occupy HITL recapture [`162325`](../../captures/session_20260905_162325.log) (`fedcc25f`):**
Flashed `teensy41-capture-serial`. Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. Dumps reconstruct 39 / effective 28 / frame 26. No two-length. `52@2172` absent. HITL FAIL is one 1-tick key: `52@2180` f=46 e=47 (E=R=47). Plan: [`overdub_paint_effective_frame_parity_bugfix.md`](../Plans/overdub_paint_effective_frame_parity_bugfix.md).

**Prior — RC1d-c first-STOPPED playhead tail ([`153848`](../../captures/session_20260905_153848.log) `52@2172`):**
Last OVERDUBBING playhead tail freezes inclusive 2230 on `liveDisplayNotes`. RC1b skip keeps that prefix after stop. `refreshViewportAfterOverdubStop` rewrites stop-phase tails from sealed closed Off (2231). Host pins PASS. `teensy41-capture-serial` RAM1 locals **6848**. Native **1647/1649** (pre-existing playhead-tails fail). Do not change RC1b skip, events, or E=59.

**Prior — RC1d Loop pin (investigation only, [`153848`](../../captures/session_20260905_153848.log) `52@2172`):**
Host `test_occupy_153848_52_2172_first_inclusive_2230_pin` PASS. Effective Off@2231 / dump 59. Draw-source prefix, sealed union, and companion overlay stay inclusive 2231. No pending/Length companion on the Add at 2230. **Not RC1d-a. Not RC1d-b.**

**Prior — RC1c occupy HITL recapture [`153848`](../../captures/session_20260905_153848.log):**
Flashed `565b87cb` `teensy41-capture-serial`. Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. Dumps reconstruct 39 / effective 28 / frame 26. No two-length. No `F == R` remnant (`48@666` E=F=396 vs R=744). HITL FAIL is one 1-tick key: `52@2172` f=58 e=59. First run [`153705`](../../captures/session_20260905_153705.log) dropped USB before dumps. Extra-short hold-1 keys (268 / 164) are absent. RC2 / RC3 stay gated.

**Prior — RC1c window gather per-layer apply ([`144743`](../../captures/session_20260905_144743.log) / [`152012`](../../captures/session_20260905_152012.log)):**
`copyEffectiveCommittedEventsInRange` applies Active Note rows on each committed chunk list, then merges. Host `test_occupy_144743_48_616_materialize_vs_window_vs_pending` expects window gather **400**, On@885 absent, Off@1016 kept. `teensy41-capture-serial` RAM1 locals **6848**. Native **1644/1646** — pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end` (does not call window gather).

**Prior — occupy HITL recapture [`152012`](../../captures/session_20260905_152012.log):**
Flashed `teensy41-capture-serial` (no RC1c firmware). Occupy holds PASS: OD1 `shorten=0 hide=0`; OD2 `shorten=3 hide=11`, `from=snapshot`, visual 22, GUS 2/2. RC1c FAIL — 9 `E != F`, no two-length; dumps reconstruct 39 / effective 28 / frame 25. Representative `48@666` frame 739 / effective 399 (same reconstruct-remnant class as [`144743`](../../captures/session_20260905_144743.log) `48@616`).

**Prior — occupy present-at-hold RC4 ([`134436`](../../captures/session_20260905_134436.log)):**
`resolveOccupancyAtNoteOn` may transform a ledger-selected note only when that note is present at the hold phase. Filter before geometric fallback. Do not change Hide-absorbs-Shorten. Do not retune snapshot fill. Host `a04e2bd5`.

**Prior — effective vs frame RC1b ([`154310`](../../captures/session_20260903_154310.log)):**
After stop, draw-source sealed union does not append a linear row whose pitch+start already exists in the committed prefix. During capture, suffix upsert stays (`150650`). RC1 overlay list stays. Named Hide/Shorten sibling drop now includes closed suffix remnants (`000402`); open holds and gather upsert stay. Do not add gather `keepShortest`. Occupy HITL phase windows close OD2 on `Overdubbing stopped` / `ODUB,stop,enter` when `ST,OVERDUBBING→PLAYING` is missing ([`133818`](../../captures/session_20260905_133818.log)).

**Prior — effective vs frame RC1 ([`140729`](../../captures/session_20260903_140729.log)):**
Display overlay appends sealed companion ids across wraps so frame realizes already-correct effective Hide/Shorten. `lastOverdubSealCompanionIds_` stays last-wrap-only for admission. Overlay list is a transitional bridge, not a second effective owner. Device first-STOPPED two-length FAIL [`154310`](../../captures/session_20260903_154310.log) is RC1b (sealed suffix), not an RC1 revert.

**Prior — RC-D2 `resolveUs` 8 ms (measured only):**
[`122730`](../../captures/session_20260903_122730.log) OVERDUBBING `DFRAME resolveUs` min 2149 / median 8724 / max 15285 (16 of 31 ≥ 8000). Do not start an index. `#41` Stage 2 / off-clock seal stay out. Plan: [`display_draw_source_reuse_empty_bar_bugfix.md`](../Plans/display_draw_source_reuse_empty_bar_bugfix.md).

**Prior — Shorten same-start sibling drop ([`123236`](../../captures/session_20260903_123236.log), [`140729`](../../captures/session_20260903_140729.log)):**
Display Hide/Shorten apply drops a longer same-pitch+start closed sibling of the kept short row, including suffix remnants (`000402`). Device **PASS** for the original prefix-only slice: first STOPPED `src,frame` has no two-length pair (`63@0` reconstruct 144/576 → frame 144 only). Host `88d24b6c`. Not that RC: `77@336` frame 48 vs effective 16; `63@1680` frame 288 while effective omits it. Do not add gather `keepShortest`. Plan: [`overdub_paint_shorten_same_start_sibling_bugfix.md`](../Plans/overdub_paint_shorten_same_start_sibling_bugfix.md).

**Prior — RC-S retain+merge ([`120321`](../../captures/session_20260903_120321.log), [`122730`](../../captures/session_20260903_122730.log)):**
`Loop::retainCapturePreviewAtSeal` retains uncovered sealed rows, then merges this pass’s closed preview via `upsertDisplayNoteAtPitchStart`. Device **PASS**: wrap 2 `sealed` 63→159 (159 > lap-2 preview 106), `frame` 316→311, not visual+latest-lap (`225`). Live `hidden` vs stop `DIAG,oh hide` are different counters (122730 hidden=4 vs hide=1); not a reuse owner. Host `030e1a08`. Snapshot completeness is not a substitute.

**Prior — draw-source reuse RC-D3 ([`101721`](../../captures/session_20260903_101721.log), [`115237`](../../captures/session_20260903_115237.log), [`120321`](../../captures/session_20260903_120321.log)):**
Reuse copies `liveDisplayCommittedPrefix_` and applies overlays/pending on a frame-local copy. [`115237`](../../captures/session_20260903_115237.log) `hidden` 0→2 before wrap 1. `teensy41-capture-serial` already has `SESSION_CAPTURE_DRAW_SRC=1`. Display stays `resolveDisplayNotesByDrawSource`. Do not promote `liveDisplayNotes` to committed-source authority.

**Prior — occupy snapshot source ([DEC-052](../DECISION_LOG.md#dec-052--occupy-consumes-the-draw-source-predicate)):**
`rebuildOverdubSourceSpanCache` consumes `shouldGatherPublishedSnapshotForDrawSource`. Predicate true → exclusive published-`LoopSnapshot` fill (`from=snapshot`). Predicate false → prepared → prior → covering visual → passes unchanged. Plan: [`overdub_occupy_snapshot_source_refinement.md`](../Plans/overdub_occupy_snapshot_source_refinement.md). Native occupy fixtures in `test_overdub_source_view` PASS. `teensy41-capture-serial` RAM1 locals **6848**. Host `test_occupy_paint_serial_verify` PASS. Full native **1628/1630** — one pre-existing fail in `test_same_pass_playhead_tails_keep_closed_bar_four_end` (does not call occupy fill). Device: `overdub_occupy_paint` OD2 open `from=snapshot` when paint `snap!=0`; `occupy_od2_snap_not_zero` is paint/occupy divergence only. Stop totals emit `#CAP,DIAG,oh` so `add`/`shorten`/`hide` fit `kMaxCaptureTextBytes` (192); [`150143`](../../captures/session_20260902_150143.log) `overlap_hold` clipped `hide`. OVERDUBBING reconstruct stays off. Stage 4 stays closed. `#41` Stage 2 stays out.

**Prior — occupy prepared source pin ([`134815`](../../captures/session_20260902_134815.log)):**
`srcskip` `not_finished` because the LCR device gate is STOPPED-only and occupy-paint never idles STOPPED with committed passes (`sess=0 prep=0`). Opens were `from=visual`. [`113651`](../../captures/session_20260902_113651.log) `from=prepared,notes=8` is leftover LCR on a restamped finished session (`prep=3072` at OD1 open), not occupy-paint owner. Plan: [`overdub_occupy_prepared_source_incomplete_bugfix.md`](../Plans/overdub_occupy_prepared_source_incomplete_bugfix.md).

**Prior — occupy-paint HITL + second-overdub stale snapshot ([`092344`](../../captures/session_20260902_092344.log)):**
Preset `overdub_occupy_paint`: record seeds loop length (pitch 72). First overdub writes the half-bar phrase (48/52/56/60). Second overdub occupies that phrase: same-start 56 (Hide), +1/4 on 48/52 (Shorten + wrap), then −1/2 from that (+1/4 − 1/2 = −1/4 vs source) Hide + wrap. Host verifier is `verify_occupy_paint` (OD1 must not Shorten/Hide; occupy gates are OD2). Plan: [`overdub_paint_second_overdub_stale_snapshot_bugfix.md`](../Plans/overdub_paint_second_overdub_stale_snapshot_bugfix.md). Persist/`usbdisp` cadence stays a follow-up. OVERDUBBING reconstruct stays off. Stage 4 stays closed. RC2b stays parked. Source-view wrap-held [`013709`](../../captures/session_20260902_013709.log) stays a separate plan.

**Prior — source-view wrap-held wrong head Off ([`013709`](../../captures/session_20260902_013709.log)):**
Occupy `39@3024→448` at `hs=288` while storage WRAP is `3024→64`. Effective `39@288` len **2784**. A 4-event reconstruct does not emit `3024→448` (Off@64 blocked by intervening On@288; 3024 finishes to 3071). Pin the source-view merge / collapse / companion path next. Plan: [`overdub_source_view_wrap_held_wrong_head_off_bugfix.md`](../Plans/overdub_source_view_wrap_held_wrong_head_off_bugfix.md). Equal-tick consume shipped `932a58d6`.

**Prior — sealed survives current snapshot ([`012714`](../../captures/session_20260902_012714.log), recapture [`013709`](../../captures/session_20260902_013709.log)):**
`sealedCapturePreviewIsCovered` uses `visualCache.builtFromRevision` only. Sealed unions only while capture or the overdub session is active. `openOverdubSession` clears leftover record sealed. Keep-larger during a live session is reverted. Plan: [`overdub_paint_sealed_survives_current_snapshot_bugfix.md`](../Plans/overdub_paint_sealed_survives_current_snapshot_bugfix.md).

**Prior — stop gather keeps adopted linear ends ([`011906`](../../captures/session_20260902_011906.log)):**
`!captureActive` was too wide: wrap seal still has an overdub session, so gather fell back to record `visual=65`. Session flag stays. Keep-larger reverted after `012714`. Plan: [`overdub_paint_stop_snapshot_wrap_end_bugfix.md`](../Plans/overdub_paint_stop_snapshot_wrap_end_bugfix.md).

**Prior — closed occupy + stale snapshot paint ([`005439`](../../captures/session_20260902_005439.log)):**
Device PASS. Occupy frame `63→82`. Wrap 2 frame **131→132** with `snap=0`. Overdub stop published (`adopt_partial` **144**, DISP **157**). Log ends mid-`LoopPersist` (`SAVE,in_progress`) after capture Ctrl+C; no HardFault line. Plans: [`overdub_paint_closed_preview_suffix_hide_bugfix.md`](../Plans/overdub_paint_closed_preview_suffix_hide_bugfix.md), [`overdub_paint_stale_snapshot_after_wrap_bugfix.md`](../Plans/overdub_paint_stale_snapshot_after_wrap_bugfix.md). Reconstruct stays off. Stage 4 stays closed. RC2b stays parked.

**Prior — overdub paint capture handoff ([`234726`](../../captures/session_20260901_234726.log)):**
Reconstruct stays off. While capture is active, `gatherDrawSourceBars` unions `capturePreview` across the window. Plan: [`overdub_paint_capture_handoff_bugfix.md`](../Plans/overdub_paint_capture_handoff_bugfix.md).

**Prior — OD2 visual reconstruct gated during overdub ([`233642`](../../captures/session_20260901_233642.log)):**
Stage 3 `load_frame` is one phase, so VisualCache idle fitted during OVERDUBBING. DEC-045 gate is `allowVisualCacheReconstructSlice`. Device [`234726`](../../captures/session_20260901_234726.log) held `visual` **63** until stop `adopt_partial` **95**.

**Prior — display frame Stage 3 host shipped:**
[`display_frame_phase_split_windowed_paint_refinement.md`](../Plans/display_frame_phase_split_windowed_paint_refinement.md).
`DisplayManager::update` runs one paint phase per admitted LoadAndDisplay turn. Device [`233642`](../../captures/session_20260901_233642.log): `load_frame` **4.7–5.4 ms**; `DFRAME` wall p50 **12.8 ms**; transfer **4496 µs**.

**OpenSpec 4b.1 / 4b.2 / 4b.2c:** closed under Ring B round-robin
([`immutable-loop-snapshot-passes/tasks.md`](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md);
[`playback_window_round_robin_admission_refinement.md`](../Plans/playback_window_round_robin_admission_refinement.md)).
**4b.1** pass = one owner per cursor turn (no true same-turn snapshot/persist
stack). **4b.2** pass = first-record start after leaving bar 1 + live append
on the five-owner cursor. **4b.2c** is closed: capture
[`232633`](../../captures/session_20260826_232633.log) shows one in-flight
record construction (`construction=1`, `queued=0`, `applied=0`, `exit=1`,
`inflight=1`) through PLAYING/OVERDUBBING, then the final `pending=0` turn
publishes `root=0x7030ad1c` with `root_rev=1`/`playback_rev=1`. No later
PLAYING/OVERDUBBING `snapshot_idle` occurs. The apparent violation was the
first-root construction interval, not stacked successor overdub passes.
`DEC-045` remains the historical baseline; `DEC-047` supersedes its post-root
successor restriction only for the activated `3.5`/`3.6` slice. **4b.3**
remains open; exclude `T0/S6` when
scoring post-stop gaps. A capture-only `track_update` profiler now reports
per-track maxima and over-budget counts from `TrackManager::updateAllTracks`;
attribution from [`000834`](../../captures/session_20260827_000834.log):
PLAYING/OVERDUBBING `tracks`/`usbclk` peak is **11.3 ms** (T4 11114 µs,
T6 6520 µs); post-stop `usbdisp` **116.7 ms** is `usbtrans` 116729 µs,
not `updateAllTracks`. RAM1 locals **4640**.

**Snapshot slab packing (`DEC-048`):** `allocateSnapshotShared` bump-allocates
into 32 KB slabs owned by `Loop`. One mutable span root and one event root
are reused per `LoopSnapshotConstruction`. Slab deny fails construction.
Native fixture `test_snapshot_one_bar_node_count_and_unchanged_membership_share`
covers one-bar node counts and successor membership sharing. Native
**1484/1484**. `teensy41-capture-serial` RAM1 locals **5920**.

**Active OpenSpec slice:** task `3.5` firmware is in tree on
`feature/overdub-wrap-snapshot-publication`. Settled wraps queue bounded
mutation-journal fragments; `LoopSnapshotConstruction` uses persistent bar
cursors and fixed-capacity membership pages (16 refs × 8 pages). NoteOff
pairing copies the NoteOn `NoteId` before promotion. PLAYING/OVERDUBBING may
step wrap journals after the first root. Failed fragments are retained.
Task `3.6` remains open: native parity extensions plus the measured
`T_RT_reserve` device gate. Capture
[`173826`](../../captures/session_20260827_173826.log) showed successor
construction after the first wrap (`construction=2`, `root_rev=1`) but
`queued`/`applied` are pass-id fields and cannot tell wrap-journal fragments
from pass reconstruction. `snapshot_idle` now also emits `journals`,
`fragment=i/n`, and `parent_rev` (published revision when the front journal
parent is the selected root; `0` when the parent is an unpublished
candidate). RAM1 admission is restored without USB-host ownership changes:
`teensy41-capture-serial` now reports RAM1 locals **7008** (variables 91296,
code 420652, padding 5332). The follow-up admission fix now allows queued
session-exit snapshot work after the wrap journals drain, and allows the
bounded Snapshot owner to run when tracks are stopped but the transport clock
is still running. Focused native admission coverage passes; full native and
device recapture remain open. Construction now persists membership-copy,
mutation, append, and event-sort progress at one fixed-capacity membership
page per admitted unit; native snapshot/take-survival coverage passes and the
device recapture [`202635`](../../captures/session_20260827_202635.log) still
fails `T_RT_reserve`: `snapshot_idle` reached 79.169 ms because session-exit
start reconstructed a whole queued pass in one admitted unit. Existing journal
caps now also bound that start: `startNextPendingConstruction` walks committed
pass events through a persistent `CommittedEventRange` cursor and begins at
most one journal-capped change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) then published that
wrap candidate as soon as `journals=0` while `queued=3` session-exit passes
remained, so playback selected an incomplete snapshot and dropped the first
record pass. Wrap completion now keeps the candidate unpublished until queued
passes, construction progress, and companion changes complete. Device
recapture remains open.

**Prior — display watches parked after [`204043`](../../captures/session_20260901_204043.log)**
Not Now implementing. Reopen only if a later capture matches a gate below.
Closed on `204043` (do not reopen): DEC-051 seal-at-0; stop hide re-admit
of `63@768→1151`; wrap-held paint from the flatten `WRAP` scanner;
snapshot-current hide seam; same-start occupy vs reconstruct stacks.
Parked gates:
| Watch | Reopen if a future capture shows | Plan |
|------|----------------------------------|------|
| RC2b wrap-held rewrite | `draw_src` / frame turns a closed same-`NoteId` into wrap-held (`2296→672`, `2304→2976` / `576` / `1008`) | [`overdub_wrap_companion_and_pairing_bugfix.md`](../Plans/overdub_wrap_companion_and_pairing_bugfix.md) |
| `79@240` dual Off | compose/effective end **272** and drawn/frame end **288** with `SEVT` `F@272` and `F@288` | same + [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) |
| Stopped-frame miss | `69@3024` (or another pitch+start) in `SEVT` + effective, absent from stopped frame | [`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](../Plans/overdub_occupy_same_start_stop_hide_readmit_bugfix.md) |
| Held-note retention | pitch-64 (or equivalent) live hold 40/48 vs MIDI ~624 after seal | [`display_seal_handoff_capture_retention_bugfix.md`](../Plans/display_seal_handoff_capture_retention_bugfix.md) |
Do not patch reconstruct pairing, `applyCapturePlayheadTails` from the red
host pin, or `keepShortestEndAtPitchStart` on the draw-source gather unless
a gate above fires.

**Prior — same-start occupy paint is last overdub + occupy, not a remnant**
Closed as intended on [`204043`](../../captures/session_20260901_204043.log).
Reconstruct still stacks every same-start length (occupy needs both
`noteId`s). Frame/effective follow the last COORD hold, then occupy
Shorten/Hide: `63@0` last COORD `0→576`, later `336→576` Shortens to
exclusive end − 1 = **335**; `768→95` is the same rule (`864−1`).
`1104`/`1536`/`2304`/`69@48` keep the last overdub length (240 / 232 /
240 / 48), not reconstruct's shorter sibling. Do not reopen to force
`0→192` or keep-shortest.

**Prior — same-start Hide must not re-admit the record row at overdub stop**
Device **PASS** [`204043`](../../captures/session_20260901_204043.log):
frame `63@768` len 95; no post-stop `draw_src`; after snapshot-current
`DFRAME` stays 81. Host fixtures shipped. Plan:
[`overdub_occupy_same_start_stop_hide_readmit_bugfix.md`](../Plans/overdub_occupy_same_start_stop_hide_readmit_bugfix.md).

**Prior — DEC-051 overdub wrap seals at loop end ([`193232`](../../captures/session_20260901_193232.log))**
Stage 1 host clock shipped. Device **PASS**
[`201956`](../../captures/session_20260901_201956.log) and
[`204043`](../../captures/session_20260901_204043.log) (wraps at phase 0,
no `WRAP,2304,96`, no `DNTE 0→3072`, SEVT 125/125 linear pairs).
Stage 2 stays gated (no `0→L`). Plans:
[`overdub_wrap_bar4_note_off_session_seal_bugfix.md`](../Plans/overdub_wrap_bar4_note_off_session_seal_bugfix.md),
[`overdub_wrap_loop_end_commit_refinement.md`](../Plans/overdub_wrap_loop_end_commit_refinement.md).

**Prior — overdub wrap RC2b ([`192435`](../../captures/session_20260901_192435.log))**
RC5b device **PASS**: first **U:** `Overdub undone`, `DISP` 75→63. Classic
`2296→672` was not in [`204043`](../../captures/session_20260901_204043.log).
Host tails pin stays **red** (`2976` → `2296`); no tails firmware from that
pin. Parked — reopen only on the capture gates in the parked-watches table
above. RC3 overlay re-admit is closed on `204043`. Plan:
[`overdub_wrap_companion_and_pairing_bugfix.md`](../Plans/overdub_wrap_companion_and_pairing_bugfix.md).

**Prior — display seal handoff RC1 (`153517` coverage over-claim)**
Idle OverlayRejected fallback was stamping `visualCache.builtFromRevision =
playbackRevision` while the gather still carried the stale snapshot's content
(8 vs 10). Retention released and the painter flashed previous-pass notes.
`visualCacheGatherCoversPlaybackRevision` forbids that stamp; `slice_keep` also
no longer claims coverage. Release predicate unchanged.
Committed `c74b8e21`.
Plan: [`display_seal_handoff_coverage_and_wrap_pairing_bugfix.md`](../Plans/display_seal_handoff_coverage_and_wrap_pairing_bugfix.md).

**Prior — display seal handoff (capture retention)**
Recapture [`150650`](../../captures/session_20260901_150650.log) confirms the wrap
publication fix: `root_rev` catches `playback_rev` within 150–220 ms at every wrap
(1, 3, 5, 7, 9, 11, 13, 15, 17), phase-0 holds resolve (`hs=0` reports `a=1`,
`as=0`, `ae=576` from the second pass), and `DIAG,draw_src,diff` is empty. The
STOPPED frame still differs from effective on exactly the two held pitch-64 rows:
frame `s0 len40` / `s768 len48` against effective `624`/`624`, where MIDI input
gives NoteOn storage 0 → NoteOff 624 and NoteOn 768 → NoteOff 1392.

Root cause is a **capture → committed-tier lifetime gap**, not cache arbitration:
the cache's last rebuild before the frame was `slice_clean` at `52023305`, four
seconds before those notes existed, and there is no `slice_keep` in the capture.
The pass sealed at `56135261`, the frame was painted at `56215949`, and `root_rev`
17 published at `56288500` with the covering slice at `56311186`.
`commitPendingCapturePass` cleared `capturePreview` at the seal, so the rows left
the Capture tier before either committed tier carried them.

`Loop` retains closed preview rows at the seal
(`retainCapturePreviewAtSeal`) and releases them only when the committed
representation the painter selects proves coverage
(`sealedCapturePreviewIsCovered`): only `visualCache.builtFromRevision` proves
coverage. A current published snapshot is not membership (`012714` wrap 2).
A later wrap must not `clear()` uncovered sealed rows (`120321`); retain
keeps them and merges the new closed preview.
`resolveDisplayNotesByDrawSource` unions the retained rows into the committed
prefix through `upsertDisplayNoteAtPitchStart`, which replaces only at equal
pitch+start and never acts as a general newer-wins rule.
`keepShortestEndAtPitchStart` is untouched. Retention is a lifetime refinement of
the existing Capture tier: `Loop` owns the rows, display only consumes them,
`VisualCache` only reports coverage, and `DrawSourcePath` is unchanged.
Plan: [`display_seal_handoff_capture_retention_bugfix.md`](../Plans/display_seal_handoff_capture_retention_bugfix.md).
Host gates: `teensy41-capture-serial` RAM1 locals **6848**, native **1566/1566**.
Parked — [`204043`](../../captures/session_20260901_204043.log) has no pitch-64
holds. Reopen only if a later capture shows the held-note retention gate.

**Prior — overdub wrap snapshot publication (tasks `3.5`/`3.6`)**
Capture [`134803`](../../captures/session_20260901_134803.log) proved
occupancy converges (`src,reconstruct` 13 pitch-64 rows → `src,effective` 4:
`0→576`, `768→576`, `1536→512`, `2304→576`) while the drawn frame kept the
raw 144-length rows, because `root_rev` stayed **1** against `playback_rev`
3…11 for the whole overdub. `remainingSessionExitPublishWork` gates wrap
publication on `companionsApplied`, which only ever becomes true on the
session-exit path, so no wrap candidate could ever publish.
`remainingWrapPublishWork` now scopes the companion condition to
`sessionExitQueued`, and each wrap journal carries the `targetRevision`
stamped by `recordLastOverdubSeal` so a candidate is never selected for a
revision it does not contain. Host gates: `teensy41-capture-serial` RAM1
locals **6848**, native **1559/1559**. Device recapture open.

**Prior — snapshot-first bar painter (`DEC-050`)**
Occupancy resolves at NoteOn into `pendingNoteChanges_`
(`resolveOccupancyAtNoteOn`). Paint-only `pendingPaintHides_` is removed.
`upsertSourceTransform` is monotonically restrictive. Deferred stored-MIDI
verification emits `DIAG,dnte,src,effective` after raw reconstruct. Stage B
paint uses `resolveDisplayNotesByDrawSource`: Snapshot when
`publishedSnapshotIsCurrent`, otherwise VisualCache, plus Capture union on
live bars; pending transforms then playhead tails. Stage C logs
`DIAG,draw_src,diff` against the old compose path and is read-only — it saves
and restores compose frame bookkeeping around the comparison. Compose is not
deleted yet. Open on the VisualCache tier: `keepShortestEndAtPitchStart`
(idle slice and wrap adopt) keeps the shortest dual-storage sibling, so that
tier still contradicts effective geometry when the snapshot is not current.

**Prior — visual cache wrap-stale (Stage 1)**
([#41](https://github.com/Lytrix/MidiLooper/issues/41),
[`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log). Three wraps on a
7-bar loop (`live=5376`). No wrap `VCACHE,stale` with `dsz=0` while
`notes>0`. Only `lcr,src` is `why=open,from=visual` at overdub start. First
wrap `DISP` `183,111` → next frame `184,180`. Second wrap first paint
`249,246` (not a hold of 180). Contrast [`201556`](../../captures/session_20260831_201556.log)
`visual=206` / `frame=134`.
**Stage 1b — wrap-pair length:** [`202745`](../../captures/session_20260831_202745.log)
`DNTE` after `OVERDUBBING → STOPPED` shows the same chords at three
lengths (39/45/51 @ 240 as 208, 256, and 304). NOTE_EDIT paint reconstructs
with wrap pairing off and drops the extras. Visual-cache merge of a sealed
wrap pass now uses merged reconstruct; `appendOverdubPassWrapPairedNotes`
skips a longer sibling of an existing pitch+start. Device recapture is
required before calling 1b closed. Native **1514/1514**. RAM1 locals **6944**.
**Stage 1c — wrap-held display:** [`204132`](../../captures/session_20260831_204132.log)
second wrap occupy already has `39@192–592` and `39@336–592`. STOPPED DNTE
is short (`192/16`, `336/112`). NOTE_EDIT open
`rebuildVisualCacheFromPasses` then `VCACHE,full` 291 notes and focus
`presence=0`. Visual-cache wrap append is wrap-held heads/tails only.
Source view keeps full wrap pairing.
**Stage 1d — no wrap-tick visual merge:** shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): wrap 2 idle
`slice_clean` `dsz=0` while longs stay in `visualCache` (`DISP`
`243,243`). Overlay fill is not current committed.
**Stage 1e — idle-slice current fill:**
[`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md).
`rebuildVisualCacheIdleSlice` discards Overlay (`flat` only; no fallback
reset) and fills from prepared LCR or budgeted
`gatherCommittedEventsInWindow`. Playback Overlay contract unchanged.
Native **1518/1518**. RAM1 locals **6944**. Device recapture required
(`205917` shape: wrap 2 first neighborhood must match committed; wrap 3
must not be the first shorten).
**Stage 1f — STOPPED paint current fill:**
[`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md).
`resolveDisplayNotesCommitted` last-resort discards Overlay and falls
through to `gatherCommittedEvents`. Playback Overlay and
`rebuildDisplayNotesInWindow` unchanged. Native **1519/1519**. RAM1
locals **6944**. Device FAIL
[`220337`](../../captures/session_20260831_220337.log): first 8-bar
STOPPED `DISP` **276 / 344** (no `paint_src`); wrap/stop
`stale_range` is `dcnt=8` on an 8-bar loop; PLAYING Snapshot idle then
**334 / 334**. Third stop `OverlayRejected` then **80 / 91**. Play is
still the first match.
**Stage 1g — wrap dirty span:**
[`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md).
`markTickSpanDirty` marks wrap-held (`endTick < startTick`) On/Off bars
only. Linear pass spans stay inclusive. Native **1521/1521**. RAM1
locals **6944**. Device [`221925`](../../captures/session_20260831_221925.log):
4-bar loop, wrap `dcnt=4` from linear 39/45/51 in every bar (not a
wrap-interior fail). Wrap 1 `slice_clean` `63→102`, wrap 2 `102→124`.
Wrap 3 idle `OverlayRejected`/`src,None` — no `slice_clean`. First
STOPPED `DISP` **128 / 137** (no `paint_src`). Stop `overlap_hold`
`hide=8`. `DNTE` stacks `39@48` and `39@432` at **144 and 256**. MIDI 47
(B2) is absent. 1g does not own hide or wrap-pair length.
Recapture [`222349`](../../captures/session_20260831_222349.log) (same 4-bar
loop, later overdub): B3 is the only pitch held across stop (`MI` ON
`325318691`, seal `325433453`, OFF `325506187`); `CLN,wrap_synth,0`.
Each wrap first `DISP` drops (`102→67`, `119→101`, `135→121`, `157→136`,
`162→150`) then `slice_clean` restores. First slot0 STOPPED `DISP`
**159 / 31**; `paint_src,OverlayRejected` then **159 / 166**. No `DNTE` /
`SEVT` / `WRAP` dump in this file — length-to-loop-end is not in the log.
**Stage 1h — held-note wrap pair:**
[`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md).
`appendCanonicalSpansFromMidiRange` stack fallback assigns `Off@0` only
when the last On is wrap-held (`startTick >= tailStart` and
`isPreferredWrapTailForHeadOffInSpanBuild`). Mid-loop B3 `1856` keeps
`F@1912` / `F@1920`. `finalizePendingNotes` still closes at playhead.
Native **1523/1523**. RAM1 locals **6944**.
**Stage 1i — wrap/stop first paint:**
[`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md).
Dirty overdub layer holds before snapshot window gather. STOPPED assigns
dirty nonempty `visualCache` when the composed handoff is gone (slot
mismatch). Last-resort Overlay reject stays for empty cache.
Native **1525/1525**. RAM1 locals **6944**. Device FAIL
[`230400`](../../captures/session_20260831_230400.log): wrap 1 `105→66`
(cache 63). Hold is committed-prefix only; sealed capture is gone. Next: 1k.
[`225243`](../../captures/session_20260831_225243.log) first STOPPED
**147 / 198** kept RC5a handoff (same slot); the STOPPED hold did not run.
**Stage 1j — stop handoff playhead tail:**
[`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md).
`applyCapturePlayheadTails` writes `liveDisplayPendingPaintNotes_` only.
`liveDisplayNotes` stays at capture-preview lengths so RC5a does not freeze
`endTick = currentTick` (`225243` D#2 ON@624 → stop phase 704). Unmatched
`N@2928` / `F@0` → `loopLength` is a later reconstruct RC.
Native **1526/1526**. RAM1 locals **6944**.
Device [`231953`](../../captures/session_20260831_231953.log) dump gate
PASS: `DIAG,dnte,src,reconstruct,count,221`, `src,frame,count,160`,
`WRAP` 144, `SEVT` 707. First STOPPED **160 / 148** matches the frame
dump. Stop `COORD` storage **1168**; frame dump has no note ending at
1168 (1j playhead freeze not on that frame).
**Stage 1k — wrap compose hold:**
[`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md).
Grow compares cache to `max(committedCount, composed liveDisplayNotes)`.
**Stage 1l — Off@0 leftover tail pair:**
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md).
Leftover tail-window On + leftover `F@0` wrap-splits to `L-1` (not
`tryPairWrappedTailOn`). Mid-loop linear pairs stay linear (1h).
**Stage 1b / occupy hide on hold (`003616`):**
[`overdub_occupy_hide_on_hold_current_tick_bugfix.md`](../Plans/overdub_occupy_hide_on_hold_current_tick_bugfix.md).
Occupy On / playback cross upserts a **paint-only** Hide
(`pendingPaintHides_`) so the earlier note drops on the currentTick frame.
**Wrap-seal RC (`005757`):**
[`overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md`](../Plans/overdub_wrap_seal_occupy_hide_deletes_shorten_bugfix.md).
`commitOverdubWrapAtSessionStart` must not seal occupy Hide as Delete.
OverlapNoteOff accumulate is Shorten / Length again. NOTE_EDIT
`OverlapNoteOff` stays Shorten. Parent unique-key revert:
[`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md).
**NoteOff OverlapNoteOn (`010926` RC1):**
[`overdub_wrap_occupy_noteoff_shorten_not_delete_bugfix.md`](../Plans/overdub_wrap_occupy_noteoff_shorten_not_delete_bugfix.md).
`accumulatePendingNoteChangesFromSourceNotes` Shortens OverlapNoteOn to
occupy exclusive end − 1. CompleteCover still Hides / Deletes.
**norowid paint + wrap cache overlay (`010926` RC2):**
`upsertPendingHidesForOccupiedNoteIds` falls back to present-at-hold
source-view rows when ledger ids have no source-view row. Idle slice,
`adopt_partial`, wrap append, and grown-cache assign apply pending paint
so wrap first `DISP` does not restore occupied rows. Focused native
pending **72/72**. Native **1539/1539**. RAM1 locals **6848**. Device
recapture required (`010926` shape).
**NoteOn paint + capture mirror (`104129` / `110022` / `111343`):**
[`overdub_occupy_on_noteon_paint_immediate_bugfix.md`](../Plans/overdub_occupy_on_noteon_paint_immediate_bugfix.md).
Geometric present-at-hold fallback when ledger occupy is empty (`n=0`).
Over-budget overdub resolve still mirrors `capturePreview` and playhead
tails, including after a committed-layer rebuild. Wrap adopt: paint Hide
on committed prefix only; same-start dedupe does not drop longer live
capture over shorter committed sibling. Compose counts clamp to the
vector after hide/dedupe; `liveDisplayNotes.resize` never grows
(`111343` `DNTE,0,0,0,0`). Overdub playhead tails stay on over-budget
frames (`112806`). Native **1544/1544**. RAM1 locals **6848**.
Device recapture required. Stop reconstruct still shows unshortened
first-overdub E4 `760–1920` beside later `768–144` (`overlap_hold`
`shorten=0`).
**Stage 1m — stop compose adopt (`000013`):**
[`display_visual_cache_stop_compose_adopt_bugfix.md`](../Plans/display_visual_cache_stop_compose_adopt_bugfix.md).
Other tracks STOPPED first and cleared `liveDisplayNotes` (141→31). Adopt
compose onto the outgoing loop when it is larger than that loop's
`visualCache` on a short loop. Wrap-hold adopts so cache tracks compose
(wraps 8–11). Full-loop idle must not replace a larger cache with a
smaller slice (`slice_keep`, Prepared 95→80). Occupy `hide=2` is not this
slice. Device recapture: first STOPPED on the overdub loop ≥ last
OVERDUBBING compose; later PLAYING must not fall to 80; cache must not
freeze at 95 while compose grows.
Native **1537/1537**. RAM1 locals **6848**. Occupy hide is in tree
(source-view reconstruct keeps both `noteId`s; adopt/idle keep the shortest
display end). Device recapture required for [`002145`](../../captures/session_20260901_002145.log)
`norow` 75/76 and pitch-63 frame 96+288.
Device [`002145`](../../captures/session_20260901_002145.log) (4-bar `len=3072`):
**1k PASS** — every wrap first OVERDUBBING `frame` ≥ last-before (odub1 wrap 1 `71→72`; odub2 wrap 1 `94→95`).
**1m PASS** — odub1 last `88/80` → `adopt_partial` **88** → first STOPPED **88/88**. Odub2 last `145/144` → `adopt_partial` **145** → first PLAYING **145/145**. Cache tracks compose (`88→94→103→113→120→128→136→144`). Settled STOPPED **145**, not 80. Global stop still paints other slot-0 loops (`32`, `42`, `50688/1429`) then returns to **145**.
**1b** reconstruct **PASS** (0 same-start stacks, count 73). Frame dump **FAIL** on that firmware: pitch **63** at `0/384/768/1152/1536/1920/2304/2688` still has lengths **96 and 288** (frame 88).
**1l not in this log** — no `SEVT` `F@0`.
Stage 2 is unblocked and not started. Do
not start all-track idle fan-out,
`T4 track_update`, N / pages knobs, or OpenSpec `3.5` / `3.6` checkoff.

**Shipped — live-record skip parent note lookup**
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)):
`fromCaptureStore` fragments do not call `findNoteAtPitch`. `openNotes` is
the sole pairing mechanism; the parent is not an identity source. Session-exit
keeps the lookup. Device [`192422`](../../captures/session_20260831_192422.log):
32 live-record starts, `note_lookup=0`; after-first `start` 167–233 µs.
The [`190654`](../../captures/session_20260831_190654.log) lookup-driven
`start` growth (375→1007 µs) is gone.

**Shipped — live-record snapshot start bound**
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)):
RECORDING `startNextPendingConstruction` walks `capture.store` from
`appliedLiveRecordEventIndex` through `tick < includeUntil` and emits one
journal-capped fragment via `appendNextPassChangeFragment`. No full-store
`copyEventsTo` / reconstruct. `openNotes` persist across fragments and bars.
Row-22 admission stays one start XOR one step. Native take-survival live-record
fixtures pass. Native **1507/1507**. RAM1 locals **6944**. Device non-growth
capture is required before calling the RC closed. Do not freeze
`T_RT_reserve`. Do not raise N.

**Shipped — overdub-stop incremental admission (`DEC-049`)**
([`overdub_stop_incremental_admission_bugfix.md`](../Plans/overdub_stop_incremental_admission_bugfix.md)):
`stopOverdubbing` reanchors traversal and keeps a still-valid full-loop
merged stream. The next `playCommittedLoopMidi` may admit the Rank-6 seal
receipt incrementally. `playback_build` labels `snapshot` / `overlay` /
`compatibility` from the branch actually taken. While a publication
backlog is present, Snapshot idle is 2 of every 6 turns. Native **1503/1503**. RAM1 locals **6944**.
Device [`152513`](../../captures/session_20260831_152513.log): T4 16-bar
Stage 2 PASS. Device [`153113`](../../captures/session_20260831_153113.log):
T4 11-bar Stage 3 drain PASS — 16.197 s after the last stop,
`pending=0` / `root=0x703fadf0` / `root_rev=7` / `playback_rev=7` /
`publish=9`, then `playback_build,snapshot`. Device test: Snapshot idle
passes `stepPendingSnapshotPublish(2)`
(`MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn`). Recapture
[`161532`](../../captures/session_20260831_161532.log): T4 16-bar, last
stop `incremental` `rev=8`, then 1.824 s / one `BAR` to
`pending=0` / `root_rev=8` / `playback_rev=8`. That drain already had
`root_rev=1`; it is not the empty-root 16.197 s class of
[`153113`](../../captures/session_20260831_153113.log). `start+step`
same-turn count is 61; parent max **13.350 ms** (`start=12891` +
`step=423`); 7 parents ≥ 8.82 ms. Drain-window parent max **4.371 ms**.
Device test now: `stepPendingSnapshotPublish(2)` and
`kSnapshotMembershipMaxPages = 16`. Recapture
[`165637`](../../captures/session_20260831_165637.log) (N=2, 16 pages):
T4 16-bar, last stop `incremental` `rev=9`, then **1.885 s** to
`pending=0` / `root_rev=9` / `playback_rev=9` with `root_rev=1` already
present. `start+step` = 58; parent max **12.138 ms**; 3 parents ≥ 8.82 ms.
Drain parent max **4.413 ms**. Compare
[`153113`](../../captures/session_20260831_153113.log) (N=1, 8 pages),
[`161532`](../../captures/session_20260831_161532.log) (N=2, 8 pages),
and [`164448`](../../captures/session_20260831_164448.log) (N=1, 16 pages).
`164448` + tail [`165027`](../../captures/session_20260831_165027.log):
T4 is 16 bars / 12288 ticks from full-loop `incremental`. `start+step` = 0.
Last stop `rev=13` @ 559.185 s; publish @ 608.693 s (**49.508 s**)
`pending=0` / `root=0x70498e30` / `root_rev=13` / `playback_rev=13` /
`publish=8`. `165027` has a 40.114 s gap with no `snapshot_idle` between
`journals=13` `fragment=9/18` and `journals=0` `applied=3`. Drain parent
max 4.330 ms in `164448`; tail parents ≤ 0.493 ms. 4 live-record parents
≥ 8.82 ms (start only, max 10.887 ms). ExecutionBounds still documents 8.
Do not freeze `T_RT_reserve`. Do not check off OpenSpec `3.5`/`3.6`.

**Shipped — wrap-tick derived rebuild**
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)):
Device recapture [`135510`](../../captures/session_20260831_135510.log):
wrap child line `total=41851,seal=36025,pass_apply=676,src_append=5150`;
`track_update[4]=41902` in that window (not the [`005527`](../../captures/session_20260828_005527.log)
77.618 ms class); no `why=wrap`; Rank 6 gather `playback_build` 7547 µs at
`57711324` is the next-tick owner this slice removes. Native **1496/1496**.
RAM1 locals **6944**.

**Immediate Stage 1**
([`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md)):
`MidiServiceDrain::allowSnapshotIdleStep()` now admits pending session-exit,
in-flight, or wrap-journal Snapshot work in `STOPPED` even when
`visualCacheNeedsFirstPaint` is true. `STOPPED` ends active MIDI ownership;
it does not mean pending snapshot work is complete. Native policy fixture
`test_stopped_pending_snapshot_wins_over_first_paint` covers that
combination. Device
recapture [`214832`](../../captures/session_20260827_214832.log) does **not**
close Stage 1: wrap journals drained with `queued=4` and `root=0x0`, then
STOPPED first-paint `VCACHE` ran and no `STOPPED` `snapshot_idle` appeared.
[`215420`](../../captures/session_20260827_215420.log) also holds completeness
(`root=0x0`) and also has zero `STOPPED` `snapshot_idle` across a 16 s hold;
that hold is not first-paint (`VCACHE` `slice_clean` `notes=166` `dirty=0`
before stop, no post-stop `VCACHE`).
[`215845`](../../captures/session_20260827_215845.log) repeats that on a
120 s STOPPED hold: last PLAYING idle still `pending=1` `queued=3`
`journals=2`, then persist/`idle_maint` only, no `STOPPED` `snapshot_idle`.
STOPPED Snapshot admission now scans every slot and selects a track with
pending publication instead of the visual idle cursor. Device recapture
[`221054`](../../captures/session_20260827_221054.log) **closes Stage 1**:
first STOPPED `snapshot_idle` at +0.070 s, candidate stayed `root=0x0` until
+77.765 s, then `pending=0` published `root=0x70400ac8` / `root_rev=10`.

**Immediate Stage 2 leftover named**
([`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md)):
Device recapture [`225659`](../../captures/session_20260827_225659.log)
closes the 224842 leftover: record-stop PLAYING idle at 30.164 s is
**20.984 ms** with `discard=15262` (`discardPendingIfLoopLengthChanged`),
`start=5704`, `enqueue=5`, exclusive leftover **13 µs**. Exclusive leftover
after `enqueue`/`discard` is ≤ 39 µs on every paired row. `enqueue` max is
14 µs. `emit` max is 52 µs and sits after the parent `us` stamp.
STOPPED session-exit `start`/`pass_change` reaches **19.996 / 19.968 ms**.
`step` max is 5.992 ms. Completeness held (`root=0x0` until the final
`pending=0` row). Publication closed at 183.121 s:
`root=0x70431844` / `root_rev=8` / `finish=671` / `publish=10`.
First STOPPED idle +0.066 s. Child-line budget held (31701/31701 complete
child rows). The Stage 2 falsification review is recorded in the plan.
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): first PLAYING
idle after record stop is `discard=47` (was `15262` on
[`225659`](../../captures/session_20260827_225659.log)). The candidate
stayed `root=0x0` until `pending=0` / `root=0x703248cc` / `root_rev=1`.
RECORDING used 2669 idles / 234 ms CPU, not a prefix rebuild. Native
fixtures cover the 9216/21504 fit pair and the applied-ticks overflow
discard. Host gates after inner timings: capture-serial RAM1 locals
**6944**. For
`startNextPassChangeFragment`, device recapture
[`004119`](../../captures/session_20260828_004119.log) names the 21 ms
STOPPED session-exit as `note_lookup=18293` on `pass_change=21050`
(86.9%). Every ≥10 ms fragment is 82–88% `LoopSnapshot::findNote`.
Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max **857 µs** (was 18293 on
[`004119`](../../captures/session_20260828_004119.log)); `pass_change`
max **3.219 ms**. Tail
[`005819`](../../captures/session_20260828_005819.log) publishes
`pending=0` / `root=0x70413fec` / `root_rev=10`. Same-pitch scan and
refill remain. No `step` rewrite is
justified yet. Host gates: capture-serial RAM1 locals **6944**; native
**1495/1495**.
Per-turn reserve measurement remains necessary, but freezing the minimum
observed reserve is rejected as a guarantee. Do not remove the +16
live-record bars. Do not check off OpenSpec `3.5`/`3.6`.

Direct consumer migration, persistence, and snapshot-identity undo remain
later phases. Decision:
[`DEC-047`](../DECISION_LOG.md#dec-047--bounded-overdub-wrap-snapshot-publication).

**Separate capture finding:** [`214703`](../../captures/session_20260826_214703.log)
confirmed `CLN,overdub,wrap_synth,3` after `finalizePendingNotes` reported no
pending notes/off events. Stored data contained existing head offs such as
`F@112` plus synthetic `F@6143` (`loopLengthTicks - 1`) for the same notes.
Track this in [`overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md);
do not fold it into 4b.2c.

**Immediate slice (side track):** Slice 1A measure gate **FAIL** on
[`213537`](../../captures/session_20260826_213537.log). Decomposition
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
**D1** is **closed** — native + device PASS
[`221655`](../../captures/session_20260826_221655.log): switch track 0→1
while `chan resume=2560`, **0** `reset,dirty`, resume `chan 2568` on return,
then `Complete` at 178.738 s. Next LCR-prepare step would be **D2 design**,
but maintained-LCR prepare is not the snapshot north star; do **not** start
D2 firmware or 1B unless re-authorized. Parent remains
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)).

G1 sizing is **satisfied** (doc-only). `representativeRebuildWall` = 358 µs
from [`202654`](../../captures/session_20260826_202654.log) `T4/S0`
`path=snapshot`; `prefetchTicks` = **272**; horizon stays playhead-centred
(`H_fwd` = 768). `T0/S6` compatibility is excluded. Slice 4 / Ring A firmware
is not authorized. Residual **4b.1b** (indivisible
`NoteUtils::reconstructDisplayNotes` in `Loop::rebuildVisualCacheIdleSlice`)
stays **parked**. Do not make a cooperative reconstruct state machine.
STOPPED/PLAYING post-overdub long-tail stays out of scope.

Ring B/RC2 shipped: five deferred owners rotate through one unconditional
cursor turn (snapshot step, visual-cache slice, wrap append, deferred
persistence, load/display). Visual-dependent turns skip only while wrapped
append is pending. RC2 refuses uncovered visual-cache reads on the overdub
paint path and falls back to committed-pass reconstruction when a dirty cache
cannot cover the overlap source span. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings for 4b.1b live on the owner-boundary plan.

OpenSpec **4b.2** remains the first-take `loopLengthTicks` behavior:
snapshot idle starts from `capture.store` only after the playhead has left bar
1, then appends each later completed bar when the unpublished parent still has
tick room. Live RECORDING does not rebuild the settled prefix when the parent
is too short; the bar still being written is not included.
[`114012`](../../captures/session_20260826_114012.log) started at
`record_length=768` while bar 1 was still being written.
[`120020`](../../captures/session_20260826_120020.log) started after bar 1
and did not restart `snapshot_idle` during RECORDING; `DISP` still jumped
on every quantized `record_length` step. First-take display length stays
the continuous playhead.
[`121754`](../../captures/session_20260826_121754.log) rebuilt the whole
settled prefix on each later length step (2611 RECORDING `snapshot_idle`
steps, BPM drift, post-stop `midi_gap` 361 ms).
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING with repeated 2.6–27.4 ms
`snapshot_idle` turns and `midi_gap` spikes (35 ms, 432 ms); cadence
alternation is in tree for device verify.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long PLAYING/OVERDUBBING visual idle reconstruct
bursts (`idle_reconstruct` ~145–217 ms; `midi_gap` 415 ms OVERDUBBING,
242 ms PLAYING, 451 ms STOPPED). In tree: unpublished snapshot contention
keeps visual fallback constrained and cadence-gated (1-bar constrained
slice on every 4th eligible turn) while snapshot updates stay active.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms and 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows. Refinement in tree: unpublished visual fallback constraint now
applies whenever unpublished snapshot work is pending (not only in-flight),
and live-record snapshot append starts now admit at most one completed bar
of new range per start to keep setup cost small.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
owners are still active: `snapshot_idle` and `PERS,work` still share turns
(`snapshot_idle us=26823` at 147.341 s then `PERS,work payload_slice=20806`,
plus repeated alternation while pending), OVERDUBBING still reaches
`midi_gap=236090` with `usbdisp=143649` / `load_frame=25638`, and STOPPED
reaches `midi_gap=430552` with `usbdisp=538063`, `load_frame=401498`,
`idle_maint=337246` while `ODUB,stop,display` remains 21.9-50.7 ms. The
same capture records `timing_max,DisplayResolveLiveCapture,215619`. In tree:
post-stop lane now blocks deferred save on turns where snapshot already ran;
device recapture is still required and 4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer shows
`PERS,work` while `snapshot_idle` is active (same-turn overlap not observed in
that run), but 4b.3/4b.4 owners remain: post-overdub PLAYING still reached
`midi_gap=175230` (`usbdisp=121603`, `load_frame=14728`, `late_on=184364`),
STOPPED reached `midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`,
`late_on=30491`), and `timing_max,DisplayResolveLiveCapture` remained high
(`157754`). In tree for 4b.3: post-overdub drain now inserts MIDI polls during
`runDeferredLoadAndDisplayFrame` (after display and load-job slices) so
`load_frame` does not monopolize an entire drain turn before the next USB
dispatch. Device recapture required.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (e.g.
`snapshot_idle us=7360` then `PERS,work 86`), plus OVERDUBBING windows with
`midi_gap=200549` (`usbdisp=129828`, `load_frame=23600`, `late_on=26036`) and
`midi_gap=196173` (`usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were
26-52 ms class after stop-display lines (display 19.6-51.4 ms). In tree for
4b.1: lane arbitration now includes RECORDING alongside PLAYING/OVERDUBBING
for pending snapshot work, and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection (not transport-only).
Device recapture required.
[`134438`](../../captures/session_20260826_134438.log) showed no
`PERS,work` entries (`0` matches) while `snapshot_idle` remained active, so
same-turn overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`, and STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open:
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now caps per-call dispatch batch size to `32`
(from `128`) while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns cannot spend a full 128-message USB dispatch burst in
one `usbdisp` slice. `pio run -e teensy41-capture-serial` and
`pio test -e native` PASS; device recapture required.
[`135317`](../../captures/session_20260826_135317.log) kept `PERS,work=0`
while `snapshot_idle` remained active (4b.1 still open until repeated holds).
4b.3 remains open: first post-stop cycle stayed bounded at
`ODUB,stop,display=15428`, but later `midi_gap=181392` occurred in
OVERDUBBING after PLAYING re-entered overdub; later cycles were
`midi_gap=42033` and `22132`. 4b.4 remains open:
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`rebuildOverdubCommittedDisplayLayer` now avoids snapshot gather+reconstruct on
the overdub paint path when `visualCache` already has notes (falls through to
held-layer/window-filter branches), bounding this owner before recapture.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS
(`RAM1 free for local variables: 4608`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but OVERDUBBING still reached `midi_gap=133468` in first-bars windows. 4b.4
improved strongly in this run: `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` while nearby PLAYING `DISP`
totals were `240`/`253`/`308`) and a stop-path restamp miss
(`seal_publish_span_cache_restamp ... invalidated_missing_id` with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from a clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; device
recapture required.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and the
wrap-reopen source drop (`src,why=open ... notes=0`) remains under active fix.
[`141838`](../../captures/session_20260826_141838.log) **reopens 4b.4** and names
two root causes — design session doc
[`playback_window_round_robin_admission_refinement.md`](../Plans/playback_window_round_robin_admission_refinement.md),
**no firmware authorized** (architecture checkpoint answers yes on ownership,
transitions, and execution bound; needs `DEC-046`).
**RC1 (bar 4–8 stalls):** one `TrackManager::updateAllTracks` call ran **344 ms**
inside clock dispatch (`midi_input`/`clk`/`tracks`/`usbdev`/`usbdisp`/`usbclk`
all `344xxx,3`; `midi_gap` only `26165`), producing `late_event,off,264136,4608`.
`PlaybackMergedMidiEventsRebuild=41` over 22 bar edges is **two** content-sized
`ensurePlaybackMergedMidiEventsBuilt` rebuilds admitted in the same interval,
because the reuse test expires exactly one bar after the last rebuild for every
enabled slot (9 enabled here). ~314 ms is unattributed: `recordPlaybackRebuild`
emits the **last** pending sample, not the max, and `cer_range`/`gather_*` are
compiled out at `SESSION_CAPTURE_INVESTIGATION_DIAG=0`.
**RC2 (overdub notes vanish at wrap):** wrap only records a pass id, no publish
happens between the record seal (52.157 s) and the stop seal (91.592 s),
`visualCache` is left dirty record-only (`VCACHE,stale,notes,270 … dcnt,18`),
`deferredDerivedViewMaintenance` excludes OVERDUBBING so the dirty bars never
drain, and the **4b.4 guard** `loop.visualCache.notes.empty()` in
`rebuildOverdubCommittedDisplayLayer` blocks the snapshot gather after
`visualCacheCoversFollowWindow` already failed — the exact condition task 4.4
forbids above. `DISP` `visualNotes` stays at `270` until
`STOPPED,…,504,504` at 106.118 s. Same stale cache also seeded overlap-resolve:
`srcafter,why=wrap,from=cache,after=240` against true content `452`.
**RC3 (separate, not bundled):** `snapshot_idle` burned ~1.0 s CPU per 2 s wall
from 16 s to 77 s (2992 turns) and published nothing.
**Review round 1 outcome:** architecture direction and the RC1/RC2/RC3
separation accepted; four changes applied to the design doc before `DEC-046`.
(1) **No bound reclassification** — Ring A caps admission multiplicity, which is
not an execution-bound class, so `ExecutionBounds.md` rows 1 and 2 both stay
`Unbounded` and no fourth class is introduced (DEC-043). (2) `prefetchTicks` is
**derived**, not chosen: `(slots-1) x clockInterval + worstCaseSingleRebuild +
margin`, binding at the **highest** supported tempo (`ClockManager` clamp
`[20,300]`). The originally proposed `TICKS_PER_BAR/4` is short at 120 BPM / 9
slots (500 vs 511 ms) and short by 2x at 300 BPM — gate **G1** blocks Slice 4
until the per-rebuild worst case is measured. (3) RC2 promoted to invariant
**R** (a derived representation may satisfy a query only when its coverage
corresponds to the requested interval and revision); the three repairs are its
enforcement points, and the `rebuildOverdubSourceSpanCache` repair is a
**semantic** gate independent of the display gate. (4) Missed-prefetch policy is
explicit (invariant **A1**): keep the existing immutable window, **never** a
synchronous rebuild on the clock path, and count
`DIAG,playback_window_starved` as an architecture failure. Slice order is now
measurement closure (0+1) → representation repair (2a/2b) → admission (3, 4).
**Review round 2 outcome:** sizing basis narrowed, rings unchanged. Ring A's
population is now the **playback-relevant** destinations whose window needs
preparation — a dynamic demand-driven set, not all enabled `(track, slot)` pairs
(invariant **A2**: no playback-relevant destination needing refresh may be
indefinitely displaced). A **design envelope** (16 tracks x 2 active slots,
≤ 240 BPM) is stated separately from absolute capacity; the 64-destination /
300 BPM calculation is **withdrawn as a blocking gate** and deferred to capacity
documentation. G1 is now representative envelope sizing, not worst-case
characterization. Investigating the reopened horizon question found
`ensurePlaybackMergedMidiEventsBuilt` centres the window on the playhead
(`winStart = playhead - winLen/2`), so the **forward horizon is already 1 bar**
(768 ticks) and 1-bar total content would halve it — the available lever is
window **bias**, not size. Within the envelope (`H_fwd`=1000 ms at 240 BPM) the
arbitration term is 73 / 156 / 323 ms at 8 / 16 / 32 eligible destinations, so
the design space is comfortable to ~16 even against the stricter
`lead <= H_fwd/2` target. Open architectural question of this phase is now
**what constitutes a playback-relevant destination** (`updateAllTracks` gates
only on `engineShouldRun(slotEnabled[i][s])` today, independent of track state),
plus horizon/bias.
**Review round 3 outcome:** four tightenings applied; sizing basis accepted.
The `(N-1) x T_clock` arbitration term is now labelled a **conservative** bound
for continuously-due destinations, not the expected steady state (self-staggering
is verified in Slice 4, not assumed), and the 32-destination row is the normal
upper envelope rather than a Ring A design target. **A1** is split into invariant
("under the design envelope, a playback-relevant destination must have its
replacement ready before its current window expires") and failure policy
(existing immutable window stays selected; no synchronous rebuild on the clock
path), so the failure branch is not unreachable by definition. Ring B's owner-3
dependency skip is given **normative non-priority semantics**: a skip consumes
the cursor position and transfers no turn. Content window **stays at 2 bars**;
only bias is open. **2a is not shippable ahead of Ring B** (Slice 3) — it lands
with native correctness tests and its device gate closes after Ring B, unless
recapture shows the restored gather path is unexercised (no
`DisplayCaptureFullGather` growth, `timing_max,DisplayResolveLiveCapture` inside
the 4b.4-closed class of 8608 us).
**Design session (round 4) — playback relevance derived; last blocking item
closed.** Finding: **no track-state check exists anywhere on the
playback-window path.** `PlaybackMidiOutput::engineShouldRun` is
`return slotEnabled;`, `Track::getEffectivePlaybackTick` returns the global
`currentTick` regardless of state, and `playMidiEvents` /
`playMidiEventsForSlot` return early only on `isStoppedRecording()` and
`!hasCommittedPasses() || loopLengthTicks == 0` — then
`playCommittedLoopMidi` calls `ensurePlaybackMergedMidiEventsBuilt` *before*
the `mergedEvents.empty()` return. So any enabled slot with committed content
rebuilds its 2-bar window once per bar forever, playing or not.
`141838` confirms it: only **one** track ever changes state (all nine
`ST,Track` lines), nine slots were enabled at boot, and
`DIAG,playback_build` window starts advance exactly one bar per ~2 s
(`0, 768, 1536, 2304, …`) from 15.409 s — record start. No spurious emission:
MO on every channel except the recorded channel 5 is 9–10, matching the nine
transition-driven all-notes-off broadcasts. Pure waste, which is why it never
surfaced as a correctness bug.
**Obligation** exists in `TRACK_PLAYING` and `TRACK_OVERDUBBING`; not in
`EMPTY`/`ARMED` (nothing started) or `STOPPED_RECORDING` (already excluded in
code). **`TRACK_STOPPED` is the leak.** Mute/solo/slot-mute do **not** remove
obligation — `PlaybackMidiOutput.h` documents that they suppress send, not the
engine, and the engine must keep cursor and ledger coherent for unmute.
Active-vs-layered selects `PlaybackEmitPolicy`, not obligation.
Predicate: `slotEnabled && trackPlaybackEngineRunning && hasCommittedPasses &&
loopLengthTicks > 0`, owned by `TrackManager` in
`TrackManagerPlaybackPolicy.cpp` as the engine-side counterpart of
`isTrackAudible` — no new module.
**Queued-destination prewarm keeps its commit-boundary exemption** (open item 3
resolved): `updateAllTracks` already refuses to commit a slot switch while
`!isPlaybackMergedMidiEventsReadyForSlot(targetSlot)` on a long loop, and
prewarm runs off the clock path via `ensurePlaybackMergedEventsForSlot`.
**Remaining open:** item **1a** — whether `TRACK_RECORDING` joins the engine-
running set for *other* enabled slots on the same track (product decision), and
item **2** — window bias (needs the LoopEnd-launch/clock-catch-up backward
margin requirement). Content window stays at 2 bars.
**`DEC-046` ACCEPTED 2026-08-26** as architecture direction — it does **not**
authorize firmware Slices 1–4. Item 1a resolved: **`TRACK_RECORDING` is included**
(layered-slot playback during recording is intentional), so `TRACK_RECORDING`
joins PLAYING and OVERDUBBING in `trackPlaybackEngineRunning`; the recorded loop
stays excluded by lack of committed passes. Only item 2 (window bias) remains
open, affecting `H_fwd`/G1 but not the admission model.
**Slice 1 firmware is in tree (user authorized this slice):**
`recordPlaybackRebuild` keeps the maximum duration and that sample's
track/slot/path, plus a rebuild count. Capture line is
`DIAG,playback_build,<path>,<us>,<winStart>,<winLen>,<rev>,<track>,<slot>,<count>`,
where `<path>` is `snapshot`, `compatibility`, or `note_edit`. The path is
captured at `Track::ensurePlaybackMergedMidiEventsBuilt`; the one-shot remains
deferred and `<count>` remains an aggregate of rebuild calls before emission.
Device recapture is required to attribute the six `cer_edit` signatures to
snapshot versus compatibility playback.
Ring B/RC2 is **closed**. RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). Residual 4b.1b is
owned by the owner-boundary plan and parked.
**Slice 0 RAM1 blocker found and fixed (2026-08-26).**
`teensy41-capture-serial-investigation-diag` built at RAM1 **locals 512** —
a FAIL against the 4000 floor despite PlatformIO `SUCCESS`, so the board was not
flashed. ELF section diff attributed the cost to `.data` +1024 / `.bss` +3072
with ITCM unchanged (424796+1188 = 424860+1124 = 425984), and the confirmed
cause was `SESSION_CAPTURE_INVESTIGATION_DIAG` format strings **not**
flash-placed: `LoopMaterialization.cpp` and `EditApply.cpp` already had
`LOOP_DIAG_FMT`/`EDIT_DIAG_FMT` = `PSTR(...)`, but `LoopVisualCache.cpp`
(4 literals) and `LoopPendingNoteChange.cpp` (6 literals) had none, so their
`#CAP` literals landed in DTCM — contrary to
`Instrumentation-Session-Capture-Gating`. Fix adds
`LOOP_VCACHE_DIAG_FMT` / `LOOP_NOTECHANGE_DIAG_FMT` on the same pattern.
Result: investigation-diag RAM1 variables 97792 → **93696**, locals 512 →
**4608** — identical to `teensy41-capture-serial`, so the investigation build now
costs **no** DTCM over the default capture build. Flash absorbed the strings
(`data` 78692 → 79796). Both envs PASS; `pio test -e native` **1475/1475**.
Checkpoint: ownership no, transitions no, unbounded interval work no.
**Slice 0 DONE — attribution closed.**
[`153941`](../../captures/session_20260826_153941.log) on the investigation-diag
env reproduced the stall larger (`tracks=393818`, `midi_input=393845`, same
chain) and attributed it end to end. Of one `updateAllTracks` at **393818 us**:
`cer_range` **367412** (93%) → `edit` (`applyNoteEditPassSequence`) **343625**
(94% of that) → `ulen` **341642** → **`recon` 339073**. The windowed CER
traversal is only `append=23511`; `collect`+`sort` are 276 us. So **92% of the
rebuild is note reconstruction inside the edit projection.**
`cer_rows` is constant in all 50 non-empty samples: `active=217, comp=217`
(every Active row an overdub companion), `del=185`, `ulen=32` — 217 rows
replayed to emit **164** events in a 2-bar window. F11 proven: edit-projection
cost scales with EditPass count, not window size. `recon` is 98–99% of `ulen`
in every sample; `nlen` drives it (18→339 ms, 16→287 ms, 12→170 ms) but the
same `nlen=18` also measured 68989 us, so per-row cost is content-dependent
(~19 ms/row worst).
**Compatibility path, not snapshot path:** `cer_range` starts at 52.014 s,
before this session's long record at 61.168 s, and every publish belongs to the
one transitioning track — so the loop carrying 18 chunk lists and 217 companion
rows is a **loaded slot with no published root**, reaching
`copyEffectiveCommittedEventsInRange` via `gatherCommittedEventsInWindow`.
**New root cause RC4 (separate, not bundled):** the length-update path re-runs
note reconstruction per row on the compatibility gather. RC1 is admission
multiplicity; RC4 is unit cost. Against the envelope (`H_fwd_wall`=1000 ms at
240 BPM) a 367 ms rebuild clears `L < H_fwd` but **violates the `L <= H_fwd/2`
target from 16 eligible destinations upward** — admission scheduling alone only
just fits, with no margin. Candidate resolution is already in the architecture
direction (DEC-044/045): a published snapshot carries edits applied, so the
compatibility gather and this whole cost do not exist for a slot with a
published root. Whether loaded slots publish on restore is the open design
question.
[`160244`](../../captures/session_20260826_160244.log) **proves RC4 unit cost is
environment-dependent, not input-dependent.** Six recurring `cer_edit` input
signatures repeat in three rounds; five with byte-identical
`ndel/nlen/lgth/shrt/ev` cost **7.3x-9.7x** more in round 2 (long overdub,
`snapshot_idle` 48.4% of window) than in rounds 1 and 3 (28.9% and 0%).
`cer_range` splits into an uncontended band **29.5-98.9 ms** and a contended band
**306-363 ms**, so **367 ms is pathological, not representative** — `G1` must
consume the uncontended figure. Mechanism is **not** proven: wall-clock inflation
from interleaving (48.4% load alone gives ~2x, not 9x) versus
`InternalHeapFirstAllocator` spill to PSRAM (`min_ever` fell 340->120 KB, which is
pressure but not exhaustion). Discriminate before optimizing.
**Weighting between RC1 and RC4 revised:** `tracks` max 391.1 ms against a single
362.9 ms rebuild leaves no room for multiplicity as the main term, and observed
multiplicity is **pairs** (median `cer_range` spacing 1998 ms; only 8 gaps under
50 ms, all 11.9-16.8 ms). Ring A's benefit here is bounded by ~2x while RC4 is
~9x. Ring A stays architecturally correct but must not be presented as the fix
for the 391 ms.
**Slice 1 destination attribution closed by [`161834`](../../captures/session_20260826_161834.log)
and [`162522`](../../captures/session_20260826_162522.log).** The 94
`playback_build` maxima in `161834` identify four destinations: `T0/S6` (57
lines, max 166.325 ms), `T4/S0` (34, max 25.439 ms), `T6/S0` (2, max 10.965
ms), and `T2/S2` (1, max 9.406 ms). `162522` directly pairs all 15 repeated
`cer_edit` compatibility gathers with `T0/S6`; its maximum is 360.754 ms.
`T0/S6` is the dominant long-loop stream.
The telemetry `<count>` is rebuild calls before the next one-shot emit, not
distinct destinations per bar edge; no eligible-set count is inferred from it.
The small `lists=1/2, rows=0` gathers are separate edit-free destinations.
**RC4 mechanism remains open after `162522`:** `snapshot_idle` lines occur
after the inflated gather, so snapshot construction is not concurrent inside
the `g_reconstructUs` span. `165024` now rules out the measured
`InternalHeapFirstAllocator` spill route and nested clock dispatch: all 16
`cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`. Higher-
priority interrupt elapsed time and `ExternalMemoryFirstAllocator`
service/cache effects remain unmeasured. `cer_recon` now splits
internal-heap `filt` from PSRAM-first `span`/`proj` and records
IntervalTimer ISR `itickus`.
[`171159`](../../captures/session_20260826_171159.log) closes that split:
`filt` is unchanged on the 7.9x `nlen=18` pair (`36` / `36`); `span` and
`proj` take the inflation; `itickus=0` on all 11 rows. Question 3 is closed
from code: `adoptPersistedSnapshot` / `restorePassesSnapshot` do not publish.
The new source-tagged telemetry is device-validated by
[`202654`](../../captures/session_20260826_202654.log): 82 emitted
`playback_build` samples classify as 74 `compatibility` and 8 `snapshot`
(no `note_edit`). The long-loop `T0/S6` stream remains compatibility, reaching
325.541 ms, while snapshot-backed `T4/S0` samples remain below 1 ms
(maximum 358 us). This confirms the loaded-slot compatibility path and the
published-root snapshot path coexist in one later capture.
RC4 question 5 is closed as `T0/S6` compatibility-only: the eight `lgth`
rows belong to that loaded slot's companion replay, not to a published
snapshot. Ignore further `T0/S6` lengthen-branch forensics.
**G1 satisfied:** `representativeRebuildWall` = 358 µs (`T4/S0` snapshot
maximum in `202654`); `prefetchTicks` = 272; `H_fwd` = 768. The same
capture's non-`T0/S6` compatibility ceiling is 11.859 ms and still fits
that lead. Ring A firmware remains unauthorized.
[`205447`](../../captures/session_20260826_205447.log) confirms leaving
`T0/S6` for `T0/S5`: S5 rebuild is 334 µs and S6 drops out of
`playback_build`. The same timestamp is a 377 ms `idle_maint` / `midi_gap`
stall after `VCACHE,stale_all`, not an S5 rebuild cost. RC4 conclusions
are in
[`playback_window_edit_projection_unit_cost_investigation.md`](../Plans/playback_window_edit_projection_unit_cost_investigation.md).
**RC4 sequencing decided (user, 2026-08-26):** investigation is **authorized now**
in [`playback_window_edit_projection_unit_cost_investigation.md`](../Plans/playback_window_edit_projection_unit_cost_investigation.md);
the **firmware fix is a separate change** and must not enter the Ring A slice.
Owner is proven: `applyChangeLengthById` reconstructs **only on the lengthen
branch** via `reconstructNotesForPitch`, which per row reallocates, rescans every
event, and re-runs the full canonical-span pipeline — `lgth=8` rows carry all
339 ms (~42 ms each) while `shrt=10` shorten rows contribute nothing.
Order: Slice 1 closure → RC4 investigation → measure RC4 resolution → **size G1
from the post-resolution cost** → Ring B/RC2 independently → Ring A last. Do
**not** lock the prefetch lead at 367 ms: that is ~35 clock dispatch periods at
240 BPM, and two samples with identical row counts differ 5x (339 vs 69 ms), so
representativeness is unestablished.
RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). Do **not** check off
4b.3 until device verify.
Do not close **4b.1**. Do **not** split `atomicRenameTempFile`. 4b.0 wrap/hold
remains device PASS [`092755`](../../captures/session_20260826_092755.log).

Phase 3 task **4.4 display** remains in tree (not closed). Paint and visual-cache fill read
published `LoopSnapshot` events through
`tryGatherPublishedSnapshotEventsInWindow` (plus live capture when needed),
then `reconstructDisplayNotes`. A nonempty dirty `visualCache` prefix does
not authorize the paint window — `visualCacheCoversFollowWindow` must pass
or the window gathers from the snapshot after capture ends
([`235954`](../../captures/session_20260825_235954.log)). Live overdub
keeps the PLAYING committed handoff only while the paint window stays
inside the last gather ([`001048`](../../captures/session_20260826_001048.log)
`frame=0`; [`001907`](../../captures/session_20260826_001907.log) held the
first 16-bar slice on a 21-bar loop). Follow exit or wrap invalidate
snapshot-gathers once. A remainder of at most two gather margins is
included in the first gather so a 20-bar loop does not omit the last
2 bars and then gather on the MIDI path
([`002634`](../../captures/session_20260826_002634.log): window at 3072
at 117.1 s, last-bar notes at 119.2 s, `DisplayResolve` 81–196 ms).
After overdub stop, `ODUB,stop,display` holds the composed frame and does
not snapshot-gather. Device PASS [`004934`](../../captures/session_20260826_004934.log):
display **8.8 ms** / **12.2 ms** (was 97–154 ms on
[`003459`](../../captures/session_20260826_003459.log)). PLAYING after first
publish is **7.4–8.4 ms** `midi_gap`. Ring B/RC2 host implementation is now
in tree; RC2 device gate 4b.2b is closed by
[`183452`](../../captures/session_20260826_183452.log). 4b.3 post-overdub
load-frame drain polls remain separately device-verification work.
Compatibility gather remains
when no published root exists. Do not CER-gather on the overdub paint
path. Task **4.3** stays skipped. NOTE_EDIT, overdub analysis,
validation, 4.5, 4.6, and Phase 5 are **not selected**.

After the first published `LoopSnapshot` exists, PLAYING and OVERDUBBING idle
do not `step` successor construction. PLAYING visual stays off unless the
cache is empty. First-record starts during RECORDING after the playhead has
left bar 1 and continues on PLAYING (`DEC-045` 4b.2). Device PASS
[`233308`](../../captures/session_20260825_233308.log): four overdubs after
record; first publish at 97.50 s; **no** `snapshot_idle` from then until
STOPPED; PLAY4 BPM 119.966–120.049.

Phase 3 tasks 4.1–4.2 remain complete. Playback preparation reads the
published `LoopSnapshot` through `ensurePlaybackMergedMidiEventsBuilt`,
records snapshot identity for freshness, and falls back to compatibility
gather when no snapshot is selected. Cursor, order, ledger, and horizon stay
on `Track` / `LoopPlaybackRuntime`. `playbackEventPhase` stays the hot-path
scalar.

**Authorized exception:** queued-destination visual-cache invalidation — port of the
repair authorized on `feature/overdub-stop-lag-stage-c`. Owner:
`DisplayManager::invalidateForSlotChange`. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).
Firmware in tree; host gates next. Does not change snapshot ownership or LCR.

**Branch:** `feature/immutable-loop-snapshot-passes`, forked from
`546169f66658b87c54e6166b79eba74628aa3d1e`.

**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Plan:** [`loop_pass_snapshot_effective_state_refinement.md`](../Plans/loop_pass_snapshot_effective_state_refinement.md)
**Active wrap slice:** [`overdub_wrap_source_snapshot_migration_refinement.md`](../Plans/overdub_wrap_source_snapshot_migration_refinement.md) — OpenSpec tasks `3.5`/`3.6` are closed; 4.4a source resolution and 4.4b display snapshot fallback are now closed.
**Applied Phase 0 spec patch:** [`loop_snapshot_pitch_bar_span_index_refinement.md`](../Plans/loop_snapshot_pitch_bar_span_index_refinement.md) — logical structure selected. Phase 1 native/offline model is complete. Phase 2 publication adapters, Phase 3 playback snapshot reads (4.1–4.2), stateless snapshot queries (4.3), 4.4a source resolution, and 4.4b display fallback are complete. Remaining NOTE_EDIT/analysis/validation and display identity migration remains next.

Phase 0 documentation now includes the project-scoped
[`MIDI_PROTOCOL_GUIDE.md`](../Guides/MIDI_PROTOCOL_GUIDE.md), based on
M2-104-UM v1.1.2, with a read-only audit of the current MIDI 1.0 logic. The
2026-08-25 reference-root refinement separates `LoopPass` from `LoopSnapshot`,
requires a persistent reference root, defines reachability and provisional
versus global snapshots, and loads only the latest saved snapshot after reboot.
The later 2026-08-25 pitch/bar amendment selects the logical indexes. The Phase
1 native measurement selects the sparse fixed-capacity paged directory
experiment; page capacity and memory characteristics remain measured
parameters. Formal reassessment and approval are recorded in
[`ARCHITECTURE-REASSESSMENT.md`](../../openspec/changes/immutable-loop-snapshot-passes/ARCHITECTURE-REASSESSMENT.md).
Later-phase gates are recorded in
[`PREFLIGHT.md`](../../openspec/changes/immutable-loop-snapshot-passes/PREFLIGHT.md)
and
[`DECISION_REVIEW.md`](../../openspec/changes/immutable-loop-snapshot-passes/DECISION_REVIEW.md).
The Phase 1 model is native/offline only; no production runtime behavior or
consumer selection changed.

The maintained `LoopContentResolution` lifecycle and its playback-readiness
continuation are not pursued on this branch. The existing `LoopContentResolution`
prototype remains historical compatibility context only; the proposed snapshot
model must not create a replacement maintained effective-state owner.

Source-level replace-on-retrigger pairing is deferred. This branch retains the
current `Track::recordMidiEvents()` / `pendingNotes` behavior and existing LIFO
pairing. The deferred future bugfix may add an internal synthetic NoteOff before
replacing an open same-channel/same-pitch capture entry; it is outside the
immutable snapshot Phase 0 scope.

## Historical base work

The sections below record work present at the fork point. They are not the active
execution queue for this branch and require explicit reauthorization before use.

### Slice 0 — docs + scaffolding delete

**Owner:** docs + `Loop` companion-projection members (`companionProjectionEvents_`, `rebuildCompanionProjectionForCurrentRevision`, `tryCopyCompanionProjectedWindow`, `clearCompanionProjectionCache`)

**Invariant:** Dead paired-projection scaffolding is gone. Interval gather still uses windowed `applyNoteEditPassSequence` (predictable-but-slow baseline). One derived-authority direction is documented; no second consumer cache.

**Status:** Shipped — native **1429/1429**. Dead scaffolding removed; gather still windowed `applyNoteEditPassSequence`.

### Slice 1A — cooperative prepare (next)

**Owner:** existing LCR device-gate prepare path only.

**Invariant:** Same prepare semantics; persistent progress; bounded work units; yield/resume without repeating completed work. Instrument `prepare,slice,*`. If F1–F7 cannot be subdivided without algorithm decomposition, **stop and report** (failure is valid).

**Do not in 1A:** optimize prepare, redesign algorithm, add cache, wire playback consume, enforce 60% × T_clock timer.

**Timing gate:** At 170 BPM, one MIDI clock interval is **14.71 ms**. The
**8.82 ms** value (60% of that interval) is only an experimental operating
target; no guaranteed maintenance reservation has been established. Measure the
proposed eight-candidate unit separately, including its indivisible path-copy,
index, replacement/tombstone, and allocation work. Treat p95 above 8.82 ms as
outside the initial target and any unit above 25 ms as an investigation failure;
these are not hard MIDI guarantees.

### Paired companion consume reverted (closed context)

**Evidence:** [`163135`](../../captures/session_20260822_163135.log) — `paired_proj` **0** / `win_apply` **33**; multi-second `idle_gather`; USB reconnect. Decision: `PairedCompanionState` reverted; tip cut at Stage C. Overlap never used paired projection (`ActiveNoteLedger` / `overdubSourceViewNotes_`).

### STOPPED undo / track-switch first-paint

**Evidence:** [`183229`](../../captures/session_20260821_183229.log) device PASS. Prior fail [`161804`](../../captures/session_20260821_161804.log) / [`165107`](../../captures/session_20260821_165107.log) undo `idle_maint` 850 ms. Plan: [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §34 / §36 Stage C.

**Owner:** `DisplayManager::invalidateForSlotChange` / `Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice`

**Invariant:** STOPPED undo and track switch keep `visualCache.notes` and fill through budgeted idle slices with wrap-append deferred. No wipe. No unbounded gather-plus-append in one STOPPED turn. No companion full replay on STOPPED.

**Status:** Device PASS [`183229`](../../captures/session_20260821_183229.log) — undo `stale_all` notes=2988, first `DISP` visual=2988, first post-undo `idle_maint` 224 ms; 68-bar switch notes=3040 / visual=3040. Native **1429/1429**. Closed.

### Overdub companion admission and source-span prewarm

**Evidence:** [`162928`](../../captures/session_20260821_162928.log) empty open + 136.77 s freeze. Device PASS [`165107`](../../captures/session_20260821_165107.log): 5/5 opens `notes` 1319–1394; 34/34 consume `idsel>0`; max `tracks` **190 ms**. Residual `norow` 1–4 with working select is parked.

**Owner:** `Track::processDeferredIdleMaintenance` → `Loop::prewarmOverdubSourceSpanCache` (Stage A empty-projection gather path removed with dead companion-projection scaffolding)

**Invariant:** Clock-path gather must not run full companion replay. Empty companion projection is not a successful gather. First overdub open hydrates a non-empty source view from a cache filled after boot first-paint, not from a same-turn `slice_clean` prewarm.

**Status:** Stage A+B device PASS [`165107`](../../captures/session_20260821_165107.log). §36 closed.

### Boot title `boot_commit` display stall

**Evidence:** [`161804`](../../captures/session_20260821_161804.log) device PASS. Prior gather hangs: [`161148`](../../captures/session_20260821_161148.log) (`boot_commit=70438665`, first `DISP` visual=0), [`154353`](../../captures/session_20260821_154353.log) (`boot_commit=75457996`, visual=0).

**Owner:** `runDeferredIdleMaintenance` / `runDeferredLoadAndDisplayFrame` / `TrackManager::onBootSlotLoadComplete`

**Invariant:** First piano roll filters the focus visual cache. `onBootSlotLoadComplete` must not clear `visualCache.notes`.

**Stage change shipped:** Boot idle stays on the focus track; `boot_commit` waits until that cache is clean; boot complete no longer calls `invalidateForSlotChange`. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).

**Status:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — focus `slice_clean` 2638 notes at 10.9 s; USB at 11.484 s; first `DISP` visual=**2638** / 1224 window notes; `boot_commit=288557` (289 ms). `DFRAME` continues at ~15 ms.

### Boot title startup stall hardening

**Evidence:** [`161410`](../../captures/session_20260820_161410.log)

**Owner:** `Looper::setup`

**Invariant:** Startup does not stall at the title screen when SD bootstrap is transiently unavailable; boot continues without requiring repeated manual reboots.

**Stage change shipped:** `Looper::setup` now initializes SD with bounded retries (`3` attempts, `50 ms` delay). If SD remains unavailable, boot takes an explicit empty-runtime fallback path and skips `StorageManager::loadState` instead of blocking startup.

**Status:** Native verification pending.

### Overdub lag plan — baseline reset and reprioritized execution

**Evidence:** [`210319`](../../captures/session_20260820_210319.log) (new baseline), [`231845`](../../captures/session_20260820_231845.log), [`232951`](../../captures/session_20260820_232951.log), plus prior attribution captures [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`174941`](../../captures/session_20260820_174941.log)

**Owner set (current priority order):**
1. `Track::stopOverdubbing` stop-path handoff (`ODUB,stop,display` envelope)
2. `Loop::sealPendingNoteChangesToEditPasses` + `TrackUndo::pushOverdubSessionOnStop` (dominant `seal_fx_*` contributors)
3. Post-stop scheduler debt window (`processDeferredIdleMaintenance`, `runDeferredLoadAndDisplayFrame`, `StorageManager::processDeferredSaveState`)

**Invariant:** Prioritize only runtime-blocking windows: stop display handoff and first post-stop scheduler stall. Stop commit semantics remain unchanged; stop return must not wait on newly discovered deferred debt.

**Runtime-stall baseline (from [`210319`](../../captures/session_20260820_210319.log)):**
- **Published stop cycles with complete first post-stop window:** 6
- **`ODUB,stop,display` elapsed:** **56.451-71.803 ms** (median **65.288 ms**)
- **First post-stop `DIAG,midi_gap`:** **78.634-145.373 ms** (median **107.569 ms**)
- First post-stop decomposition:
  - `idle_maint`: **78.619-145.357 ms** (median **101.576 ms**) — dominant
  - `load_frame`: **27.553-42.889 ms** (median **33.548 ms**)
  - `persist_save`: **13.295-122.704 ms** (median **47.168 ms**)
- Published stop `seal` duration (same 6 complete cycles): **45.461-61.129 ms** (median **56.006 ms**)
- `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **41.310 ms** (dominant synchronous owner)
  - `seal_fx_push_overdub_undo`: **13.243 ms** (secondary synchronous spike)
  - `seal_fx_mark_display_ranges`: **2.828 ms**; remaining `seal_fx_*` are minor
- Additional published cycle without a complete first post-stop window in this capture: `display=39.965 ms`, `seal=29.498 ms` (tracked separately from the 6-cycle target sample).
- Two `seal=skipped` cycles still show large first gaps (**672.708-711.300 ms**) with small display (~11 ms), proving deferred scheduler debt can dominate independently of stop display.

**Reprioritized execution (based on measured stalls):**
1. **P1 — Stop display envelope trim (synchronous):** keep attribution on `seal_fx_seal_pending_changes` and `seal_fx_push_overdub_undo`; reduce and bound the `ODUB,stop,display` envelope before any broad scheduler tuning.
2. **P2 — Post-stop debt causality proof (deferred):** classify first-gap contributors as **pre-existing debt**, **stop-created debt**, **stop-amplified debt**, or **newly scheduled after stop**.
3. **P3 — Scheduler policy only after causality:** if P2 finds architectural stop-triggered invalidation/rebuild, fix that owner cause first; do not mask it with slice caps.
4. **P4 — Background arbitration:** prevent large `idle_maint` + `persist_save` stacking from consuming the same post-stop runtime window once causality is closed.

**Runtime scheduling roadmap linkage:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md)
- P1 remains pre-contract synchronous owner attribution on `Track::stopOverdubbing` / seal owners; it does not start interval reservation.
- P2 contributes Stage R owner-boundary evidence for post-stop debt classification (`pre-existing`, `stop-created`, `stop-amplified`, `newly scheduled after stop`).
- P3 enforces Owner-Boundary Gate ordering: if stop-triggered unnecessary invalidation/rebuild is found, fix that owner cause before slice caps or arbitration tuning.
- P4 is scheduling policy input only after owner causality is closed; interval reservation remains unauthorized until roadmap Stage C/A gates are explicitly satisfied.

**Runtime targets (for this overdub-lag slice):**
1. **Stop handoff target:** `ODUB,stop,display` is measured as a bounded owner envelope with dominant contributors attributed (`seal_fx_seal_pending_changes`, `seal_fx_push_overdub_undo`) and trimmed without changing stop commit semantics.
2. **First post-stop opportunity target:** first post-stop `DIAG,midi_gap` is reduced by owner-cause closure, not by masking unknown debt; each major contribution is classified as `pre-existing debt`, `stop-created debt`, `stop-amplified debt`, or `newly scheduled after stop`.
3. **Stop/deferred boundary target:** stop return does not wait on completion of deferred maintenance debt discovered in P2.
4. **Arbitration target:** long `idle_maint`, `load_frame`, and `persist_save` work does not stack into the same first post-stop runtime window after P4 policy work.
5. **Scheduling-contract target carryover:** no multi-second PLAYING/transport-active `DIAG,midi_gap` windows; interval reservation remains out of scope for this slice.
6. **Musical correctness gate carryover (when lateness hooks are present in the same evidence run):** `late_event_count == 0` and `max_lateness_us <= 0` for note-on, note-off, and clock.

**Target-aligned baseline checkpoints (same [`210319`](../../captures/session_20260820_210319.log) evidence):**
1. **Stop handoff target baseline:** complete published stop-display envelope is **56.451-71.803 ms** (median **65.288 ms**); dominant synchronous contributors remain `seal_fx_seal_pending_changes` (max **41.310 ms**) and `seal_fx_push_overdub_undo` (max **13.243 ms**).
2. **First post-stop opportunity target baseline:** first post-stop `DIAG,midi_gap` on complete published cycles is **78.634-145.373 ms** (median **107.569 ms**) with decomposition windows above.
3. **Stop/deferred boundary target baseline:** two `seal=skipped` cycles still show small stop-display windows (~**11 ms**) with large first post-stop gaps (**672.708-711.300 ms**), confirming deferred debt can dominate without large synchronous display cost.
4. **Arbitration target baseline:** in the 6 complete published windows, `load_frame` remains **27.553-42.889 ms** every cycle and `persist_save` reaches **47+ ms** in 4/6 windows (max **122.704 ms**), so stacked post-stop work remains an active scheduling risk.
5. **Scheduling-contract carryover baseline:** run-wide `DIAG,midi_gap` in this capture reaches **2.855 s** (3 windows over 1 s), so the no-multi-second-gap carryover target is not yet met globally.
6. **Musical correctness gate carryover baseline:** lateness hooks are present; this run reports non-zero late note windows (`late_on`: 5 windows, 23 events total, max lateness **42.458 ms**; `late_off`: 4 windows, 13 events total, max lateness **29.551 ms**; `late_clk`: 0 events).

**P1 re-measure after sorted transform lookup ([`213848`](../../captures/session_20260820_213848.log)):**
- Sample shape: **9** stop cycles, **7** published cycles with complete first post-stop windows, **0** skipped-complete cycles.
- **`ODUB,stop,display` (published + complete):** **26.751-134.121 ms** (median **58.246 ms**) — median improved vs baseline, but one long-tail outlier remains.
- **Published `seal` (same 7 complete cycles):** **18.250-125.144 ms** (median **48.153 ms**).
- **First post-stop `DIAG,midi_gap` (published + complete):** **43.875-189.583 ms** (median **104.331 ms**).
- First post-stop decomposition (published + complete):
  - `idle_maint`: **32.233-189.565 ms** (median **104.313 ms**)
  - `load_frame`: **27.147-36.773 ms** (median **31.289 ms**)
  - `persist_save`: **13.960-55.248 ms** (median **45.047 ms**)
- `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **48.354 ms** (higher max than baseline)
  - `seal_fx_push_overdub_undo`: **6.650 ms** (lower max than baseline)
  - `seal_fx_mark_display_ranges`: **3.818 ms**
- Dominant outlier cycle: `ODUB,stop,display=134.121 ms`, `seal=125.144 ms`, `seal_fx_seal_pending_changes=48.354 ms`, `seal_fx_mark_display_ranges=3.818 ms` (same stop cycle).
- Carryover checks in the same run: run-wide `DIAG,midi_gap` max **2.890 s** (5 windows over 1 s); lateness remains non-zero (`late_on` max **64.220 ms**, 46 events; `late_off` max **87.453 ms**, 30 events; `late_clk` zero).

**P1 re-measure after publish-owner telemetry ([`215223`](../../captures/session_20260820_215223.log)):**
- Sample shape: **8** stop cycles, **7** published cycles with complete first post-stop windows, **0** skipped-complete cycles.
- **`ODUB,stop,display` (published + complete):** **32.702-124.432 ms** (median **50.174 ms**).
- **Published `seal` (same 7 complete cycles):** **23.529-115.351 ms** (median **42.042 ms**).
- **First post-stop `DIAG,midi_gap` (published + complete):** **50.388-160.096 ms** (median **102.466 ms**).
- First post-stop decomposition (published + complete):
  - `idle_maint`: **38.602-160.081 ms** (median **102.448 ms**)
  - `load_frame`: **27.039-36.331 ms** (median **30.616 ms**)
  - `persist_save`: **16.108-85.121 ms** (median **46.960 ms**)
- New publish-owner telemetry maxima in the same run:
  - `seal_fx_publish_pending_pass`: **62.448 ms**
  - `seal_publish_span_cache_restamp`: **62.323 ms**
  - `seal_publish_promote_pass`: **9 us**
  - `seal_publish_finalize_state`: **10 us**
  - `seal_publish_clear_capture`: **13 us**
- Existing `seal_fx_*` maxima in the same run:
  - `seal_fx_seal_pending_changes`: **44.809 ms**
  - `seal_fx_push_overdub_undo`: **69 us**
  - `seal_fx_mark_display_ranges`: **3.991 ms**
- Dominant outlier cycle attribution: `ODUB,stop,display=124.432 ms` with `seal=115.351 ms`, split primarily across `seal_publish_span_cache_restamp=62.323 ms` and `seal_fx_seal_pending_changes=44.809 ms`; publish owner is now directly measured.
- Carryover checks in the same run: run-wide `DIAG,midi_gap` max **1.907 s** (1 window over 1 s, 1 over 500 ms); lateness remains non-zero (`late_on` max **53.490 ms**, 36 events; `late_off` max **48.415 ms**, 27 events; `late_clk` zero).

**P2 re-measure after global post-overdub drain-window clamp ([`230507`](../../captures/session_20260820_230507.log)):**
- Sample shape: **10** stop cycles, **10** published cycles, **10** handoff rows, **6** unique first post-stop windows.
- **`ODUB,stop,display` (published):** **34.471-58.055 ms** (median **49.813 ms**).
- **Published `seal`:** **24.092-47.049 ms** (median **39.534 ms**).
- **First post-stop `DIAG,midi_gap` (unique windows):** **87.209-168.113 ms** (median **108.276 ms**).
- First post-stop decomposition (same unique windows):
  - `idle_maint`: **87.178-168.095 ms** (median **108.244 ms**)
  - `load_frame`: **29.352-34.071 ms** (median **32.430 ms**)
  - `persist_save`: **0.001 ms** (flat)
- Debt classification in the same run:
  - `ODUB,stop,debt_handoff`: **10/10 stop_created_debt**
  - `DIAG,odub_debt visual`: **10/10 stop_created_debt**
  - `DIAG,odub_debt save`: **9/10 pre_existing_debt**, **1/10 stop_created_debt**
  - Clamp contract check: `idle_slice=1` and `idle_bars=1` in **10/10** rows
- Delta vs pre-clamp reference [`225521`](../../captures/session_20260820_225521.log):
  - First post-stop `midi_gap` median **160.499 -> 108.276 ms**; max **506.253 -> 168.113 ms**
  - First post-stop `idle_maint` median **147.522 -> 108.244 ms**; max **498.002 -> 168.095 ms**
- Remaining synchronous stop owners in this run:
  - `seal_publish_span_cache_restamp`: **2.357-20.746 ms** (median **15.792 ms**)
  - `seal_fx_seal_pending_changes`: **2.709-23.835 ms** (median **17.319 ms**)

**P3/P4 re-measure after stop-owner trim ([`231845`](../../captures/session_20260820_231845.log), [`232951`](../../captures/session_20260820_232951.log), [`233633`](../../captures/session_20260820_233633.log), [`235214`](../../captures/session_20260820_235214.log)):**
- Sample shape in both captures: **12** stops = **9 published** + **3 skipped**.
- Published synchronous stop owners stay bounded:
  - [`231845`](../../captures/session_20260820_231845.log): `ODUB,stop,display` median **21.993 ms** (max **37.466 ms**), `seal_publish_span_cache_restamp` median **2.988 ms** (max **5.391 ms**), `seal_fx_seal_pending_changes` median **1.226 ms** (max **1.524 ms**).
  - [`232951`](../../captures/session_20260820_232951.log): `ODUB,stop,display` median **24.824 ms** (max **36.010 ms**), `seal_publish_span_cache_restamp` median **3.898 ms** (max **5.343 ms**), `seal_fx_seal_pending_changes` median **1.090 ms** (max **1.601 ms**).
- Remaining long-tail is deferred-window, not synchronous stop owner:
  - [`232951`](../../captures/session_20260820_232951.log) skipped stops: `ODUB,stop,display` **9.829-9.991 ms**, but first post-stop `DIAG,midi_gap` **595.365-603.237 ms**.
  - First post-stop unique windows in [`232951`](../../captures/session_20260820_232951.log): `midi_gap` median **134.773 ms**, max **603.237 ms**; `idle_maint` median **134.753 ms**, max **603.218 ms**; `persist_save` median **17.333 ms**, max **55.075 ms**.
- Debt causality remains classified and clamped (`ODUB,stop,debt_handoff`: **10 stop_created_debt**, **2 pre_existing_debt**; `idle_slice=1`, `idle_bars=1` in **12/12** rows).
- [`233633`](../../captures/session_20260820_233633.log) confirms the remaining long-tail owner is `idle_append` during post-stop PLAYING debt windows after `VCACHE,stale_all` (undo path), not synchronous stop publish owners:
  - `DIAG,midi_gap=850.101 ms` aligns with `DIAG,loop_rem,idle_append=797.766 ms` + `DIAG,loop_rem,idle_maint=844.487 ms`.
  - `DIAG,midi_gap=817.292 ms` aligns with `DIAG,loop_rem,idle_append=769.842 ms` + `DIAG,loop_rem,idle_maint=817.272 ms`.
- [`235214`](../../captures/session_20260820_235214.log) validates the Stage D arbitration + append guard on overdub windows:
  - Sample shape: **6** stops, **6 published**, **0 skipped**, `RING,overflow=0`.
  - First post-stop unique windows: `DIAG,midi_gap` **32.692-152.781 ms** (median **101.382 ms**), `idle_maint` **32.691-152.759 ms** (median **101.358 ms**), `persist_save` **0.001-0.188 ms** (median **0.001 ms**).
  - Compared with [`233633`](../../captures/session_20260820_233633.log): skipped windows removed (**2 -> 0**) and first post-stop gap max reduced (**850.101 -> 152.781 ms**).
- Remaining run-wide multi-second outliers in [`235214`](../../captures/session_20260820_235214.log) are not post-stop overdub windows: `DIAG,midi_gap` **1908-1910 ms** aligns with `loop_rem,boot_commit` **1632 ms** + `loop_rem,load_frame` **1642 ms** while state is `STOPPED`; keep this as separate non-overdub scheduling follow-up.
- **Stage D implementation scope (shipped):** defer `StorageManager::processDeferredSaveState` admission while post-overdub drain is active and deferred save work is pending, and skip wrap-pair append in that same stressed drain context, so `idle_maint` + `persist_save` + `idle_append` do not stack in one post-stop drain turn.

**Stage changes shipped:**  
- `applyPendingHideAndShortenToNotes` now applies `Shorten`/`Hide` transforms by rebuilding the note vector once, avoiding repeated in-place erase scans for each pending change.  
- `saveNoteEditPass` gained `deferDerivedInvalidate` for batch callsites.  
- `sealPendingNoteChangesToEditPasses` now defers derived invalidation during companion row inserts and performs a single `playbackRevision` + `notifyCommittedContentChanged` publish after the batch.
- Companion sealing now performs one heap-reserve admission check for the entire companion batch and skips repeated per-row heap checks once the batch is admitted.
- Companion sealing now reserves `passes.editPasses` capacity for the full companion batch before row insertion, removing vector growth churn from the stop path.
- Added one batch timing line per companion seal (`DIAG,seal_companion_batch`) so each stop cycle records rows/sealed/duration directly.
- Added Tier-A stop sub-stage timing lines (`DIAG,odub_stop`) for `companion_seal`, `undo_push`, `mark_range`, `publish_prepared`, and `persist_request` inside `finalizeCommitSideEffects`, so long-tail stop cycles can be attributed to one owner step even under ring pressure.
- Companion per-row telemetry is now capped (first 8 `DIAG,seal_companion` rows) to avoid line-format/append churn during heavy stop cycles while preserving batch-level timing visibility (`DIAG,seal_companion_batch`).
- `markAffectedDisplayCacheRanges` now marks companion row spans first, then does one note-cache pass against the companion target-note set, instead of scanning `visualCache.notes` once per companion row.
- `applyPendingNoteChangesToOverdubSourceView` Add-note merge batching is restored (user-directed) so pending Add notes are merged in one pass before `Shorten`/`Hide` projection updates.
- `applyPendingHideAndShortenToNotes` now keeps Shorten/Hide transforms sorted by `noteId` and uses bounded lookup, removing repeated linear transform scans from the overdub-stop seal path while preserving Shorten/Hide outcomes.
- Added `SESSION_CAPTURE`-gated stop publish telemetry for the previously uninstrumented overdub-stop publish owner (`Loop::commitPendingCapturePass`) with cold/Flash placement (`LOOP_COLD_MEM`): `seal_fx_publish_pending_pass`, `seal_publish_promote_pass`, `seal_publish_span_cache_restamp`, `seal_publish_finalize_state`, `seal_publish_clear_capture`.
- `commitPendingCapturePass` span-cache restamp now hard-bounds stop-path work: when the committed pass exceeds the stop restamp event budget (`2048` events), restamp is deferred (`seal_publish_span_cache_restamp=deferred_event_budget`) and revision restamp is skipped so heavy rebuild work moves out of the synchronous stop envelope.
- `commitPendingCapturePass` now skips full committed-pass note reconstruction when no `NoteOn` id is missing from the span cache, and reconstructs only when missing ids remain after fast eligibility filtering.
- `mergeDisplayNotesIntoOverdubSourceView` and overdub-source span-cache add paths now use sorted `NoteId` indices for bounded membership checks instead of repeated per-candidate linear scans.
- Added one-shot post-stop causality probe lines: `ODUB,stop,debt_handoff` plus `DIAG,odub_debt` on the first PLAYING idle-maintenance turn after overdub stop, classifying visual/save debt as `pre_existing_debt`, `stop_created_debt`, `stop_amplified_debt`, or `newly_scheduled_after_stop` using pre-stop vs post-stop dirty/pending snapshots.
- Causality capture [`224752`](../../captures/session_20260820_224752.log) confirms stop-created visual debt in the first PLAYING maintenance turn (`ODUB,stop,debt_handoff=stop_created_debt`, 10/10 stops). Follow-up scheduling trim: while `playingMidiDrainAfterOverdubStopActive()` is true, idle visual cache slices are capped to 1 bar and mixed-dirty long-loop fallback stays neighborhood-bounded (no `UINT32_MAX` distance override in that drain window).
- Drain-window clamp now applies to all tracks in the same loop turn (via `main()` `postOverdubPlayingMidiDrain` -> `Track::processDeferredIdleMaintenance(..., postOverdubDrainWindowActive)`), so non-owner tracks cannot run broader PLAYING idle visual slices while a post-overdub drain window is active.
- Drain-window policy now restricts PLAYING idle visual maintenance to the overdub-stop owner track while the post-overdub drain window is armed; non-owner tracks defer their visual idle slices until the owner drain window clears, preventing unrelated pre-existing debt from consuming the same first post-stop MIDI turn.
- Behavior scope guard: this drain-window policy is stop-only. It is armed by `stopOverdubbing` (`armPlayingMidiDrainAfterOverdubStop`) and reset by `startOverdubbing`; the clamp gate applies only to PLAYING/STOPPED_RECORDING maintenance. OVERDUBBING and overdub-wrap commit paths keep normal note-update processing (`commitOverdubWrapAtSessionStart` still seals companions with source-view update enabled).
- Added post-stop arbitration gate in `main()` via `MidiServiceDrain::allowDeferredSave`: while the post-overdub drain window is active and deferred save work is pending, `StorageManager::processDeferredSaveState` is deferred for that loop turn so `idle_maint` and `persist_save` do not stack in the same drain-window slot.
- Added post-stop visual slice append guard in `Track::processDeferredIdleMaintenance`: for the full post-overdub drain window, `Loop::rebuildVisualCacheIdleSlice` runs without wrap-pair append (`allowOverdubWrapAppend=false`) so expensive `idle_append` work does not stack into stressed post-stop runtime windows.
- Added constrained fallback gather mode for the full post-overdub drain window: `Loop::rebuildVisualCacheIdleSlice(..., constrainFallbackGather=true)` removes pad bars and skips active edit-row projection in fallback gather windows, so post-stop fallback slices stay bounded when prepared windows are unavailable.
- Added no-progress constrained-slice backoff in `Track::processDeferredIdleMaintenance`: when a constrained fallback slice does not reduce dirty bars, the next constrained slice is deferred for `250 ms` (`overdubDrainFallbackRetryAtMs_`) to prevent repeated high-cost fallback churn in adjacent loop turns.
- Added non-overdub STOPPED follow-up optimization in `StorageManagerInternal::enqueueRemainingLoopSlotRestores`: boot background-fill now reuses boot-scanned in-RAM slot payload flags and skips per-slot SD reprobe during full enqueue, removing one-turn `SD.exists`/token-scan churn from `boot_commit`/`load_frame`.
- Further optimized `enqueueRemainingLoopSlotRestores` bulk admission: per-entry priority computation and per-entry queue sort are now deferred during full boot enqueue, then applied once via final `reprioritizeDeferredLoopSlotRestoreEntries()`, reducing boot-commit scheduler churn without changing restore ordering semantics.
- `enqueueRemainingLoopSlotRestores` now admits background-fill queue entries incrementally across loop turns (`kBootAdmissionEntriesPerTurn=4`) and only finalizes reprioritize/sort after admission completes, so STOPPED boot does not pay full remaining-slot admission in one `boot_commit` / `load_frame` window.
- Background `LoadLoopJob` stepping now enforces a transport-clock-safe resume policy: Low (non-focus) load jobs are disallowed while global transport clock is running and are parked/deferred for retry later, so slot-load catch-up cannot compete with active MIDI timing service.
- STOPPED deferred-maintenance scheduling now uses cancel/resume policy in `main()`: when all tracks are STOPPED, run `Track::processDeferredIdleMaintenance` for one track per turn (round-robin), and skip STOPPED maintenance entirely while global transport clock is running.
- Realtime visual-cache slices now defer wrap-pair append (`idle_append`) while playback/capture states are active; deferred append is resumed in STOPPED idle (`Loop::applyDeferredVisualCacheWrapAppend`) so MIDI service keeps priority during PLAYING/OVERDUBBING while append work restarts later.
- Realtime visual-cache gather restart cooldown (`activeVisualSliceRetryAtMs_`) was rolled back after reboot evidence in [`012714`](../../captures/session_20260821_012714.log) to remove the newest strict throttle while retaining append defer/resume and post-overdub drain-window guards.
- New capture attribution from [`010055`](../../captures/session_20260821_010055.log): the two ~500 ms active-window outliers are deferred-maintenance owners, not stop-path owners — `DIAG,midi_gap=534.305 ms` aligns with `DIAG,idle_maint=519.178 ms`, and `DIAG,midi_gap=564.640 ms` aligns with `DIAG,idle_maint=535.469 ms` + `DIAG,playback_build=85.417 ms`.
- Follow-up capture [`011853`](../../captures/session_20260821_011853.log) confirms active-window outliers moved away from `idle_append`: the remaining ~500 ms windows align with `idle_gather` + `idle_maint` (`midi_gap=582.155 ms` with `idle_maint=567.506 ms`; `midi_gap=582.887 ms` with `idle_maint=568.300 ms`), while the only >500 ms `idle_append` (`949.485 ms`) occurs in STOPPED with immediate `VCACHE,append_resume`.
- Added resumable window traversal in `CommittedEventRange::appendWindowBudgeted` (cursor + per-slice event budget) and rewired fallback gather in `Loop::rebuildVisualCacheIdleSlice` to use a Loop-owned gather session keyed by `(windowStart, windowLength, playbackRevision, includeActiveEditRows)`.
- Fallback gather now has explicit invalidation semantics: any window/revision/mode mismatch resets the session; cache invalidation/rebuild paths call `resetFallbackGatherSession`; incomplete slices return before reconstruction so runtime turns stay bounded.
- Added active/stopped fallback budgets in `Track::processDeferredIdleMaintenance` (`96` events during post-overdub drain, `192` during other realtime maintenance, `768` in STOPPED idle) and capture diagnostics (`DIAG,lcr,vch,gather_progress|gather_sort|gather_edit`) for phase-cost attribution.
- RAM1 remained within budget after this slice by reusing cold sort helpers and removing additional `std::sort` template instantiations from the realtime path (`teensy41-capture-serial` locals headroom restored).
- Validation capture [`015645`](../../captures/session_20260821_015645.log) confirms bounded overdub-stop owners and isolates one active fallback-gather churn outlier:
  - Overdub stop (7 published cycles): `ODUB,stop,display` **20.700-33.276 ms** (median **25.352 ms**), `ODUB,stop,seal` **11.567-21.813 ms** (median **14.569 ms**), `seal_publish_span_cache_restamp` **3.410-5.500 ms**, `seal_fx_seal_pending_changes` **1.310-1.935 ms**.
  - Active (`PLAYING`/`OVERDUBBING`) `DIAG,midi_gap`: **28.734-827.234 ms** (median **34.755 ms**); single long outlier `827.234 ms` maps to `DIAG,idle_maint=826.425 ms` + `DIAG,load_frame=23.535 ms` in `PLAYING` (`#CAP,31432460` report block).
  - Outlier window proof (`#CAP,30217106` → `#CAP,31417617`): **122** consecutive `DIAG,lcr,vch,gather_progress,ev=192,total=192,done=0,resume=0` lines immediately precede the outlier, with no `gather_sort`/`gather_edit` completion in that suffix, proving active-window fallback gather restart/no-resume churn under the current session key.
- Follow-up owner fix attempt (in-flight session window hold) was reverted in the same session after startup instability report; keep active behavior on the last known-stable fallback-session semantics while root-causing a safer anti-reset strategy.
- `applyNoteEditPassSequence` now prefilters `Update/Delete` rows by currently present target `NoteId`s while preserving row order, and maintains the present-id set through `Create/Delete` so fallback gather avoids repeated no-op target scans in dense active edit-row sessions.
- `applyNoteEditPassSequence` Phase A planner is now active with strict barrier semantics: only contiguous `Update` rows for the same `targetNoteId` + same property collapse, and only for `Velocity`/`Pitch` updates; any other row remains an execution barrier.
- Added executor-equivalence coverage for the Phase A planner in `test_edit_apply` (`test_sequence_phasea_collapse_contiguous_velocity_and_pitch_equivalence`, `test_sequence_phasea_barrier_with_length_keeps_equivalence`, `test_sequence_phasea_randomized_equivalence_against_baseline`), validated against the non-collapsing baseline executor over deterministic randomized scenarios.
- `applyNoteEditPassSequence` and `planContiguousCollapsedRows` are now `NOTE_EDIT_MEM` to keep Phase A planner logic out of RAM1/ITCM on Teensy (`teensy41-capture-serial` locals headroom preserved).
- Phase B cache slice shipped in `applyNoteEditPassSequence`: added per-sequence NoteOn index reuse (`NoteOnIndexLookupCache`) with conservative invalidation semantics. Cache entries are positional indices and are cleared after structural row actions (`Create`, `Delete`, `NoteRange`, `Length`) and after any cache-stale skip path, so reuse is limited to ordering-safe windows (`Pitch`/`Velocity` updates and unchanged vector topology).
- Internal note-apply helpers now use cache-aware lookup (`findNoteOnByIdCached`) while preserving public behavior (`findNoteOnById` remains linear and unchanged for external callers); structural helpers also clear cache after in-function erase/sort paths (`applyDeleteNoteById`, `applyMoveNoteById`, `applyChangeLengthById`, `applyAddNote`).
- Added Phase B targeted equivalence coverage in `test_edit_apply` (`test_sequence_phaseb_length_then_same_note_update_equivalence`, `test_sequence_phaseb_length_with_interleaved_note_updates_equivalence`) to pin length-mutation invalidation safety before and after interleaved note updates.
- Validation capture [`082858`](../../captures/session_20260821_082858.log) confirms stop owners remain bounded after Phase A while active fallback edit work remains the outlier owner:
  - Overdub stop (11 published cycles): `ODUB,stop,display` **17.031-28.116 ms** (median **22.602 ms**), `ODUB,stop,seal` **9.855-17.730 ms** (median **14.207 ms**), `seal_publish_span_cache_restamp` **3.292-6.048 ms**, `seal_fx_seal_pending_changes` **0.440-1.999 ms**, `seal_fx_mark_display_ranges` **3.189-6.092 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **29.052-856.062 ms** (median **73.630 ms**). Peak active report `#CAP,90758349` maps to `DIAG,idle_maint=854.985 ms` and `DIAG,lcr,vch,gather_edit,us=826242`.
  - Second active outlier `#CAP,95758554` (`midi_gap=600.711 ms`) maps to `DIAG,idle_maint=585.338 ms` with concurrent deferred work (`persist_save=11.345 ms`).
  - Phase A reduces worst `gather_edit` from **1,083,993 us** (`015645`) to **826,242 us** (`082858`), but `gather_edit` remains large enough to dominate active runtime stalls; cache-oriented Phase B remains required.
  - Non-active outliers stay outside the overdub-stop owner boundary (`midi_gap=23,391,338 us` at `#CAP,35316786` aligns with boot/load `loop_rem,boot_commit` and `loop_rem,load_frame` in STOPPED startup windows).
- Validation capture [`084213`](../../captures/session_20260821_084213.log) confirms the Phase B cache slice shifts fallback edit cost out of active runtime windows:
  - Overdub stop (10 published cycles): `ODUB,stop,display` **17.509-47.266 ms** (median **23.202 ms**), `ODUB,stop,seal` **9.558-36.016 ms** (median **13.686 ms**), `seal_publish_span_cache_restamp` **2.826-5.960 ms**, `seal_fx_seal_pending_changes` **1.149-2.038 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **26.828-79.941 ms** (median **37.399 ms**, 21 samples), down from `082858` active max **856.062 ms** and median **73.630 ms**.
  - The two largest `DIAG,lcr,vch,gather_edit` events (`1,070,494 us` at `#CAP,164959999`; `464,307 us` at `#CAP,163097840`) occur in `STOPPED` state and map to `DIAG,loop_rem,idle_gather` / `DIAG,loop_rem,idle_maint`, not active MIDI-service windows.
  - The largest active `midi_gap` windows (`#CAP,90939663` at **79.941 ms**, `#CAP,75906773` at **78.345 ms**) show `persist_save` + `load_frame` stacking while `DIAG,idle_maint` stays low (about **5.3 ms**), and do not include active `gather_edit` outliers.
  - One stop-cycle display outlier (`ODUB,stop,display=47.266 ms`) is attributed to synchronous stop sub-stage `seal_fx_mark_display_ranges=26.886 ms` in the same cycle (`#CAP,111344311` series).
- Validation capture [`085458`](../../captures/session_20260821_085458.log) shows stop-path owners remain bounded but active runtime regressed back to idle-maintenance ownership:
  - Overdub stop (7 published cycles): `ODUB,stop,display` **20.501-33.291 ms** (median **24.063 ms**), `ODUB,stop,seal` **11.776-21.663 ms** (median **13.630 ms**), `seal_publish_span_cache_restamp` **4.019-6.123 ms**, `seal_fx_seal_pending_changes` **0.621-2.038 ms**.
  - Active (`PLAYING` + `OVERDUBBING`) `DIAG,midi_gap`: **23.825-2,893.664 ms** (median **36.546 ms**, 18 samples), vs `084213` active max **79.941 ms**.
  - Largest active windows map to `idle_maint` (not persist-save stacking): `#CAP,44306112` `midi_gap=2,893.664 ms` with `idle_maint=2,864.114 ms`, `load_frame=30.239 ms`, `persist_save=0.234 ms`; `#CAP,44282997` `midi_gap=2,865.744 ms` with `idle_maint=2,864.092 ms`, `load_frame=26.220 ms`, `persist_save=0.197 ms`.
  - OVERDUBBING outliers also map to fallback gather/edit cost: `#CAP,60224592` `midi_gap=842.661 ms` aligns with `idle_maint=840.200 ms`; immediately preceding owner lines show `DIAG,lcr,vch,gather_edit,us=813088` (`#CAP,58974437`) and `DIAG,loop_rem,idle_gather=817.671 ms`.
  - Additional active gather/edit spikes are present in OVERDUBBING (`gather_edit` up to **813.088 ms** and **753.577 ms**) with paired `loop_rem,idle_gather` / `loop_rem,idle_maint` windows, confirming the remaining owner is active fallback gather-edit churn.
- Active owner follow-up shipped: `main()` now gates `StorageManager::processDeferredSaveState` behind same-turn `load_frame` duration (`MidiServiceDrain::allowDeferredSaveAfterLoadFrame`) so timing-critical turns defer save when `load_frame >= 15 ms`; existing post-overdub drain-window save gate remains in effect. Native + `teensy41-capture-serial` build passed; recapture pending.
- Overdub-stop companion sealing now skips source-view mutation in the stop owner path (`sealPendingNoteChangesToEditPasses(!overdubStop)`), preserving the same persisted companions while avoiding source-view/spancache projection work during synchronous stop handoff.
- `sealPendingNoteChangesToEditPasses` is explicitly `LOOP_COLD_MEM` so this stop-only companion owner stays out of RAM1/ITCM budget.
- `commitPendingCapturePass` wrap-pair restamp guard now prefilters with head/tail window key sets before fallback scan, and missing-id collection batches then deduplicates once before reconstruction, reducing restamp bookkeeping churn in `seal_publish_span_cache_restamp`.
- Wrapped head/tail detection during span-cache restamp now uses bounded linear scan without the previous 17x128 stack table, removing large fixed stack footprint from the overdub-stop publish owner.
- `Loop::commitCapturePass`, `Loop::sealCapture`, and `Loop::finalizeCaptureWrapWindowAtStop` are explicitly `LOOP_COLD_MEM` to keep overdub-stop owner code on flash and restore RAM1 headroom in `teensy41-capture-serial`.
- `Track::stopOverdubbing` and the in-edit `handleNoteEditFold` stop path now use `SC_REC_FLUSH_PENDING_REVTS(128)` with Tier-A ODUB stop markers, preserving stop-stage observability without reintroducing broad flush churn in the synchronous owner path.
- `CaptureLineTier::isTierALine` now treats `ODUB,stop` lines as Tier-A so stop-stage telemetry remains visible even with deterministic `flush(8)` stop budgets under ring pressure.
- Companion-only projection fast path shipped in `Loop` window consumers: `copyEffectiveCommittedEventsInRange` and `advanceFallbackGatherSession` copy a ready Overdub companion cache only. A miss, including an empty projection, uses windowed apply. Full companion rebuild is not admitted from gather (Stage A; [`162928`](../../captures/session_20260821_162928.log)).

**Status:** Native **1399/1399** and `teensy41-capture-serial` build **PASS**. New baseline capture [`210319`](../../captures/session_20260820_210319.log) replaces older stop-lag envelopes for planning.

### Overdub-start begin-capture long-delay restoration — validated

**Evidence:** [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log)

**Owner:** `Loop::rebuildOverdubSourceSpanCache`, `Track::startOverdubbing` (`ODUB,stage,begin_capture`).

**Invariant:** Overdub start stays deterministic while an overdub session is active: if prepared spans are unavailable, start/wrap does not force a full-loop materialize on the timing-critical path.

**Root cause proved in captures:** `ODUB,stage,begin_capture` spiked to about **2.2 s** (`2194189 us`, `2248843 us`) in [`165956`](../../captures/session_20260820_165956.log), and `Live Overdub -> Overdubbing started` was delayed by **2143-2249 ms** on early starts. The same pattern appears in [`165534`](../../captures/session_20260820_165534.log) (~2102/2189 ms). This directly maps to `rebuildOverdubSourceSpanCache` falling back to `passes.materializeToEventVector` when prepared spans are unavailable.

**Stage change shipped:** Restored overdub-session cache fallback in `rebuildOverdubSourceSpanCache`: when prepared spans are unavailable during an active overdub session, reuse prior cache notes instead of forcing full-loop materialization on start/wrap. Non-session behavior remains unchanged (full materialize is still allowed outside active overdub sessions).

**Status:** Native **1397/1397**. `teensy41-capture-serial` build **PASS**. New baseline [`210319`](../../captures/session_20260820_210319.log): `ODUB,stage,begin_capture` stays bounded at **1851-6443 us** (median **2070 us**), confirming start stalls are no longer the active bottleneck.

**Historical outlier context:** [`170724`](../../captures/session_20260820_170724.log) contains a one-cycle stop-path outlier (`ODUB,stop,seal=310980 us`, `display=320406 us`). The new planning baseline is [`210319`](../../captures/session_20260820_210319.log) runtime windows above (published stop display 56.451-71.803 ms; first post-stop MIDI Input Gap 78.634-145.373 ms).

### Overdub-start reboot — reverted to baseline, blocked on instrumentation

**Evidence:** [`162146`](../../captures/session_20260820_162146.log), [`132145`](../../captures/session_20260820_132145.log), [`143518`](../../captures/session_20260820_143518.log)

**Owner:** `Track::startOverdubbing`, `TrackManager::startOverdubbingTrack`, `DebugSessionCapture` capture ring.

**Symptom:** USB drops immediately after `[TRACK] Overdubbing started @ tick N`, the final statement of `Track::startOverdubbing`. Reproduces on first and second overdub start.

**Not caused by the 2026-08-20 removals.** The reboot is present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), before any of them. Commits `4af158a`, `39b8e1d`, `983262d`, `98699b6`, `6ff4142` are reverted; the tree is byte-identical to `28cf6e3`. Pass accumulation is also ruled out — the first capture of the day already reached `pass,99` and the last reached `pass,101`.

**Capture blind spot (why eight successive fixes all "made no difference"):** two independent mechanisms starve overdub telemetry out of the ring.

| Mechanism | Owner | Effect |
|---|---|---|
| Flush budget `<= 8` takes the drop-only branch | `DebugSessionCapture::flushCaptureBuffer` | Discards up to 8 non-Tier-A head records and returns without transmitting. The overdub `SC_REC_FLUSH_PENDING_REVTS(8)` sites delete telemetry instead of sending it. |
| Tier-B append refused while ring is full and head is Tier-A | `DebugSessionCapture::appendCaptureRecord` | `ODUB,stage`, `MI`, `MO`, `LED` never enter the ring. `DIAG,lcr,` and `VCACHE,` are Tier-A per `CaptureLineTier::isTierALine`. |
| `formatPhaseLine` 1 s rate limit never engages | `LoopContentResolution` device-gate slice machine | `stepChanged` is true on every slice because the machine alternates `idx` ↔ `pair`, so the Tier-A `DIAG,lcr,phase` line emits per slice, not per second. |

In [`162146`](../../captures/session_20260820_162146.log) the last `#CAP,…,MI,…` is at 9.387 s and the last `LED` at 14.89 s, while `DIAG,lcr,phase` keeps emitting to 33.38 s (355 lines). The record button press at 35.198 s produced a text log line but **no** `MI` capture line, so Tier-B appends were already being refused when overdub started. `ODUB,stage,manager_enter` / `manager_done` are therefore absent for instrumentation reasons, not because that code did not run.

**Fault reason never observed.** `setup()` prints `CrashReport` before `logger.setup(LOG_DEBUG)`, and every reconnect boot in the 2026-08-20 captures begins at `Logger initialized with level: 3`. The pre-logger boot output is absent from all of them. The firmware also writes `CrashReport` to `crashlog.txt` on SD (`main.cpp` `setup()`); that file is the authoritative record and has not been read.

**Next:** read SD `crashlog.txt` to classify the fault (allocator abort vs hard fault vs watchdog) before any further code change. Do not attribute the reboot to a removal again without an `ODUB,stage` line or a crash record.

**Status:** Native **1397/1397**. `teensy41-capture-serial` RAM1 code **424572** / locals **4768**. Not flashed.

### Cleanup branch telemetry/fader compile-gating — shipped stage

**Branch:** `chore/cleanup-codebase-tidiness`

**Owner:** `MidiHandler::mirrorUsbFaderProbePassthrough`, `RuntimeTimingTelemetry` callsites, overdub stop telemetry callsites.

**Invariant:** Plain `teensy41` builds do not execute telemetry-only timing work (`micros()`/heap snapshots/local telemetry structs) when the owning capture or perf feature is disabled; fader USB-host helper logic is compiled only with `MIDI_USB_FADER_PROBE_PASSTHROUGH`.

**Status:** **Verified** `pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial` with zero compiler warnings.

### Overdub session index reboot undo — HITL PASS

**Plan:** [`overdub_session_index_reboot_undo_bugfix.md`](../Plans/overdub_session_index_reboot_undo_bugfix.md)  
**Evidence:** [`003854`](../../captures/session_20260820_003854.log)

**Owner:** `Loop::openOverdubSession`, `deriveContentUndoUnits`, `StorageLoopIo` `OSI1`.

**Invariant:** One overdub session is one `OverdubPassAdded` before and after reboot. Wraps share `overdubSessionIndex` (same grouping role as `editPassIndex`). Missing `OSI1` stays one unit per pass.

**Status:** **HITL PASS** [`003854`](../../captures/session_20260820_003854.log). Native **1397/1397**. RAM1 code **425548** / locals **4768**. Three wraps + stop; reboot `Undo (entries=2)` `kind=1` `undo_count=179`; `DISP` 112→14; redo restores 112.

### Overdub-stop handoff flash

**Plan:** [`display_overdub_stop_handoff_flash_bugfix.md`](../Plans/display_overdub_stop_handoff_flash_bugfix.md)  
**Evidence:** [`000553`](../../captures/session_20260820_000553.log)

**Owner:** `DisplayManager::resolveDisplayNotesCommitted`, `preferPreservedOverdubStopHandoff`.

**Invariant:** While `visualCacheDirty` after overdub stop, the revision-matched composed frame stays paint authority. Follow re-filters that frame; it does not replace it with pre-commit `visualCache.notes`.

**Status:** **HITL PASS** [`001925`](../../captures/session_20260820_001925.log). Native **1391/1391**. RAM1 code **425276** / locals **4768**. Evidence [`000553`](../../captures/session_20260820_000553.log) remains the failing baseline (655→583 at `vis=2166`).

### Display window follow readiness

**Plan:** [`display_window_follow_readiness_bugfix.md`](../Plans/display_window_follow_readiness_bugfix.md)  
**Evidence:** [`232337`](../../captures/session_20260819_232337.log)

**Owner:** `DisplayManager::resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`, `resolveWindowedDisplayNotes`, `visualCacheCoversWindow`.

**Invariant:** Long-loop committed paint filters `visualCache.notes` for the paint window plus follow margin/lookahead. A dirty cache does not keep a 16-bar `overdubSourceViewNotes` frame as display authority.

**Status:** **HITL PASS** [`000553`](../../captures/session_20260820_000553.log) — `verify_follow_window_readiness` ok (0 stale holds). Native **1390/1390**. RAM1 code **425276** / locals **4768**. Evidence [`232337`](../../captures/session_20260819_232337.log) remains the failing baseline.

### Overdub participant discovery (occupy / source-view RC closed)

Occupy source-view resolver geometry ([`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)) and wrap display D2-D ([`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md)) are **FROZEN** — HITL [`174246`](../../captures/session_20260819_174246.log). Native **1384/1384**. Next on this branch: 64-bar `from=span` HITL **parked** (DEC-041); see § Parked below.

### Overdub ledger completion — shipped (Stages 2–4)

**Plan:** [`overdub_ledger_completion_enhancement.md`](../Plans/overdub_ledger_completion_enhancement.md)

**Owner:** `Loop::ensureOverdubSourceNotesForHold`, `Loop::accumulatePendingNoteChangesForIncomingNote`.

**Invariant:** Source-view rebuild and long-loop note-off hold hydration both read the source span cache (`from=cache`) with prepared spans when available and full-loop resolved spans otherwise; source-view path no longer calls `resolveWindow`.

**Status:** Native **1387/1387**. Baseline cost anchor [`202256`](../../captures/session_20260819_202256.log) (32× `from=win`, `merged=0`). Device check [`221834`](../../captures/session_20260819_221834.log): hold path `from=cache` with no `hold,miss`. Overdub-entry stall RC: companion sealing now restamps source-span cache revision only for active overdub sessions with full companion seal success, preventing stale-stamp rebuilds on the next overdub entry while preserving non-session rebuild behavior. Cold-start mitigation originally called `Loop::prewarmOverdubSourceSpanCache` from STOPPED idle after visual cache cleanup. `754db31` removed that caller because same-turn prewarm after `slice_clean` hung [`160519`](../../captures/session_20260820_160519.log). Stage B restores the caller on a later clean-cache turn after `boot_commit`. Device PASS [`165107`](../../captures/session_20260821_165107.log) after [`162928`](../../captures/session_20260821_162928.log) opened `notes=0` without it.

### Consume id-resolution completeness — FROZEN (Stages 1–2)

**Plan:** [`overdub_consume_id_resolution_completeness_bugfix.md`](../Plans/overdub_consume_id_resolution_completeness_bugfix.md)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote`. All holds → `effectiveOverlapNoteIds` (incoming ids + geometric union + long-loop hold fill), then `appendNotesForIds` only. `collectConsumeWindow` **removed**.

**Invariant:** Every geometric consume participant resolves via `appendNotesForIds`; no window scan.

**Status:** Native **1387/1387**. HITL Stage 1 [`195016`](../../captures/session_20260819_195016.log); Stage 2 [`201457`](../../captures/session_20260819_201457.log): zero `DIAG,consume` / `scanadd` / `norow`; occupy **86/86** `eq=1`. RAM1 code **425964** / locals **4768**.

**Closed predecessor:** [`overdub_consume_ledger_merge_enhancement.md`](../Plans/overdub_consume_ledger_merge_enhancement.md) Stage 1A/1B attribution — selection-change rejected on [`191133`](../../captures/session_20260819_191133.log).

### Consume candidate attribution — Stage 1A/1B COMPLETE (observability only, superseded)

**Plan:** [`overdub_consume_ledger_merge_enhancement.md`](../Plans/overdub_consume_ledger_merge_enhancement.md)

**Owner:** `Loop::accumulatePendingNoteChangesForIncomingNote`. Selection behavior **unchanged** — this does **not** start the parked consume merge.

**Invariant:** consume candidate selection is measured, not changed: every candidate is attributed to the occupy-id lookup, the window scan, or late JIT materialization.

**Status:** Counters + `DIAG,consume,select` shipped. **1B detail diagnostics shipped:** `DIAG,consume,norowid` (ids that failed to resolve to a source-view row) and `DIAG,consume,scanadd` (scan-added candidate ids with `in_ids=0|1`). **Consume merge selection change REJECTED** — HITL [`191133`](../../captures/session_20260819_191133.log): 3 of 5 attributed holds had a **non-empty** occupy set where the window scan was the only path to a participant (2× `idsel=1`, 1× `norow=1`). Follow-up HITL [`193024`](../../captures/session_20260819_193024.log): `norow` did not reproduce (`norow=0`, `norowid=0`), while `scanadd` persisted (18 lines, all `in_ids=0`). Empty-ids fallback remains load-bearing. Native **1386/1386**. RAM1 code **425964** / locals **4768**.

**Stage 1A guard check:** `overlapNoteIds` is written by `snapshotOverlapHoldCandidates` (open at `holdStart`) **and** `collectOverlapHoldPlaybackNoteOn` (NoteOns during the hold, from `sendMidiEvent` before the `playbackEmitMidiOutput_` gate). `appendNotesForIds` cannot reach a note absent from `overdubSourceViewNotes_`, so the long-loop JIT branch is the only path to a JIT-merged ahead note — proven by `test_consume_attribution_counts_late_note_for_jit_ahead_candidate`. Blocking additive candidates on non-empty ids therefore changes long-loop consume, which a 1-bar gate cannot observe.

**Long-loop JIT class did not reproduce:** `why=hold` **140** with long loops present (`live=18432`, `live=52224`), yet every line is `jit=0` / `late=0`. Reachable in native, not the device problem.

**Closed 2026-08-19.** Stage 1A delivered its purpose: the attribution evidence decided the selection question. Stages 1 and 2 (selection change) are **rejected** — do **not** block additive scan candidates on non-empty ids. The `norow` class is **tagged for investigation** in § Parked, no plan opened.

### Occupy source view keeps resolver geometry — FROZEN

**Plan:** [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)  
**Closed:** 2026-08-19 — **Gate 5A** shipped; extra covering `a>n` **0** [`161349`](../../captures/session_20260819_161349.log). Display/source-view fix: sibling D2-D [`174246`](../../captures/session_20260819_174246.log). **Gate 5B withdrawn.** Do not reopen without new `a>n` or pin [`121141`](../../captures/session_20260819_121141.log) `n=1 a=2`.

### Overdub wrap display drop — FROZEN

**Plan:** [`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md)  
**Closed:** 2026-08-19 — D2-D revision-freshness guard in `committedPlaybackNoteOnIdentityValid`. HITL [`174246`](../../captures/session_20260819_174246.log): DISP monotonic, `identity_invalid` **0**, ledger **95/95**. Lane **N** diagnostic only. Layer 1 `prep=0` [`170838`](../../captures/session_20260819_170838.log) **parked**.

### Occupy missing open identity (`n=1 a=2` extra covering span)

**Plan:** [`overdub_occupy_missing_open_identity_bugfix.md`](../Plans/overdub_occupy_missing_open_identity_bugfix.md)  
**Parent:** [`overdub_occupy_leftover_identity_bugfix.md`](../Plans/overdub_occupy_leftover_identity_bugfix.md) — leftover HITL **PASS** [`111819`](../../captures/session_20260819_111819.log)  
**Pin:** [`111819`](../../captures/session_20260819_111819.log) L948 pitch 24 `hs=72` `n=1 a=2` covering 5893 `0–743` + 5901 `0–168`

**Invariant:** A source-view / prepared identity counts as present-at-hold only if that `noteId` has a NoteOn in the complete committed playback stream.

**Status:** Native **PASS** 1374/1374. RAM1 **425948** / **4768**. HITL [`121141`](../../captures/session_20260819_121141.log): 5893-class **MET**. Remaining `n=1 a=2` **2 FAIL** — extra covering ids **have** a merged NoteOn (`on=1`). This RC’s root cause is invalid for those FAILs. **STOP** — do not widen the identity filter. Successor: source-view resolver geometry above.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; FIFO; option B; leftover ledger erase; `isPlaybackCatchUpWindow` equal-tick contract.

### Occupy leftover identity after rematerialize

**Plan:** [`overdub_occupy_leftover_identity_bugfix.md`](../Plans/overdub_occupy_leftover_identity_bugfix.md)  
**Parent:** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](../Plans/overdub_occupy_clock_duplicate_off_ledger_bugfix.md) — HITL [`104654`](../../captures/session_20260819_104654.log) duplicate-Off apply **not** this FAIL  
**Pin:** [`104654`](../../captures/session_20260819_104654.log) 19× `n=1 a=0` pitch 12 `lid=5701` `lst=96`

**Invariant:** An open ledger identity is retained iff its NoteOn identity exists in a complete rebuilt committed playback stream.

**Native:** **PASS** 1371/1371. RAM1 **425932** / **4768**.  
**HITL [`111819`](../../captures/session_20260819_111819.log):** leftover **MET**. `n=1 a=0` **0**. `n=0 a=1` **0**. `led == n` **1/1**. `5701` gone. 11 `ledger,erase`. Remaining 1 mismatch is `n=1 a=2` pitch 24 `hs=72` (two covering spans, one Entry) — not this FAIL.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; FIFO; option B; clearing the whole ledger; `isPlaybackCatchUpWindow` equal-tick contract; duplicate-Off ledger apply.

### Occupy clock duplicate Off skips ledger

**Plan:** [`overdub_occupy_clock_duplicate_off_ledger_bugfix.md`](../Plans/overdub_occupy_clock_duplicate_off_ledger_bugfix.md)  
**Parent:** [`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md) — HITL [`103234`](../../captures/session_20260819_103234.log) `led == n` **MET**  
**Pin:** [`103234`](../../captures/session_20260819_103234.log) L6674 pitch 24 `n=3 a=2`; two `Off@71`

**Invariant:** A second committed NoteOff (or NoteOn) at the same phase, pitch, and type still applies to `ActiveNoteLedger`; only the MIDI wire is deduped.

**HITL [`104654`](../../captures/session_20260819_104654.log):** `led == n` **41/41**. `n=0 a=1` **0**. Pin two-Off dumps **0**. Leftover counts **FAIL**: `n=1 a=0` 18→19 (all pitch 12 `lid=5701` `lst=96`); `n=3 a=2` 1→2 (nested On@240 pair + leftover, not two Off@71). `n=2 a=1` 20→10. Do not reopen this apply. Successor: leftover identity above.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; `rebuildPlaybackOrder`; duplicate `noteOn` no-op push.

### Occupy duplicate open identity (catch-up then clock)

**Plan:** [`overdub_occupy_duplicate_open_identity_bugfix.md`](../Plans/overdub_occupy_duplicate_open_identity_bugfix.md)  
**Parent:** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md) — HITL [`101319`](../../captures/session_20260819_101319.log) extra-open remains  
**Pin:** [`101319`](../../captures/session_20260819_101319.log) L2318 `n=2` `led=4`

**Invariant:** An open playback identity occupies at most one ledger Entry. A second NoteOn with that `noteId` does not push; `applyPlaybackEvent` still returns true so clock can emit.

**HITL [`103234`](../../captures/session_20260819_103234.log):** `led == n` **42/42 MET**. `n=0 a=1` **0**. Extra unique ids remain (`n=2 a=1` 25→20). `n=1 a=0` rose 3→18 — successor above (clock duplicate Off skips ledger), not exclusive-end. All mismatches `cu=0`.

**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTick` from USB; Off stamping; `rebuildPlaybackOrder`.

### Occupy catch-up per-phase Off then On (open-NoteOn stack)

**Plan:** [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md)  
**Parent:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](../Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md) — nested HITL **MET** [`095902`](../../captures/session_20260819_095902.log)  
**Pin:** [`095902`](../../captures/session_20260819_095902.log) pitch 12 `hs=336` `n=3 a=1` (ended 5218/5224 still open)

**Invariant:** `applyOpenClosedInterval` applies in-interval events in phase order, Off before On at each phase. An untagged Off can close a NoteOn that started in the same `(lastTick, occupy]` window.

**HITL [`101319`](../../captures/session_20260819_101319.log):** extra-open **not met** (`n>a` 35 vs 40 on [`095902`](../../captures/session_20260819_095902.log); `n=2 a=0` 15→1; `n=2 a=1` 6→25). One span-start `n=0 a=1` (pitch 23 `hs=384`). All 37 mismatches `cu=0`. Native fixture still holds. Do not widen catch-up; next is clock-path.

**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; occupy catching up when `occupyPhase <= lastTick`; `rebuildPlaybackOrder`.

### Occupy open-NoteOn ledger cardinality — DEC-042 (nested HITL MET)

**Plan:** [`overdub_occupy_active_note_ledger_cardinality_refinement.md`](../Plans/overdub_occupy_active_note_ledger_cardinality_refinement.md)  
**Decision:** [DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)  
**Parent:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md) — geometry proven [`092336`](../../captures/session_20260819_092336.log)

**Invariant:** `ActiveNoteLedger` holds every open playback NoteOn. Occupy walks `forEachActive`. Untagged Off → LIFO; identified Off → exact or orphan (no LIFO fallback). `noteId()` is compatibility newest-on-lane only.

**HITL [`095902`](../../captures/session_20260819_095902.log):** nested `n=0 a=1` **0 / MET**. Structural `n=2 a=2` **2**; leftover `n=1 a=2` **1** (pitch 24 same-tick `On@456` pair). Unmasked extra-open: `n=3 a=1` **18**, `n=2 a=0` **15**. All 44 mismatches `cu=0`. Extra-open is the catch-up stack RC above — not a DEC-042 fail.

**Does not reopen:** Off stamping; option B; clock equal-phase Off before On; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`.

### Occupy unmatched Off vs ledger — observability (geometry proven)

**Investigation:** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md)  
**Successor:** cardinality refinement above.

### Occupy clock same-tick Off before On — shipped

**Plan:** [`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)  
**Parent (catch-up two-pass shipped, gate not met):** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Pin:** [`235314`](../../captures/session_20260818_235314.log) — **4 `n=0 a=1`**; **0 `n=1 a=0`**; span-start L4811 / L4899 are USB-after-clock  
**HITL PASS:** [`001021`](../../captures/session_20260819_001021.log) — **0 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 82/82 occupies. L2715 `240–336` @ 240 is `n=1 a=1`.

**Invariant:** Clock playback at equal phase applies Off before On so abutting same-pitch replacement last-writes the new On. Occupy still reads `Entry.noteId`. Catch-up skip bound unchanged (`occupyPhase <= lastTick` still skips).

**HITL gate:** **met.** Occupy 12 @ `hs=0` sounding `n=1 a=1`. Do not treat `n=1 a=2` as this FAIL. 235314 interiors L4750/L4294 were not in this capture; `n=0 a=1` is 0 so unmatched-Off is not opened.

**Successor (observability):** [`overdub_occupy_unmatched_off_ledger_investigation.md`](../Plans/overdub_occupy_unmatched_off_ledger_investigation.md) — FIFO stamping rejected; geometry now **proven** as a nested same-pitch pair ([`090050`](../../captures/session_20260819_090050.log)).

**Does not reopen:** occupy repairing clock when `occupyPhase == lastTick`; `playMidiEvents` from occupy; folding capture into `mergedMidiEvents`; changing `applyPlaybackLedgerEvent`. Catch-up apply order vs stack: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).

### Occupy same-tick Off before On — catch-up interval

**Plan:** [`overdub_occupy_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_same_tick_off_before_on_bugfix.md)  
**Parent (interval catch-up shipped, gate not met):** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](../Plans/overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Pin:** [`233247`](../../captures/session_20260818_233247.log) — **11 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 100/100 occupies  
**HITL FAIL:** [`235314`](../../captures/session_20260818_235314.log) — **4 `n=0 a=1`**; **0 `n=1 a=0`**; `hs=` on 133/133 occupies. Remaining span-start fails were clock equal-tick — successor HITL **PASS** [`001021`](../../captures/session_20260819_001021.log). Global two-pass vs open-NoteOn stack: [`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md).  
**USB `playMidiEvents` catch-up:** **reverted** [`214856`](../../captures/session_20260818_214856.log) — do **not** call `playMidiEvents` from occupy

**Invariant:** When reconstructing committed ledger state over `(lastTickInLoop, occupyPhase]`, equal-tick replacement resolves Off before On. Occupy still does not advance playback. Apply order is per-phase Off then On in `CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval` ([`overdub_occupy_catchup_open_note_stack_bugfix.md`](../Plans/overdub_occupy_catchup_open_note_stack_bugfix.md)). Clock owns cursor, `nextEventIndex`, `lastTickInLoop`, send, wrap, and capture.

**HITL gate:** leftover `n=1 a=0` **met**. `n=0 a=1` not met (4) — successor above.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager patch; setting `lastTickInLoop` or `nextEventIndex` from USB; changing `applyPlaybackLedgerEvent`.

### Occupy on-tick before clock interval — ledger catch-up

**Plan:** [`overdub_occupy_on_tick_clock_catchup_bugfix.md`](../Plans/overdub_occupy_on_tick_clock_catchup_bugfix.md)  
**Parent (FROZEN leftover met):** [`overdub_occupy_merged_capture_ledger_bugfix.md`](../Plans/overdub_occupy_merged_capture_ledger_bugfix.md)  
**Pin:** [`231038`](../../captures/session_20260818_231038.log) — 6 `n=0 a=1` pitch 12; `n=1 a=0` = 0  
**HITL FAIL:** [`233247`](../../captures/session_20260818_233247.log) — interval catch-up in tree; leftover `n=1 a=0` met; remaining 11 `n=0 a=1` are successor above.

**Invariant:** USB occupy may advance the committed ledger to the USB phase; it must **not** advance playback. Occupy consumes existing committed-only `mergedMidiEvents` via `catchUpCommittedPlaybackLedgerToPhase` (`(lastTickInLoop, occupyPhase]`). Skip wrap-crossing (`shouldCommitOverdubWrap`) and `occupyPhase <= lastTickInLoop`. Interval is trusted **after** Off-before-On in that interval.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16; folding capture into `mergedMidiEvents`; occupy fallback; DisplayManager patch; setting `lastTickInLoop` or `nextEventIndex` from USB.

### Occupy merged-capture ledger — FROZEN

**Plan:** [`overdub_occupy_merged_capture_ledger_bugfix.md`](../Plans/overdub_occupy_merged_capture_ledger_bugfix.md)  
**Parent (FROZEN):** [`overdub_occupy_capture_stream_ledger_bugfix.md`](../Plans/overdub_occupy_capture_stream_ledger_bugfix.md)  
**Pin (pre-fix):** [`224719`](../../captures/session_20260818_224719.log) — 8 `n=0 a=1`; 6 `n=1 a=0`  
**HITL leftover met:** [`231038`](../../captures/session_20260818_231038.log) — **0 `n=1 a=0`**. Remaining 6 `n=0 a=1` are successor above.

**Invariant:** Playback `runtime.mergedMidiEvents` is committed-only. Occupy reads `ActiveNoteLedger` written from that representation, wrap-pass, and loop-head. Live capture echo uses `playbackCursorAdvanceSendCapture` and must not last-write that ledger. Do not fold capture into `mergedMidiEvents` again.

### Occupy capture-stream ledger — FROZEN

**Plan:** [`overdub_occupy_capture_stream_ledger_bugfix.md`](../Plans/overdub_occupy_capture_stream_ledger_bugfix.md)  
Emit-only `playbackCursorAdvanceSendCapture` shipped. Remaining writer was live capture folded into `mergedMidiEvents` — successor above. Pin [`221334`](../../captures/session_20260818_221334.log). HITL FAIL [`224719`](../../captures/session_20260818_224719.log).

### Occupy `n=0 a=1` off tick 0 — display vs ledger (USB catch-up reverted)

**Investigation:** [`overdub_occupy_off_tick_display_investigation.md`](../Plans/overdub_occupy_off_tick_display_investigation.md)  
**Bugfix (reverted):** [`overdub_occupy_after_wrap_s_interval_bugfix.md`](../Plans/overdub_occupy_after_wrap_s_interval_bugfix.md)  
**Pin:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ storage **96** `n=0 a=1 b=1` (`204277855`) after wrap 2 (S=**64**)  
**HITL FAIL:** [`214856`](../../captures/session_20260818_214856.log) — catch-up did not occupy the named spans; `playMidiEvents` from occupy **reverted**

**Display:** OVERDUBBING paint is `resolveDisplayNotesLiveCapture` from `overdubSourceViewNotes` + `capturePreview`. Occupy is not a paint input. Do not patch DisplayManager.

**Named write (native only):** wrap-committed On@96 is not in wrap-S `(prev, S]`. Clock `playCommittedLoopMidi` must walk `(S, occupyTick]` after wrap reanchor at S. Do **not** call `playMidiEvents` from occupy (wrap can commit on USB NoteOn). Occupy stays a reader. CAP `as=`/`ae=` stays. Successor for USB-before-clock occupy is ledger catch-up above.

**214856 `n=0 a=1` spans (not On@96):** 30 `528–544`; 12 `0–192`; 12 `144–232`; 12 `720–767`; 24 `216–408`. Wraps at storage **8**.

**Does not reopen:** wrap-S `(prev, S]`; loop-head Q16 (HITL PASS below). No occupy fallback.

### Loop-head playback ledger after wrap — HITL PASS

**Plan:** [`overdub_loop_head_playback_ledger_bugfix.md`](../Plans/overdub_loop_head_playback_ledger_bugfix.md)  
**Investigation:** [`overdub_loop_head_playback_ledger_investigation.md`](../Plans/overdub_loop_head_playback_ledger_investigation.md)  
**Fail pin:** [`185831`](../../captures/session_20260818_185831.log) occupy 60 @ storage **0** `n=0 a=1 b=1` (`54243271`)  
**HITL PASS:** [`203948`](../../captures/session_20260818_203948.log) occupy 12 @ storage **0** `n=1 a=1 b=1` (`214037034`, `232043429`)

**Invariant:** Overdub wrap seal does not run Q16 min-length. Wrap-committed NoteOn @ 0 stays in `lastCommittedPassId()` so the existing 0-clock `atLoopStart` walk writes `ActiveNoteLedger` before occupy.

**Owner:** `Loop::sealCapture` (`CommitReason::OverdubWrap` skips `removePairsShorterThanNoteMinLength`). Occupy stays a reader. Do not add a loop-head catch-up.

**Native:** 1350/1350.

**Does not reopen:** wrap-S `(prev, S]` (HITL PASS below). No occupy fallback. `n=0 a=1` off storage 0 in [`203948`](../../captures/session_20260818_203948.log) (12 @ 96 after wrap 2) is outside this invariant.

### Wrap-tick ledger catch-up from committed pass — HITL PASS

**Plan:** [`overdub_wrap_committed_pass_playback_bugfix.md`](../Plans/overdub_wrap_committed_pass_playback_bugfix.md)  
**HITL PASS:** [`185831`](../../captures/session_20260818_185831.log) wrap at storage **696**, occupy 71 @ **704** `n=1 a=1 b=1`. Fail pin remains [`180844`](../../captures/session_20260818_180844.log) wrap 15.

### Occupy — lookup `Entry.noteId`

**Plan:** [`overdub_present_at_tick_jit_enhancement.md`](../Plans/overdub_present_at_tick_jit_enhancement.md)  
**Architecture:** [`overdub_present_at_tick_jit_architecture.md`](../Plans/overdub_present_at_tick_jit_architecture.md)  
**Decision:** [DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat) — `collectOverdubNoteOnParticipantIds` reads `ActiveNoteLedger` at `currentTick`; do **not** say occupy = ledger  
**Evidence:** [`132806`](../../captures/session_20260818_132806.log), [`213401`](../../captures/session_20260817_213401.log), [`152940`](../../captures/session_20260813_152940.log), [`152745`](../../captures/session_20260818_152745.log)

**Owners:** `Loop` / `LoopContentResolution` = `LoopPasses`. `playCommittedLoopMidi` writes `LoopPlaybackRuntime::ledger` via `applyPlaybackLedgerEvent` on committed-only `mergedMidiEvents` / wrap-pass / loop-head. Capture emit does not write. `sendMidiEvent` emits. Occupy reads every open identity via `forEachActive` ([DEC-042](../DECISION_LOG.md#dec-042-same-pitch-active-note-identity-is-a-cardinality-problem-not-an-off-identity-problem)).

**Product:** zero or more open NoteOns per `(channel, pitch)`. No `length` on `Entry`. `LoopPasses` may still overlap (`max_same_pitch=322`). `sendMidiEvent` does not create those entries.

**Stage 0:** **Yes** — `PresentNote.noteId` and playback `evt.noteId` are both `MidiEvent.noteId`. Not a 1:1 pitch lookup.

**Now:** occupy lookup **shipped** (`forEachActive` on the lane). CAP `from=ledger`. `noteId()` is compatibility newest-only. Consume stays on `overdubSourceView`. Do not copy `PresentNoteVec`. No `length`. Wrap-S ledger catch-up **HITL PASS** [`185831`](../../captures/session_20260818_185831.log). Loop-head storage-0 **HITL PASS** [`203948`](../../captures/session_20260818_203948.log). Clock equal-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log). Open-NoteOn cardinality HITL gate open.

**Parked:** `evaluateOccupyOverlap`; wait-STOPPED-for-`lcr,mat`; consume merge; “occupy = ledger” ownership transfer.

[`132806`](../../captures/session_20260818_132806.log): Stage 1c **held**. Do not wait STOPPED for `lcr,mat`. Do not reconstruct on the note.

### 64-bar source-view identity — Stage 1c shipped; `from=span` HITL parked

Membership, length identity, and dirty/save stall **shipped** ([`overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md`](../Plans/overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md)). [`132806`](../../captures/session_20260818_132806.log) Stage 1c held; 64-bar `from=span` not met (PLAYING during `prep`). That HITL is **parked** (DEC-041). Do not wait STOPPED for `lcr,mat`. Do not start consume merge.

### Overdub participant discovery — notes present at S (Phase 4 1-bar PASS)

**Plan:** [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md)  
**Parent:** [`consumer_window_budget_ownership_architecture.md`](../Plans/consumer_window_budget_ownership_architecture.md)  
**Evidence:** [`213401`](../../captures/session_20260817_213401.log)  
**Owner (shipped occupy):** `Loop::collectOverdubNoteOnParticipantIds` (`forEachActive` on the lane). Not a smaller source window.

**PresentNote** = LCR query snapshot at S (many `NoteId`s; no `endTick`). **ActiveNote** = open `ActiveNoteLedger::Entry` (several per `(channel, pitch)`). MIDI out = `midiHandler.sendMidiEvent`. Identity field: `noteId` on `Entry`; do not copy `PresentNoteVec`.

**Phase 4 1-bar HITL PASS** [`123803`](../../captures/session_20260818_123803.log): `collectConsumeWindow` skips `ensureOverdubSourceNotesForHold` when `loopLen <= overdubSourceWindowLengthTicks()`. Track 6 (768): **`why=hold` = 0**; occupy 102/130 `from=prep` `a=1,b=1` `eq=1`; consume still Hide (`hide` 3–11). Track 0 (50688): 36 `why=hold` remain (1 `merged=1`); consume still Add/Hide (`empty_sets=0`). `late_clk=0`. Native `test_note_off_skips_hold_fill_when_source_view_covers_loop`.

**Parked — 64-bar occupy via full-loop `from=span`:** [`122848`](../../captures/session_20260818_122848.log) `a=1,b=0` was RC1 (fixed). [`125542`](../../captures/session_20260818_125542.log) `a=1,b=0` = 0. Wait-for-`lcr,mat` **parked** ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Empty occupy is **not** “no participants.” Consume stays on `overdubSourceView`. Do not start consume merge.

**Phase 3 1-bar HITL PASS** [`121933`](../../captures/session_20260818_121933.log): occupied 48/74 `a=1,b=1`; no note-on `why=hold`. Production occupy is now `Entry.noteId`.

Phase 0b **done:** `PresentNote` alone is not enough for RC8 LinearSpan (`endTick` dropped; present on `NoteSpan` / `DisplayNote`). Observation B now walks prepared `NoteSpan`s with `displayNotePresentAtHold` (same function A uses). `notePresentAt` is unchanged (playback / checkpoint fill). Prepared miss still returns false; no cold `resolveState` on note-on. PLAYING / STOPPED / MUTED participant HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)): overdub only runs in OVERDUBBING.

Phase 1 **device capture** [`002447`](../../captures/session_20260818_002447.log) (`b9b9336`): 1-bar (768). 31 `lcr,part`. **`eq=0` = 0.** After wrap 1 and wrap 2, occupied holds are `a=1,b=1` (`ao=0`,`bo=0`). [`232510`](../../captures/session_20260817_232510.log) wrap-2/3 `b=k` extras are gone. Enter before first wrap is still `from=miss` (6). Hold fill still `from=win` on that capture. Production occupy is now present-at-S (Phase 3).

**Phase 2a** `76623cd` + **O:** `a384a47`. Device [`011009`](../../captures/session_20260818_011009.log) **PASS** for wrap-undo source-view refresh: `why=undo` on peel, post-undo A `a=1`, hold `merged=0`. **Phase 2 B collect** uses RC8 hold on prepared spans. Production consume still A.

**013327 wrap-crossing fill shipped** (native A==B). Device [`021716`](../../captures/session_20260818_021716.log) pre-wrap `a=0,b=2` at storage 64 is finished opens on prepared `NoteSpan`s — MIDI reconstruct (`finishOpenNotes=false`) never produced them. **Source-view span fill** `b94bd2b` ([`overdub_participant_source_view_span_membership_bugfix.md`](../Plans/overdub_participant_source_view_span_membership_bugfix.md)). Device [`024225`](../../captures/session_20260818_024225.log) empty `from=span,notes=0` wiped RC12 display. **Display wipe PASS** [`025337`](../../captures/session_20260818_025337.log): `why=open,from=win,notes=1`; `DISP` `PLAYING` 1 → `OVERDUBBING` 1; prepared `eq=0` = 0. [`025916`](../../captures/session_20260818_025916.log) / [`030219`](../../captures/session_20260818_030219.log) hit `COORD` storage 64 **before** first wrap, but those holds are `from=miss,a=0` on a 1-note loop (`notes=1`); 030219 pitches are 86/60 (021716's pitches). After wrap, storage 64 is `eq=1`. **030219 RC12 flood:** wrap 1 `from=span,ev=8,notes=34` vs `vch notes=4` (no `lcr,mat` this capture). Span copy now keeps only window NoteOn ids. **RC12 flood PASS** [`030958`](../../captures/session_20260818_030958.log) / [`032228`](../../captures/session_20260818_032228.log): wrap `lcr,src notes` equals `vch notes` (032228 all 7 wraps delta 0). Hide-all-spans native `7760811`. 032228 wrap 6–7 `bo=1` is B overcount of Disabled wrap-layer companions; B collect now skips index orphans, Disabled-pass companion restore, and Disabled restore when an Active companion already targets that id. Occupy stays A. Device [`034455`](../../captures/session_20260818_034455.log) **PASS**: 64 `from=prep` `eq=1`, **`eq=0` = 0** after undo then more wraps. Wrap 1-frame undo flash: span copy now uses the same Disabled-companion restore guards as B collect; session undo/redo invalidates the live display cache. Device [`040236`](../../captures/session_20260818_040236.log) **PASS** (flicker gone; 103 `from=prep` `eq=1`, `eq=0` = 0). 021716 storage-64-before-wrap is **not** a blocking gate. **Phase 3 in tree:** note-on occupy is `Loop::collectOverdubNoteOnParticipantIds` — prepared present-at-S when ready, else the source-view walk. `Track::snapshotOverlapHoldCandidates` no longer calls `ensureOverdubSourceNotesForHold`. CAP `a` is source-view without fill; production occupy is `b` when `from=prep`. Native: `test_note_on_occupy_*` (64-bar NOTE ON outside the 16-bar window still occupies from prepared spans; miss does not fill). Do not delete `overdubSourceView`. Do not start the Experiment 1 1/2/4/8/16 rebuild series as production policy. NOTE_EDIT overlap is a sibling consumer.

### Consumer window budget — Experiment 1 (detach landed; series not next)

**Plan:** [`consumer_window_budget_ownership_architecture.md`](../Plans/consumer_window_budget_ownership_architecture.md)  
**Owner:** `Loop::kOverdubSourceWindowBars` / `overdubSourceWindowLengthTicks`. Default **16**. CAP `lcr,src` includes `bars=`.

Detach from display 16 is in tree. A timing-passing clamp is evidence, not policy. Successor is notes present at tick `S` (above), not a smaller geometric window. Do not shrink 2-bar gather.

### Playback gather Stage 1 — MIDI deadline lateness hooks (measurement landed)

**Plan:** [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)  
**Owner:** `RuntimeTimingTelemetry`. No geometry change. No LCR consume.

Hot path accumulates `late_on` / `late_off` / `late_clk` (on time = sent before the next tick / one-tick clock window). ISR stores stay in ITCM (`noteClockPulse` region). Main loop `maybeEmit` drains a first-late one-shot (`DIAG,late_event`) and a gather rebuild one-shot (`DIAG,playback_build`), then the 5 s Tier-A `DIAG,late_*` window. No per-event `#CAP`. Native 1329/1329. `teensy41-capture-serial` links (RAM1 code 425852, locals 4768).

[`213401`](../../captures/session_20260817_213401.log): 2-bar `playback_build` 3–6 ms; 64-bar stall is source/hold on USB. Do not start Stage 2 stamp redesign, Problem B length, or LCR consume from this file.

### Display undo / wrap / pitch-move ghosts (RC-W1, RC-N1, RC-U1)

**Plan:** [`note_edit_display_undo_overdub_wrap_bugfix.md`](../Plans/note_edit_display_undo_overdub_wrap_bugfix.md)  
**Evidence:** [`144703`](../../captures/session_20260817_144703.log), [`144939`](../../captures/session_20260817_144939.log)

**Firmware committed** (`f0b0e66` / `142b95b` / `324ffdd` / `4aabf1c`). [`152627`](../../captures/session_20260817_152627.log): RC-W1 / RC-U1 **HITL PASS**. [`155450`](../../captures/session_20260817_155450.log): NOTE_EDIT exit `slice_clean`/`DFRAME` **5** (not [`153213`](../../captures/session_20260817_153213.log) `DISP 8`). [`153213`](../../captures/session_20260817_153213.log) exit 8 was stacked persist `60@64` (consume), not wrap live-cache.

Wrap invalidates live display cache; pitch commit retires persist-twin overlay and home pitch in `visualCache`; undo/redo uses idle visual-cache slices (`refreshVisualCacheAfterPassStateChange`). Do not reopen RC11/RC12. Do not change DEC-038.2 session undo grain.

### Wrap-crossing overdub consume — **shipped** (on device)

**Plan:** [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](../Plans/overdub_wrap_crossing_hold_head_consume_bugfix.md)  
**Firmware:** `e1f57c5`. **HITL PASS** [`155450`](../../captures/session_20260817_155450.log).

One wrap-crossing incoming note occupies `[S, L) ∪ [0, E)` as a single hold. NOTE_EDIT tick 64 is `1/2` (86 + `60@64–240`); [`153213`](../../captures/session_20260817_153213.log) was `1/3`. `overlap_hold` `hide=1` `add=1`. Do not reopen RC11/RC12. Do not patch `applyNoteEditPass`.

### Loop length during overdub (queued — do not start firmware)

**Plan:** [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)  
**Evidence:** [`140355`](../../captures/session_20260817_140355.log) @ 25.541 s — LOOP_EDIT CC `ch=15 cc=2 value=80` set 768 → 62208 ticks during overdub; MIDI length feedback lagged until ~38 s.  
**Parent authority:** [`overdub_lifecycle_representation_authority.md`](../Plans/overdub_lifecycle_representation_authority.md)

Do **not** start Stage 1–4 firmware until this file is explicitly in implementation (user approved the plan + tasks only). Do not reopen RC11 consume or RC12 `appendOverdubPassDisplayNotes` skip.

**RC11/RC12 FROZEN:** [`overdub_overlap_hold_display_cache_bugfix.md`](../Plans/overdub_overlap_hold_display_cache_bugfix.md) — HITL Gate 1–4 PASS [`140355`](../../captures/session_20260817_140355.log). Native 1294/1294. Commits `2e8f480` / `e1ebcbb`.

**RC10:** live capture paint applies pending Hide/Shorten on a copy.  
**RC9:** overdub transport stop finalizes pending before `sendAllNotesOff`.  
**RC8–RC6:** see parent [`overdub_overlap_hold_same_start_bugfix.md`](../Plans/overdub_overlap_hold_same_start_bugfix.md).

### NOTE_EDIT UNDO_WARM + commit-recon (investigation)

**Plan:** [`note_edit_undo_warm_missing_recon_investigation.md`](../Plans/note_edit_undo_warm_missing_recon_investigation.md)  
**Evidence:** [`143144`](../../captures/session_20260816_143144.log), [`145518`](../../captures/session_20260816_145518.log)

**Parked — wrap-move persist:** [`201446`](../../captures/session_20260816_201446.log) live wrap-move is linear (`EditSessionAction` 2832–3408, `DNTE` length **576**). Deselect persist LIFO-pairs false Off@2688 and leaves wrap Off@96; reselect `DNTE` length **336**. That failure is partly the current rematerialize / full-loop session-store structure. Do **not** add more LIFO / wrap-off persist patches in that structure. Re-evaluate after NOTE_EDIT hydrate if the 336/288 shorten remains. Do not patch `applyNoteEditPass` identity. Stage 2 / C5 / B2b / Layer D parked.

**Next Layer C (parked C5):** `OverlapCandidateLookup::appendNotesForIds` in overlay only — not overdub source-view / hold ids. Not reconstruct. Not empty-pair resolve.

### DEC-037 — LoopContentResolution parallel prototype

**Parked:** overlay loop picker (`set-revision-persistence` §4.8–4.10) — WIP stashed on `feature/set-revision-loop-picker`.

**Active:** native `LoopContentResolution` prototype. Do **not** optimize `materializeToEventVector` again. Do not wire resolution onto MIDI/display until three gates pass.

**Plan:** [`loop_event_sourced_resolution_architecture.md`](../Plans/loop_event_sourced_resolution_architecture.md)  
**6D investigation:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](../Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md)  
**6E overdub evaluation:** [`loop_content_resolution_overdub_state_evaluation_refinement.md`](../Plans/loop_content_resolution_overdub_state_evaluation_refinement.md) — **6E.1–6E.5 PASS**; [DEC-038](../DECISION_LOG.md#dec-038-overdub-wrap-commit-and-session-undo) **038.1 HITL PASS**; **038.2 landed** (one `OverdubPassAdded` `passIds` + STK3).  
**Handoff:** [`loop_content_resolution_stage9_handoff.md`](../Plans/loop_content_resolution_stage9_handoff.md)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)

**Now:** LED lookup Stage 1 **PASS** [`114736`](../../captures/session_20260816_114736.log) — [`post_undo_led_lookup_resumable_source_refinement.md`](../Plans/post_undo_led_lookup_resumable_source_refinement.md). No LED gather rem; BAR→LED 3.7 ms; PLAYING `clockrate` 47–48. Remaining PLAYING `midi_gap` 110–127 ms is idle/load, not lookup. Stage 2 **rejected**. Do **not** re-arm drain. Playback gather is a separate work path ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)) — do not start from this LED slice.

**Scheduler prep:** [`runtime_scheduler_lcr_consumer_grooming_refinement.md`](../Plans/runtime_scheduler_lcr_consumer_grooming_refinement.md). **Slice 1–4c device PASS.** Slice 4d firmware landed; [`143144`](../../captures/session_20260816_143144.log) moved NOTE_EDIT session work to the investigation above. **Slice 1b device PASS [`225626`](../../captures/session_20260816_225626.log)** — dirty PLAYING `midi_gap` = `idle_maint` 79–96 ms; child rem is `idle_append` only (72–82% of parent). **Slice 2b device PASS [`232423`](../../captures/session_20260816_232423.log)** — unprepared interior idle slices skip `appendOverdubPassDisplayNotes`; wrap-edge (bar 0 / last bar) still appends. 6B stops 3–7: no `idle_append` rem, `midi_gap` 24–32 ms, `clockrate` 47. **Slice 2c device PASS [`233323`](../../captures/session_20260816_233323.log)** — `stale_range` `dcnt` 4 / 5 / **1** / 4; cache notes rise; coverage `0–63`. Do not start 4e. Do not grain idle. Do not fold NOTE_EDIT hydrate into grooming — that is its own work path below. Do not optimize `LoadLoopJob` from PLAYING paint.

**FinalizeWorkspace slice** shipped (`48bd36f`). **LoopPersist finalize** relanded — boot **PASS** [`213246`](../../captures/session_20260816_213246.log); one PLAYING `persist_save` rem **206 ms** @ 25.683 s (later jobs no rem ≥ 50 ms).

**Parked:** 915 ms boot `load_frame` [`032803`](../../captures/session_20260816_032803.log) — `commitLoadLoopJobPublish` / `runDeferredLoadAndDisplayFrame`; not a freeze; not the revert.

**6A.1 HITL PASS** [`025651`](../../captures/session_20260816_025651.log) `match=1` `pmatch=1`. STOPPED `midi_gap` **72 ms** at LCR complete is a different sample (`6a,nat` 30 ms + `loop_rem,idle_maint` 58 ms); after that, max **13.5 ms**.

**Does not start:** Track A overlay, Stage 3b GUS replacement, interval reservation, RC-J patches, deleting `materializeToEventVector`. PLAYING `midi_gap` in [`192334`](../../captures/session_20260815_192334.log) was FinalizeWorkspace (sliced). Post-stop gap owner is idle visual cache, not LCR. NOTE_EDIT hydrate (below). Playback gather ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)).

### NOTE_EDIT hydrate (queued — own work path)

**Work identity:** [`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)  
**Architecture:** [`note_edit_selectedtick_lcr_resolution_architecture.md`](../Plans/note_edit_selectedtick_lcr_resolution_architecture.md) — DEC-037 amendment 2026-08-16; overlap retarget 2026-08-17; architecture **PASS**; stages **PASS WITH AMENDMENTS**.  
**Sibling:** [`overdub_participant_loop_content_architecture.md`](../Plans/overdub_participant_loop_content_architecture.md) §5 NOTE_EDIT sibling.

Overlap uses the **same participant query as overdub**, keyed by **selected/mover LinearSpan**, not a window around `selectedTick`. Select stays neighborhood navigation (`tickEvents` / `spanBoundaries` around `selectedTick`), not `resolveState`. Paint stays `visualCache` + `NoteEditCurrentState`. Stages 4a/4b/4c split.

Do **not** start firmware from “prepared LCR around `selectedTick`.” That mapping is withdrawn for overlap.

**Not** remaining `loop-content-resolution` 6.4 firmware. **Not** grooming Slice 5. **Not** `lazy-slot-hydration`. **Not** a resumable open-until-ready session. **Not** playback gather ([`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md)).

**6C/6D for this consumer:** 6C consume-when-ready **closed** [`205928`](../../captures/session_20260815_205928.log) / [`210508`](../../captures/session_20260815_210508.log) (3b stays). 6D.4 **HITL PASS** same captures; not all of LCR live.

Wrap-move persist is **parked** (current-structure issue) — it is not a start gate. Do not start firmware until this file is in § Now implementing. Do not full-replace `sessionMidiEvents()` for audition. Do not resume wrap-move persist patches from this path.

### Playback gather (Stage 1 hooks — in Now implementing)

**Work identity:** [`playback_gather_lcr_consume_enhancement.md`](../Plans/playback_gather_lcr_consume_enhancement.md) — DEC-037 amendment 2026-08-17. Stage 1 measurement hooks landed (accumulators + rare one-shots; no per-event serial). Stage 2–3 not started. 2-bar geometry unchanged. Does not start Owner-Boundary Gate / interval reservation. **Not** remaining OpenSpec 6.3 firmware. **Not** hydrate. **Not** LED lookup.

### DEC-036 Layer D 3b — overdub entry without display reconstruct (shipped)

**Plan:** [`loop_layer_d_overdub_rebuild_architecture.md`](../Plans/loop_layer_d_overdub_rebuild_architecture.md)

**D0:** **PASS** [`035414`](../captures/session_20260814_035414.log) — 6.78 s `begin_capture`; source-view problem, not overdub FSM.  
**Device FAIL [`042909`](../captures/session_20260814_042909.log):** undo **14.3 s / 14.6 s** (`VCACHE,full`); overdub **7.1 s**.  
**Device PASS [`045556`](../captures/session_20260814_045556.log):** `slice_clean` 1809 notes then overdub `begin_capture` **2214 µs**.  
**Device PASS [`112909`](../captures/session_20260814_112909.log):** undo **3 ms** (`MIDI: Undo` 143.169 → `Overdub undone` 143.172, `kind=3`); no `VCACHE,full`. Boot `load_frame` ~9.9 s remains D3/D4.  
**OpenSpec:** [`loop-effective-event-source`](../../openspec/changes/loop-effective-event-source/) closeout **4.1/4.2**. Successor: DEC-037 (post-commit rebuild / pass-list walks).

**Does not start:** Track A overlay, Stage 3b GUS replacement, interval reservation, RC-J patches.

### NOTE_EDIT on lengthened loop + overdub entry (shipped this session)

**Fix 1:** `openNoteEditSession` stops active overdub (`stopOverdubbing` → PLAYING) before rematerialize so live capture is committed and editable. **RC2:** always `rebuildVisualCacheFromPasses` on NOTE_EDIT open. Short-loop overdub-stop full rebuild removed in grooming Slice 4 — keep existing notes (`020910` / `021959` stale was adopt_partial).

**Fix 2:** Lengthened loop (4-bar loop, 2-bar content) — NOTE_EDIT select skips detailed-window note filter; bracket clamps to committed content span; select nav slots trimmed past content; nav length extends to painted note tail when overdub exceeds bar-aligned content. Fixture: [`015731`](../../captures/session_20260814_015731.log).

**Fix 3 (Stage 2) device PASS [`024428`](../../captures/session_20260814_024428.log):** after reboot, selected slot 5 undoes immediately — `Undo (entries=17)` kind=4 `NoteEditPassClosed`, then `Undo (entries=16)` kind=1 `OverdubPassAdded`. `rebuildSlotFromLoopContent` keeps the tip on the selected slot so later LoadLoopJob restores do not steal it ([`024004`](../../captures/session_20260814_024004.log) was the FAIL).

### Overdub lost on NOTE_EDIT exit — device PASS

**Device PASS [`025322`](../../captures/session_20260814_025322.log):** overdub → NOTE_EDIT (`visual_notes=102`, `session_events=234`) → pitch/range on note 194 → exit `NoteEditPass replaced … rows=1 saved=1` (not 19 Deletes). Commit flats stay 234. After later overdub, STOPPED `DISP` **103** — not the boot record-only **91**. Contrast [`014553`](../../captures/session_20260814_014553.log) `rows=19` / `DISP` 48→32.

### OLED first-frame mismatch — RC3 device PASS

**RC3 PASS [`014553`](../../captures/session_20260814_014553.log)** — dcache flush before SPI + STOPPED boot restore paints. Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

### Loop content-only history (DEC-035 Layer A) — archived

**Shipped + archived 2026-08-14.** Stage 3 + follow-up device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log). OpenSpec `openspec/changes/archive/2026-08-14-loop-content-history-persistence/`; normative `openspec/specs/loop-content-history/`. Do not start Stage 3b without new DEC.

Plan: [`loop_layer_history_persistence_architecture.md`](../Plans/loop_layer_history_persistence_architecture.md). Task [#33](https://github.com/Lytrix/MidiLooper/issues/33). Bug [#32](https://github.com/Lytrix/MidiLooper/issues/32) stall closed.

### Overdub-stop MIDI dump during PLAYING

LoadLoopJob PLAYING skip is device-proven in [`105505`](../../captures/session_20260813_105505.log). `UndoStacks` stall closed Stage 3 [`030147`](../../captures/session_20260814_030147.log). SlotMeta bundle on overdub stop closed follow-up [`032227`](../../captures/session_20260814_032227.log) — `LoopPersist` only ~139 ms. Scheduling: [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Do not patch RC-J.

### Real-time incremental work (RECORD/OVERDUB) — post–RC-C + S0 timing telemetry

**Now: S0e follow-through RC-K1–K3 shipped and device-verified.** Targeted fix of the overdub note-off cost attributed by S0e. Do not implement interval reservation, add extra `handleMidiInput()` call sites, patch RC-J, or chase overdub display frame-skip. Plan: [`realtime_incremental_work_overdub_note_change_bugfix.md`](../Plans/realtime_incremental_work_overdub_note_change_bugfix.md). Investigation [§31n](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31n-s0e--split-overdub-note-off-path-observation-only) / [§31o](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31o-s0e-follow-through--rc-k1--rc-k2--rc-k3). Evidence: [`204221`](../../captures/session_20260812_204221.log) (`notechg` 274 ms = `noterecon` 177 ms + `notepair` 98 ms).

**Baseline (proof of current stability):** [`191356`](../../captures/session_20260812_191356.log) on `752273d` (RC-H reverted). Multiple overdubs including overdub-over-overdub; display did not freeze (`slice_clean` covers the whole loop after every stop; final STOPPED paints the 16-bar window of a clean 1482-note cache). `clockrate` held **47–48** through PLAYING/OVERDUB (no dropped-clock / half-tempo). `midisvc` did **not** hold: 99–133 ms, then **218–221 ms** on the long overdub, 110–125 ms on later overdubs, against `clk`/`tracks` 4–10 ms. RECORD stays 0.3–0.8 ms. PLAYING↔OVERDUB `msi` spikes 237–433 ms. Post-stop clock drop is RC-J (48→24→0 and 48→36→0) — Owner-Boundary Gate, not S0b.

**S0e exit:** attributed in [`204221`](../../captures/session_20260812_204221.log) — `usbnote` is `notechg`; `noterecon` 177 ms is quadratic reconstruct dedup; `notepair` 98 ms is the per-candidate `noteIdHasChannel` scan. `noteappend` is 59 µs.

**S1 RC-K1 (shipped):** `reconstructNotesImpl` tracks seen `(note, startTick, endTick)` in an ordered set instead of `std::any_of` over the accepted list. Same key, same insertion order. Native `test_noteutils_reconstruct` PASS including many-identical-geometry collapse.

**S1 RC-K1b (shipped):** the ordered set allocated one PSRAM tree node per note. Boot visual-cache rebuild of 1430 notes measured 1.38 s ([`215128`](../../captures/session_20260812_215128.log) / [`215357`](../../captures/session_20260812_215357.log)). Dedup now ranks into one vector, `qsort`+unique, restore first-seen order. Native `test_noteutils_reconstruct` PASS. `teensy41-capture-serial` fits RAM1.

**S1 RC-K2 (shipped):** `accumulatePendingNoteChangesForIncomingNote` no longer scans source events for channel. Loop notes are loop-scoped (DEC-033). Native `test_pending_shorten_ignores_recorded_channel` PASS.

**S1 RC-K3 (shipped):** `overdubSourceViewNotes_` is reconstructed once in `establishOverdubSourceView` and cleared in `clearOverdubSourceView`. Note-off reads the member. Native `test_overdub_source_view` PASS.

**S1 RC-K1–K3 verified [`223033`](../../captures/session_20260812_223033.log):** the note-off gate is met — `noterecon` 0 in every window, `notechg` and `notepair` peak at 3.6 ms then hold 0.75–2.0 ms (were 274 / 98 ms).

**S1 RC-L1 (shipped, verified [`225803`](../../captures/session_20260812_225803.log)):** per-span `ProjectedIntervalVec` allocation in `projectDisplayNotes` / `projectNoteIntervals`. Four overdubs on a grown 64-bar loop: `begin_capture` 77 / 80 / 79 / 83 ms (was 1.378 s, and 58 → 410 ms as passes accumulated in [`223033`](../../captures/session_20260812_223033.log)). First USB note 108–242 ms after PLAYING→OVERDUBBING (was 1.76 s). `noterecon` 0; `notechg`/`notepair` 1.09 ms; `usbnote` 1.20 ms; overdub `clockrate` 47–48; overdub `midisvc` 4–14 ms. Remaining ~80 ms is the synchronous gather+reconstruct floor.

**Shipped (on `feature/overdub-participant-discovery`):** playback-observation overlap gates 0–4 + wrap-crossing consume `e1f57c5`. Plan: [`overdub_playback_observation_overlap_refinement.md`](../Plans/overdub_playback_observation_overlap_refinement.md). `PendingNote.overlapNoteIds` collection is wired. Note-off consumes the set via `appendNotesForIds` on `overdubSourceViewNotes_`, then geometry + `[S, E)`. Empty set is Add only. PLAYING idle prebuild reverted (`73f0489`). Option B stays withdrawn ([`021304`](../../captures/session_20260813_021304.log) 292 ms/note-off). Collection-wired overdub in [`154823`](../../captures/session_20260813_154823.log): `noterecon=0`, `notechg=2993`; 11.7 s post-stop stall is `LoopUndoHistory`, not this path.

**Gate 0:** `OverlapNoteIdSet` fixed capacity 128; overflow does not grow. Native PASS. Idle `stored_notes` measured in [`152940`](../../captures/session_20260813_152940.log): track 0 slot 4 (68 bars) `notes=1903 unique=1903 max_same_pitch=322`; track 6 `max_same_pitch=195`. Both exceed 128. Do not raise capacity without a decision. This diagnosis taxes the MIDI event runtime. Gate 0 count is recorded in [`152940`](../../captures/session_20260813_152940.log). Keep `maybeLogStoredNoteCount` as the total-notes / `max_same_pitch` inventory.

**Gate 1:** Production selection is normalized note geometry + `[S, E)` intersection (`existingNoteOverlapsIncomingHold`). `OverlapNoteIdObservation` is test/diagnostic only. Split-chunk and prior Shorten/Hide companion fixtures landed. 021304 same-pitch count still open on Gate 0.

**Gate 2 (native landed, device open):** enabled slots keep advancing playback; mute/solo/slot-mute suppress send on that track's MIDI channel and output ports (USB/DIN/USB Host). Track mute sends CC 123 on `midiChannel` only. Native proves cursor/wrap advance while MIDI send is suppressed, unmute does not resend crossed events, and mute does not clear the ledger. Device still owed: mute mid-note silences that channel on the output ports.

**Gate 3 (native landed):** empty candidate set does not look up spans and does not call `gatherCommittedEvents` / `reconstructDisplayNotes` on note-off. `OverlapCandidateLookup` copies from an already-available note list only. RC-K3 note-off still reads `overdubSourceViewNotes_`. `fullMaterializeCount` stays 0 after the work counter is reset, including when companion edit rows exist.

**Gate 4 (native landed):** `appendNotesForIds` is one pass over the available `DisplayNote` list and stops at the last matching id. It does not call `findLinearNoteSpanForNoteId` and does not build an index. 3714-note fixture with 3 ids examines 30 notes, not 3714×3.

**Hold-candidate collection (wired):** `PendingNote.overlapNoteIds` snapshots already-sounding same-pitch ids at incoming note-on and inserts playback note-on ids while the hold is open. Offs do not erase. Native: `test_overlap_hold_candidates`.

**Note-off consumes overlapNoteIds (wired):** `accumulatePendingNoteChangesForIncomingNote` looks up the set in `overdubSourceViewNotes_` (`appendNotesForIds`) and applies geometry + `[S, E)`. Empty set skips lookup (Add only; Gate 3). Native: `test_pending_note_change`.

**Wrap-crossing hold consume (head + tail, one hold) — shipped `e1f57c5`, HITL PASS [`155450`](../../captures/session_20260817_155450.log):** wrap-head `[0, E)` is the same incoming hold as the tail `[S, loopLength)`, not a second note-off. [`170449`](../../captures/session_20260813_170449.log) `hide=14` was the double-hold. NOTE_EDIT tick 64 is one 60 plus other pitches (`1/2`), not [`153213`](../../captures/session_20260817_153213.log) `1/3`. Plan: [`overdub_wrap_crossing_hold_head_consume_bugfix.md`](../Plans/overdub_wrap_crossing_hold_head_consume_bugfix.md).

**NOTE_EDIT mover wrap-length jump (RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log)):** no `2351` / `2975`. Mover **22** `DNTE` stays **95** while overlap runs on many neighbors. Note **14** `ChangeLength` `2256–2304` stays on the wrap-stub plan. Plan: [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md).

**NOTE_EDIT leave-restore painted span (RC1 native shipped; [`201948`](../../captures/session_20260813_201948.log) device):** 76 Hide/Restore is `840–863` (not 2160). Note **5** first-select is already `DNTE` **1535** — cache span is `720–2255`, not painted `720–767`. Stop on a second owner here. Mover **100** length stays **144**; 39.397 commit still saves only 14 `2256–2304` (wrap-stub plan). Plan: [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md).

**NOTE_EDIT note-off pairing LIFO (native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS):** @45.971 seals `canonical=7 pre_commit=7` — Delete 4/3/2/115, Length 1 `0–95`, mover 77 `96–788` pitch 60. Cache 22→18. [`213533`](../../captures/session_20260813_213533.log) @104.240 emitted the same five actions, then left lane 60 before deselect so the @108.711 commit carried only the mover rows. Plan: [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md).

**NOTE_EDIT edit-pass replay row payload (native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS):** `Length 115 48→287` @35.640 and `Length 115 1008→1103` @55.510 both seal and hold. Replay no longer rewrites a later row's span from an earlier row for the same `targetNoteId`. Plan: [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md).

**NOTE_EDIT overlap shorten commit seal (native shipped; device gate open):** the deselect commit dropped the overlap `Length` row because `participatingNoteVisibleOverlapTailInProgress` fired on a parked mover, so the shorten either vanished (display reverted) or landed one commit late against an unrelated focus ([`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` → @166.848 `Length 10 960–1247`). Skip removed from `buildCommitOverlapRowsFromCurrentState`; predicate stays in leave-restore. Slice B of the resolver contracts plan is **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was correct. Decisions: seal at the user-triggered commit; host tail stays truncated. Plan: [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md).

**NOTE_EDIT Length replay loop-boundary (native shipped; device gate open):** persisted `ChangeLength` 14 `2256–2304` was replayed as a wrap; same-pitch notes shortened to 2255. Owner: `applyChangeLengthById`. Plan: [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Do not fold note 5 cache pairing. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log); the wrap stub paints `DNTE,71,2256,…,48`.

**NOTE_EDIT wrap-stub commit (RC3 native shipped; device gate open):** RC2 device FAIL in [`193838`](../../captures/session_20260813_193838.log) / [`201948`](../../captures/session_20260813_201948.log) — 14 still `ChangeLength` `2256–2304` because cache has a non-zero span. RC3 skips loop-end Length unless painted end is `loopLength`. Plan: [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). Do not fold note 5 cache pairing.

**NOTE_EDIT / LOOP_EDIT display split (Stages 8–9 shipped; [`192007`](../../captures/session_20260813_192007.log) device):** 3-bar / 60-note loop (not 181114’s 64). First NOTE_EDIT `DISP` **59/60**. Hide/Restore of **45** is `1440–1511` (not 2160). **76** as overlap target still Hide/Restore `840–2160`. No MoveNote of 76; select at tick 840 rebuilds 76 (no 168 reset). `GEOM_APPLY,resolve` 16.8–52.3 ms. Paint gap and undo-warm stay open. Plan: [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md).

**Withdrawn-path cleanup (removed):** Option A slice tests, Option B windowed matrix, `gatherOverdubSourceView*InWindow`, and `gatherCommittedNoteEventsForPitch`. `maybeLogStoredNoteCount` kept.

**RC-L2 (withdrawn helper removed):** `gatherCommittedNoteEventsForPitch` is gone. Production note-off uses `overlapNoteIds` + `overdubSourceViewNotes_`. `test_overdub_source_view` now covers establish/clear only.

**RC-L3 (shipped, HITL verify open) — stored-MIDI verification off the stop path:** `emitStoredMidiVerification` ran inside MIDI button dispatch at overdub stop and cost 342.7 ms / 325.5 ms in [`013917`](../../captures/session_20260813_013917.log) (flush itself under 1 ms): a full `mergeActiveCapturePasses`, one `SEVT` line per note event over 3554 events, the capped wrap-pair scan, then a second full `reconstructNotes` for at most 32 `DNTE` lines. The ring overflowed at both stops, so most `SEVT` lines were dropped and the `seal` / `finalize` / `set_state` stage lines were evicted with them. Now `queueDeferredStoredMidiVerification` only sets a flag; `processDeferredStoredMidiVerification` drains from `processDeferredIdleMaintenance` in 64-event slices (16 while `hasDeferredSaveWork`), flattening once on the first slice and phasing notes → wrap pairs → display notes. Functions are `TRACK_COLD_MEM` — in ITCM they pushed RAM1 past a 32 KB block boundary.

**RC-L3b (shipped, device verify open) — one-shot + idle-only drain:** deferring during PLAYING still blocked MIDI for seconds because the first slice does a full merge ([`020631`](../../captures/session_20260813_020631.log) `msi` 2.95 s). Verification is now **one overdub stop per boot** (`#CAP,DIAG,stored_verify,armed,0` when disarmed; reboot to re-arm for another HITL evidence run). Drain runs only when transport is fully idle (same gate as REVT), not during PLAYING/OVERDUB. Second+ overdub stops in the same session skip verification so manual retests are not penalized.

**Architecture:** [`realtime_incremental_work_capture_overdub_architecture.md`](../Plans/realtime_incremental_work_capture_overdub_architecture.md)  
**Scheduling contract:** [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md)  
**Scheduling roadmap:** [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md) (O–T–R–C–A–P; interval reservation not authorized)  
**S0 (shipped code):** `RuntimeTimingTelemetry` — Tier-A `DIAG,midi_gap` / `midi_input` / `clk` / `tracks` / `clockrate` (5 s); observation only. Historical captures used `DIAG,msi` / `midisvc` for the same two measurements. Native `test_runtime_timing_telemetry` PASS. Playback gather Stage 1 adds `DIAG,late_on` / `late_off` / `late_clk` (5 s) plus `DIAG,late_event` / `DIAG,playback_build` one-shots — not a line per MIDI event. Do not start Stage 2/3 from this scheduling slice.  
**S0 device runs:** [`141815`](../../captures/session_20260812_141815.log), [`144323`](../../captures/session_20260812_144323.log) — DIAG timing lines lost across the whole capture pass; two root causes fixed (see [investigation §31a](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31a-s0-device-runs--first-results-2026-08-12)):
- **RC-S0a** `isTierATextLine` skipped two commas, so Tier-A classification was inert and the ring evicted every DIAG window. Parse extracted to `CaptureLineTier::isTierALine` (`test_capture_line_tier` PASS); Tier-A may now only be displaced by Tier-A.
- **RC-S0b** `MemoryMonitor::logStatus()` external-pool walk blocked the loop **593 ms** and lost external MIDI clock. Walk is now `setup()`-only (`logStatus(true)`); runtime reports `pool_size` (O(1)). Guide exemption removed.

**S0 run [`145555`](../../captures/session_20260812_145555.log):** DIAG timing windows survive through RECORD + both overdubs. `midisvc` 0.9–1.1 ms during RECORD vs **129–149 ms** during PLAYING/OVERDUB (762.5 ms at overdub entry), against `clk`/`tracks` ≤ 8.83 ms — `handleMidiInput()` duration is the dominant term and clock dispatch is not. **RC-S0c:** windows before 177 s were lost because `flushCaptureBuffer` never transmits Tier-A under the timing-critical budget; fix is a bounded Tier-A transmit allowance (roadmap O2 / [investigation §31b](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31b-s0-run-145555--first-capture-phase-envelope)).  
**Regression vs [`9678c3d`](../Plans/archive/refinements/runtime_scheduling_timing_envelope_investigation.md#31d-regression-vs-9678c3d--display-lag-and-transition-feel):** overdub display lag is the new `resolveDisplayNotesLiveCapture` budget bailout — measured resolve is 23–31 µs·10³ against a 5000 µs budget, so `reuseLastValidFrame` holds a stale frame for most of both overdub passes. Transition feel is the Clock dispatch reorder (Clock no longer transport-first).  
**Shipped (observation only):** compose sub-step instrumentation — `DisplayCommittedRebuildTime`, `DisplayCaptureReplaceTime`, `DisplayCaptureSyncTime`, plus the previously dead `DisplayCaptureGatherTime` / `DisplayCaptureFullGather` slots and branch counters `DisplayCommittedWindowFilter` / `DisplayCommittedFullAssign`. Native 1034/1034; `teensy41-capture-serial` builds.  
**S0 run [`152948`](../../captures/session_20260812_152948.log):** resolve attributed — `rebuildDisplayNotesInWindow` 25.7 ms × 22 calls (the spikes) and `filterDisplayNotesByWindowInclusion` 5.48 ms × 622 calls (the sustained over-budget). `replaceCaptureLayer` 0.10 ms, `synchronizeCaptureLayer` 0.01 ms — not factors. See §31e.  
**RC-D (new, outranks the above):** the OLED repaints every loop iteration — ~14 ms frame period against a 30 ms cadence, ~93 % of loop time in `DisplayManager::update()` from boot. `invalidateProjectedNoteEditDisplayCache` is raised by the general `invalidateLiveDisplayCache` path with no note-edit precondition, but `markNoteEditDisplayPainted` only clears it when `isNoteEditActive()`, so the ungated `maybeUpdateDisplayForNoteEditSelection` path latches on permanently.  
**RC-D fix (shipped):** `markNoteEditDisplayPainted()` now runs unconditionally at the end of the normal paint path in `DisplayManager::update`. Note-edit behaviour unchanged (the guard was already true there). Residual: the load/save overlay branch returns before the ack by design, so the latch can still spin while that overlay is open. Native 1034/1034; `teensy41-capture-serial` builds.  
**RC-D verified [`155132`](../../captures/session_20260812_155132.log):** 32.7 fps (was 70–78); display 45 % of loop time (was ~93 %); idle `msi` 15.3 ms (was ~34 ms); RECORD `midisvc` 604–905 µs. See §31f.
**Still open — dominant path (historical):** `midisvc` 126–146 ms through PLAYING/OVERDUB was later attributed S0b–S0e (`usbnote` / `notechg`) and reduced by RC-K1–K3.
**RC-E fix verified [`165636`](../../captures/session_20260812_165636.log):** `adopt_partial` reports partial coverage and idle slices backfill the whole loop in ~0.6 s (359 → 1 352 notes, then 327 → 1 417). Permanent starvation gone.
**RC-F fix shipped:** clean-cache overdub filter now gathers the paint window widened by `kWindowedGatherMarginBars = 2` and records it — the committed layer follows auto-follow instead of freezing. Verified in [`172405`](../../captures/session_20260812_172405.log): `wNotes` holds at 300–313 across the second overdub.
**RC-F follow-up shipped:** the first fix let the staleness predicate fire during the post-commit dirty window, where every rebuild took the 27.9 ms full-gather branch (30 gathers, 70 of 241 frames over budget, tails on 6 of 241) — that was the bar-boundary stutter and the "notes only appear after a bar". The resolve path no longer gathers: while `visualCacheDirty`, the committed layer is held and recovery is left to `processDeferredIdleMaintenance`; `committedWindowStale` requires a clean cache, and `committedLayerCleanCacheReady` rebuilds once when idle finishes. See §31g-3.
**RC-H reverted (RC-I):** the overdub-stop handoff preserve (`liveOverdubStopHandoffActive_`) pinned the partial adopted frame permanently — [`183429`](../../captures/session_20260812_183429.log): `adopt_partial notes=394`, then `slice_clean notes=1105`, but `DFRAME 394` held from 209.6 s through 214.2 s. The 174843 window content was density-correct; static viewport at STOPPED is expected. Reverted to pre-RC-H behaviour; `preservedHandoffAuthority` covers the ~0.6 s dirty window. **Verified [`191356`](../../captures/session_20260812_191356.log):** no freeze across two overdub clusters; idle `slice_clean` covers the loop; STOPPED paints the 16-bar window of the completed cache.
**RC-J (open, Owner-Boundary Gate G6):** post-stop persistence stall — `timingCriticalTrackActive` goes false at STOPPED while the external clock still streams. Reproduced in [`191356`](../../captures/session_20260812_191356.log): first stop `clockrate` 48→24→0 `msi` 467 ms; final stop 48→36→0 `msi` 365 ms. Do not patch ad hoc; do not widen `timingCriticalTrackActive`.
**RC-G fix shipped:** `updateOverviewCaptureDensity` builds a per-bar × 8-pitch-band mask incrementally from the capture preview (O(new notes) per frame), and `drawOverviewStrip` renders O(loop bars) from it when no clean `visualCache` exists. Record overview now shows the whole loop. Device verify pending for both.
**Remaining findings:** S0e attributed in [`204221`](../../captures/session_20260812_204221.log); RC-K1–K3 / RC-L1 device-verified in [`225803`](../../captures/session_20260812_225803.log). RC-S0c still owed (roadmap O2). RC-J is Owner-Boundary G6, not S0b. Display frame-skip / rolling-window during overdub is RC-F follow-up + investigation §31d.
**Overdub display lag:** `DisplayResolveLiveCapture` 33.6 / 33.8 ms during the second overdub against a 5 000 µs budget — the §31d bailout skips `replaceCaptureLayer` and the tails, so live overdub notes are not composed.
**Also open:** display resolve is under budget only by margin — the window filter measures 4 595 µs against a 5 000 µs line, and the gather still peaks at 26.3 ms. A 140 ms post-stop `msi` stall with `midisvc` at ~0.1 ms is uninvestigated.  
**RC-C device:** [`115913`](../../captures/session_20260812_115913.log) — timing PASS; display D1–D2 FAIL.  
**Regression:** [`122003`](../../captures/session_20260812_122003.log) — dual idle slice caused MIDI lag / clock lost (reverted).  
**Now:** Device re-measure after RC-K1b is closed on [`225803`](../../captures/session_20260812_225803.log). Remaining scheduling work is the owner-boundary roadmap, not interval reservation. Plan [`realtime_incremental_work_overdub_note_change_bugfix.md`](../Plans/realtime_incremental_work_overdub_note_change_bugfix.md).

### Long record onset display freeze — [`012342`](../../captures/session_20260812_012342.log)

**Plan:** [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md) — RC-A shipped; RC-C promoted via architecture doc above.  
**Evidence:** fail [`012342`](../../captures/session_20260812_012342.log); interim PASS [`013747`](../../captures/session_20260812_013747.log).

### Stage 5 — memory / persistence pressure

**5a (closed):** 5a-1/5a-2 shipped; 5a-3 **`pool_alloc` proof abandoned** (2026-08-12).  
**5b:** boot deferred save + clear gating — **parked**.

### Codebase consistency & maintainability — Phase 4 + LR complete

**GitHub:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) · **Plan:** [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)  
**Shipped:** TrackManager #22, NoteEditGeometryApply #23, DisplayNoteResolve #24, NoteEditFocus header #25, Phase LR #26. **Next:** close #18; pick next queue item (HITL CLI Phase 3 or persistence — see table below).

### StorageManager TU extraction — shipped (PR #17)

**GitHub:** [#16](https://github.com/Lytrix/MidiLooper/issues/16) — closed · [PR #17](https://github.com/Lytrix/MidiLooper/pull/17) merged to `dev`

### HITL CLI rebuild — Phase 3 (`base` + `edit_full` only)

**OpenSpec:** [`openspec/changes/hitl-cli-rebuild/`](../../openspec/changes/hitl-cli-rebuild/)  
**Plan:** [`docs/Plans/hitl_cli_rebuild_enhancement.md`](../Plans/hitl_cli_rebuild_enhancement.md)

| Phase | Status |
|-------|--------|
| 0–2 — foundation, scenarios | **Done** (2026-08-04) |
| 3 — layered **`base`** + **`edit_full`** | **In progress** — 3.2 bridge stabilized; next: 3.3 device PASS |
| 4–5 — corpus docs, archive | Pending |

**Layered presets (active):** `base` (`record_overdub`), `edit_full`. Do **not** wire `edit_minimal`, `revision_*`, `load_save_*`, `fader_motor_*`, etc. in this change.

**Phase 3 exit:** Mode B device PASS for `--layered --preset base` and `--layered --preset edit_full`.

### Persistence — pick one track (not all parallel)

**Guides:** [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) · **Handoffs:** [`set_revision_persistence_handoff.md`](../Plans/set_revision_persistence_handoff.md), [`continuous_runtime_persistence_phase5_recovery_handoff.md`](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md)  
**Work-queue baseline (shipped):** [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Track | OpenSpec / plan | Remaining | When to pick |
|-------|-----------------|-----------|--------------|
| **A — Overlay loop picker** | `set-revision-persistence` §4.8–4.10 | **Parked** — WIP stashed on `feature/set-revision-loop-picker` | User requests overlay milestone |
| **B — Crash recovery** | `continuous-runtime-persistence` Phase 5 | `.sealj` / slot **prefix load**, quarantine tail, native fixtures | After Layer D gate; orthogonal to overlay |
| **C — Admit API migration** | [#18](https://github.com/Lytrix/MidiLooper/issues/18) Phase 1.3 | `admitLoopSlotPersist` → `admitLoopPersist(LoopId)` at domain call sites | Hygiene with #18 closeout |
| **D — Layer D overdub rebuild** | DEC-035 Stages 6–7 / DEC-037 | **3b shipped** — successor [`loop_event_sourced_resolution_architecture.md`](../Plans/loop_event_sourced_resolution_architecture.md) | Active prototype |
| **E — Parked** | overlay hang, 3.9 failsafe | [`persistence_overlay_large_slot_focus_restore_bugfix.md`](../Plans/persistence_overlay_large_slot_focus_restore_bugfix.md); set-revision §3.9 | Investigation only |

**DeferredJobScheduler Phase B:** **Archived** [`2026-07-19-deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/). Specs: `deferred-job-scheduler/`, `lazy-slot-hydration`.

**DEC-020 note:** Phases 0–4 **shipped** on `dev`. Phase 5 was historically **paused** pending wrap-fix validation — that does not block overlay track A; confirm with user before starting B if wrap HITL is still open (see § Parked wrap investigation below).

---

## Recently shipped (2026-08)

| Slice | Decision / commit | Evidence |
|-------|-------------------|----------|
| Overdub session index reboot undo | DEC-038 amendment; `overdubSessionIndex` + `OSI1` | [`overdub_session_index_reboot_undo_bugfix.md`](../Plans/overdub_session_index_reboot_undo_bugfix.md); HITL [`003854`](../../captures/session_20260820_003854.log); native 1397/1397 |
| Overdub wrap source-view D2-D | Revision guard in `committedPlaybackNoteOnIdentityValid` | [`overdub_wrap_source_view_display_drops_committed_bugfix.md`](../Plans/overdub_wrap_source_view_display_drops_committed_bugfix.md); HITL [`174246`](../../captures/session_20260819_174246.log); native 1384/1384 |
| Occupy source-view Gate 5A + RC close | Equal-tick Off-before-On; sibling D2-D closes pin | [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](../Plans/overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md); HITL [`161349`](../../captures/session_20260819_161349.log), [`174246`](../../captures/session_20260819_174246.log) |
| Overdub pass overlap (G2) | DEC-031/032; archived `2026-08-12-overdub-pass-overlap-resolution` | Native 1016/1016; OLED PASS [`010000`](../../captures/session_20260812_010000.log); specs synced; merge PR pending |
| Live-record tick-0 NoteOn blip | `8de682c` | Native 999/999; device PASS [`182949`](../../captures/session_20260811_182949.log); pre-fix [`182528`](../../captures/session_20260811_182528.log) |
| Note edit current state | DEC-029; `3e9253e` | Native 969/969; [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md); HITL [`112202`](../../captures/session_20260808_112202.log), [`115120`](../../captures/session_20260808_115120.log), [`032118`](../../captures/session_20260808_032118.log) |
| StorageManager TU remaining trim | PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08) | [#16](https://github.com/Lytrix/MidiLooper/issues/16); [`storagemanager_translation_unit_extraction_refinement.md`](../Plans/storagemanager_translation_unit_extraction_refinement.md) |
| Codebase consistency Phase 4 — DisplayNoteResolve | PR [#24](https://github.com/Lytrix/MidiLooper/pull/24) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase LR — NoteMovementUtils shims | PR [#26](https://github.com/Lytrix/MidiLooper/pull/26) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — NoteEditFocus header | PR [#25](https://github.com/Lytrix/MidiLooper/pull/25) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — TrackManager TU | PR [#22](https://github.com/Lytrix/MidiLooper/pull/22) → `dev` (2026-08-08) | Native 969/969; manual [`225737`](../../captures/session_20260808_225737.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 1 — authority | PR [#19](https://github.com/Lytrix/MidiLooper/pull/19) merged to `dev` (2026-08-08) | Native 969/969; HITL [`163904`](../../captures/session_20260808_163904.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Playing move/length audition | `15c5750` | [`113626`](../../captures/session_20260808_113626.log), [`115120`](../../captures/session_20260808_115120.log); [bugfix doc](../Plans/note_edit_playing_move_audition_bugfix.md) |
| Resolver §12 orthogonal state | DEC-030; `7af8671` | Native 969/969; HITL [`112202`](../../captures/session_20260808_112202.log) @88.669 |
| Edit-session-action-geometry archive | 2026-08-05 | [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| Docs folder hygiene Phase 3 | 2026-08-08 | Root doc roles; `PROJECT_INTENT` redirect; `FEATURES` / `FEATURE_PLANS` banners |
| Docs folder hygiene Phase 2d | 2026-08-08 | 9 `Refinements/` logs → `Plans/archive/refinements/` |
| Docs folder hygiene Phase 2c | 2026-08-08 | 2 FROZEN `*_bugfix.md` → `Plans/archive/bugfix/` |
| Docs folder hygiene Phase 2b | 2026-08-08 | 10 `*_handoff.md` → `Plans/archive/handoff/` |
| Docs folder hygiene Phase 2a | 2026-08-08 | 43 `*.plan.md` → `Plans/archive/cursor-exports/`; 4 retained at root |
| Docs folder hygiene Phase 1.5 | 2026-08-08 | PascalCase `docs/` folders (`Authority/`, `Plans/`, `Runtime/`, `Agents/`, `Templates/`) |
| Docs folder hygiene Phase 1 | 2026-08-08 | Four-bucket `docs/README.md`; slim `Plans/README.md`; `HITL_ARCHITECTURE` → `Authority/Architecture/` — [`docs_folder_hygiene_refinement.md`](../Plans/docs_folder_hygiene_refinement.md) |

Normative specs: `note-edit-current-state`, `note-edit-modification-session`, `note-edit-session-undo`, `internal-heap-external-memory-routing`, `edit-session-action-geometry`.

---

## Parked / closed (queue references)

### Edit HITL from scratch — parked

**Plan:** [`docs/Plans/m8_edit_note_edit_hitl_automation_refinement.md`](../Plans/m8_edit_note_edit_hitl_automation_refinement.md)

Deferred from **edit-session-action-geometry** Phase 5 (D14 full matrix). Interim smoke: `edit_minimal` preset only.

### Firmware ownership / lifetime review — closed (2026-08-06)

**Plan:** [`docs/Plans/firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md) — P0/P1 **done**; Phase 5 recovery and layered **`base`** HITL **parked**.

### Note edit undo after reboot — done

**Plan:** [`.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md`](../../.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md) — HITL **PASS** [`session_20260805_212234`](../../captures/session_20260805_212234.log).

### Hygiene — codebase debt review — complete

**Plan:** [`docs/Plans/codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md) — safe hygiene **done** on `chore/codebase-hygiene-sprint1`.

### Note edit control-surface split — complete

**Plan:** [`docs/Plans/note_edit_control_surface_split_refinement.md`](../Plans/note_edit_control_surface_split_refinement.md) — Phases 0–8 on `dev`.

---

### OpenSpec — slot-performance-interaction Phase −1 (2026-07-19)

Rename-only merge-cache vocabulary (no behavior change). Remaining phases stay queued.

| Item | Status |
|------|--------|
| −1.1–−1.4 rename struct/field/APIs | Done |
| −1.5 native tests | Done (663) |

### Recently closed — Deferred job scheduler (Phase B)

**Archived:** `2026-07-19-deferred-job-scheduler`  
**Specs synced:** `deferred-job-scheduler` (new), `lazy-slot-hydration` (gate note).

| Step | Status |
|------|--------|
| B.1 thin `runFrame` | PASS [`231510`](../../captures/session_20260718_231510.log) |
| B.2 `stepSubmittedLoadJobs` | Done |
| B.3 select then step + `LoadLoopSelectionPolicy` | Done |
| B.4 native + device | PASS [`022107`](../../captures/session_20260719_022107.log) |

### Prioritized boot load isolation (merged to `dev`)

Shipped via PR #4 on `feature/memory-pressure-reclaim`.

---

### Recently closed — Unified commit lazy slot load (Phase A)

**Archived:** `2026-07-18-unified-commit-lazy-slot-load`  
**Specs synced:** `lazy-slot-hydration`, `loop-commit-semantics`, `multi-loop-slots`, `playback-runtime-prewarm`, `timeline-passes`.

| Item | Status |
|------|--------|
| A.6 parse split + A.7 PSRAM headroom fix | **PASS** [`230145`](../../captures/session_20260718_230145.log) |
| 6.2-device interactive | **PASS** [`224607`](../../captures/session_20260718_224607.log) |
| Parked | 6.1 DERIVED_READY; 6.3 undo/import Commit docs |

---

### Recently closed — Slot queue LOOP_EDIT depart

**Plan:** [`docs/Plans/slot_queue_loop_edit_depart_bugfix.md`](../Plans/slot_queue_loop_edit_depart_bugfix.md)

| Item | Status |
|------|--------|
| No mid-play geometry write on LOOP_EDIT depart | **PASS** |
| Device gate (queue slot, LoopEnd commit, no hang) | **PASS** [`004331`](../../captures/session_20260718_004331.log), reconfirmed [`010126`](../../captures/session_20260718_010126.log) |
| Queued-launch musical-time countdown | **In tree** — OLED field; LoopEnd commits present in `010126` |

---

### Paused — Memory pressure reclaim (M6 follow-on)

**Branch:** `feature/memory-pressure-reclaim`  
**Plan:** [`docs/Plans/memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md)

| Phase | Scope | Status |
|-------|--------|--------|
| **1A** | Advisory FSM + `#CAP,DIAG,pressure` | **Shipped** (`f09f547`) — validated in [`session_20260714_233620.log`](../../captures/session_20260714_233620.log) |
| **1B** | Low reclaim (`try*` owner APIs; background-first) | **Shipped** — pending manual gate |
| 2 | Critical undo trim + persistence inversion | Paused |
| 3 | Replace scattered heap thresholds | Paused |
| 4 | Manual gate 215312; idle defer; OpenSpec archive | Paused |

**Manual gate (1B):** Re-run stress session; confirm reclaim under Low without playback glitches / dropped MIDI on selected track.

---

## Recently landed (M6 Ph 1–2 — merged to `dev`)

**Commits:** `50e0f6e`…`11025ca` on `dev`

| Item | Status |
|------|--------|
| Phase A metrics | Shipped |
| Phase 1 playback window (DEC-016 chunk merge) | Shipped |
| Phase 2 display stale-while-revalidate | Shipped |
| Record-start headroom + live-record display | Shipped |
| Native | **608/608** |
| Manual | **PASS** no crash — [`session_20260714_215312.log`](../../captures/session_20260714_215312.log) (64-bar, 7-track overdub; 42 append failures remain) |

**Plan:** [`multi_track_playback_pressure_closure_refinement.md`](../Plans/multi_track_playback_pressure_closure_refinement.md)

---

## Recently landed (persistence work queue — on `dev`)

**Branch:** `feature/persistence-work-queue` — [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Phase | Status |
|-------|--------|
| B1–B4 | Work queue admission, scheduler, monolith retire |
| B4 follow-up | Sync-drain budget, clear-slot SD-restore, urgent save / mid-pass defer |
| B5 | **604/604** native; HITL **PASS** [`host_midi_automation_serial_20260714_153240.log`](../../captures/host_midi_automation_serial_20260714_153240.log) |

---

## Recently landed on `dev` (pre-queue)

### Bugfix: record-stop MIDI flood — **restore baseline playback, pending HITL**

**Evidence:** [`session_20260713_165557.log`](../../captures/session_20260713_165557.log) — 571 note-ons over 5 pitches in 704 ticks; BPM collapse; `RING,overflow`.

**Fix:** Restored `901c4d9` playback send/anchor (`reanchorPlaybackProjection`, inline `atLoopStart`, `loop.midiEvents()` window). Kept display storage-frame playhead + persistence grace.

**Plan:** [`docs/Plans/record_stop_playback_hang_bugfix.md`](../Plans/record_stop_playback_hang_bugfix.md) (phase 4c)

| Gate | Status |
|------|--------|
| Native | **562/562** |
| `teensy41-capture-serial` build | **SUCCESS** (2026-07-13) |
| HITL / manual | **User** — record→stop→PLAYING; no MO flood; playhead bar 0 |

---

### Bugfix: record-stop coordinate frame (phase 4) — **superseded by 4c flood fix**

---

### Bugfix: overdub wrap note-off pairing — **implemented, pending HITL**

**Follow-up to capture ownership refactor** ([`overdub_wrap_note_off_pairing_bugfix.md`](../Plans/overdub_wrap_note_off_pairing_bugfix.md)). Fixes wrong on/off pairing when notes are held across loop wrap.

**Changes:** `capturePhaseTick` / `appendCaptureNoteOffAtPhase`; finalize clears pending only after append; canonical `wrappedHeadOff`; stop diagnostics; 13 native tests in `test_capture_note_off_rules`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL | **User** — 132536 scenario: tail wrap pairs tail on before wrap, not N@0 |

---

### Bugfix: overdub wrap note-off capture ownership — **implemented, pending HITL**

**Scope:** Single close pipeline for open notes at record/overdub stop; playback read-only (no mid-wrap capture mutation). Aligns live capture with `buildCanonicalSpansFromMidi` wrapped linear storage.

**Changes:**
- Removed `closeOpenNotesAtLoopWrap`, `flushPendingNotesIntoCapture`, `removeCaptureNoteOffAt`
- `finalizePendingNotes(currentTick)` on all record/overdub stop paths (playhead close, not L-1)
- `Loop::sealCapture` passes playhead `openTailCloseTick` to `finalizeWrapWindowOnStore`
- Removed `recordMidiEvents` note-off repair (tick bump, L-1 removal)
- New native suite `test_capture_note_off_rules` (7 tests)

**Plan:** [`docs/Plans/overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md)

**Deferred:** playback wrap `double_on` ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log)) — separate plan; do not touch `playMidiEventsForSlot` / shared `projectionCycleStartTick`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + capture | **User** — re-run 125437 scenario: balanced SEVT per pitch, no ch4 ghost offs, no lag regression |

---

### Bugfix: loop-wrap playback lag regression — REVERTED to `bc98491` (shipped `901c4d9`)

**Decision:** `19aa47a` ("Fix loop-wrap playback retrigger") and both follow-up wrap-tail rewrites (full-order scan; cursor drain) are **reverted**. `Track::playMidiEvents`, `Track::playMidiEventsForSlot`, `IntervalProjection` (`shouldPlaybackEmitWrapTailEvent` / `shouldPlaybackCrossEvent` helpers), and their tests are restored to `bc98491` — the state the user confirmed "worked perfectly." `closeOpenNotesAtLoopWrap` on overdub playback wrap is restored.

**Proof the wrap fix regressed playback (not the active track):** during the active track's overdub, background **slots** flooded ~1 MO per main-loop frame via `playMidiEventsForSlot`, starving the loop and freezing display ≈2 s ("lag around loop wrap"). MO by channel — good [`session_20260713_115434.log`](../../captures/session_20260713_115434.log) vs bad [`session_20260713_122107.log`](../../captures/session_20260713_122107.log): ch5 (active) 212→223 unchanged; ch4 (slot) 486→**3620**; ch4 ~1 ms-gap events 278→**3422**. `19aa47a` introduced it (ch4 486→2488); cursor drain worsened it (→3622).

**Deferred (not reintroduced yet):** original wrap **double-on** ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log) — MO `double_on` at BAR, 54-bar loop). Any future fix must NOT touch per-frame slot playback / the shared one-per-track `projectionCycleStartTick`.

**Out of scope:** slot/bar queued seek `sendAllNotesOff`; unify pending-note flush at record/overdub stop; stop FSM changes.

| Gate | Status |
|------|--------|
| Native | **542/543** (known empty `test_capture_note_off_rules` suite) |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + stop | **User** — compare to 115434: no per-frame slot MO flood, no BAR bunching, no ~120 ms post-stop DISP lag |

---

### OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/) (DEC-020)

**Branch:** `dev` — Phases 0–4 shipped; **Phase 5 not started**

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** — native + **HITL passed** [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) (64+64); boot restore [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| **5** Recovery | **Not started** — longest valid prefix load + quarantine tail — [**handoff**](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) — **CURRENT_WORK pick track B** |
| **6** Full 64+64 HITL | **Mostly evidenced** (same log) — archive checklist + `oldestDirtyChunkAge` review remain |

| Gate | Owner | Status |
|------|-------|--------|
| Native | Agent | `pio test -e native` — **969/969** (2026-08-10) |
| `test_storage_loop_io` | Agent | Run before Phase 4 archive sign-off |
| Phase 4 HITL (64+64 record/overdub) | **User** | **PASS** — [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) |
| Phase 4 HITL (cold-boot restore) | **User** | **PASS** — [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| Phase 5 native fixture | Agent | Prefix load from truncated `.sealj` / slot — **not started** |

**HITL policy:** stop/crash scenarios — user captures serial manually (`capture_session.py`); agent does not loop HITL. On crash, user bisects and shares log before stop-path patches.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| **Phase 5 handoff** | [continuous_runtime_persistence_phase5_recovery_handoff.md](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

---

## Landed on branch (recovery merge — no further agent work unless HITL fails)

| Area | Commit / note | HITL |
|------|----------------|------|
| NOTE_EDIT move/pitch bracket | `736fa33` | User: `session_20260709_155307` replay |
| Split-focus slot switching | `0f086f4` (DEC-025) | User: preview slot + loop-end commit |
| Slot short-press / deferred restore | `f6b496a` | User |
| Playhead + undo geometry | `346f7ce` | User: `session_20260708_233241` |
| Loop-scoped undo (DEC-024 Ph 1) | `b1d3259` | Native only |
| Boot load + slot scan | `50ad01b` | User: cold boot ×5 |
| Slot clear + arm state | `2a565aa` | User |

---

## Parked

### Later — bound deferred wrap-append ([`183229`](../../captures/session_20260821_183229.log))

**Owner:** `Loop::applyDeferredVisualCacheWrapAppend`  
**Evidence:** `idle_append=1122618` at 44.5 s. Stage C moved wrap-append off the undo/switch turn; the later turn is still unbounded.  
**Plan:** [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §37. Do not start until a new slice is opened.

### Later — attribute 5 s STOPPED idle ([`183229`](../../captures/session_20260821_183229.log))

**Owner:** unknown until attributed.  
**Evidence:** `idle_maint=5028348` at 49.7 s between track 6 and 7; no `idle_gather` / `idle_append` child. Investigation first.  
**Plan:** §37. Do not start until a new slice is opened.

### Tagged for investigation — occupy id resolves to no source-view note (`norow`) — superseded

**Superseded by:** [`overdub_consume_id_resolution_completeness_bugfix.md`](../Plans/overdub_consume_id_resolution_completeness_bugfix.md) Stage 1 — hold fill before id lookup. Native fixture `test_consume_id_resolution_norow_repaired_by_hold_fill`. HITL gate open post-flash.

### OpenSpec: [`unified-capture-commit-owner`](../../openspec/changes/unified-capture-commit-owner/) (DEC-023)

Phases 1–3 prototype **reverted** at `40db4df` (boot bisect). Storage boot recovery helpers (`37f6b00`) landed. Retry Track/Loop slices only after stable boot + user approval.

### Derived note overlap (`edit-session-action-geometry`) — **archived 2026-08-05**

Pipeline shipped; normative [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/). Phase 5 HITL matrix **parked** — see Edit HITL plan above. Historical handoff: [`derived_note_overlap_logic_handoff.md`](../Plans/derived_note_overlap_logic_handoff.md).

### Prior: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

M6 Ph 1–2 on `dev`; pressure reclaim + Ph 3–4 in [`memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md).

---

## Do not start without decision

- DEC-023 capture-commit Track slices (after Phase 5 or explicit user go)
- `currentset-savedset-storage-layout`
- D13 jam-recording (ROADMAP — post JamRecorder)
