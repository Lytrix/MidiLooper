# Project state (execution context)

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-09-02 (010217 stop gather linear ends)

---

## Current branch

**`feature/overdub-wrap-snapshot-publication`** — 3.5 wrap-journal publication
forked from `feature/immutable-loop-snapshot-passes`. Parent:
**`feature/immutable-loop-snapshot-passes`** — forked from
`546169f66658b87c54e6166b79eba74628aa3d1e` for the immutable loop snapshot
architecture proposal. The maintained-LCR continuation from
`feature/overdub-stop-lag-stage-c` is not pursued on this branch. See
[`CURRENT_WORK.md`](CURRENT_WORK.md). Record and overdub `LoopSnapshot`
publication queue at stop and step from idle (`DEC-045`) in the current
firmware. Task `3.5` now queues bounded wrap journals and steps them during
PLAYING/OVERDUBBING after the first root (`DEC-047`). Playback still reads a
stale published window plus unpublished session passes until consumer
migration. OVERDUBBING idle does not run visual reconstruct. NOTE_EDIT close
stays synchronous.
Device PASS [`233308`](../../captures/session_20260825_233308.log): no
`snapshot_idle` after first publish until STOPPED; PLAY4 BPM 119.966–120.049.
Capture-only `snapshot_idle` telemetry exposed the root/queue boundary, and
device capture [`232633`](../../captures/session_20260826_232633.log) verified
that the PLAYING/OVERDUBBING rows before root publication were one in-flight
record construction with no queued successor passes. The final `pending=0`
row published the root; no later PLAYING/OVERDUBBING idle rows occurred.
4b.2c is closed without amending `DEC-045`.
The activated next slice is OpenSpec tasks `3.5`/`3.6`, recorded in
`DEC-047`: bounded overdub-wrap mutation journals, fixed-capacity immutable
membership pages, persistent bar cursors, incremental pair-`NoteId` assignment,
effective event-index updates, retry retention, and the measured
`T_RT_reserve` device gate. This phase supersedes `DEC-045` only for ordered
successor construction during `PLAYING`/`OVERDUBBING` after the first root;
direct consumer migration, persistence, and snapshot-identity undo remain
later. Firmware edits are not yet authorized until the phase gate and
execution-bound admission pass.
Latest `teensy41-capture-serial` build restored RAM1 locals to **7008**
without USB-host ownership changes, so task `3.6` recapture is unblocked.
The follow-up admission fix allows queued session-exit snapshot work after
wrap-journal drain and permits the bounded Snapshot owner while tracks are
stopped but the transport clock remains running. Focused native coverage
passes; full native and device recapture remain open. Construction now
persists membership-copy, mutation, append, and event-sort progress at one
fixed-capacity membership page per admitted unit. The capture-serial build
remains at **7008** RAM1 locals; device `T_RT_reserve` recapture remains open.
Capture [`202635`](../../captures/session_20260827_202635.log) confirms journal
drain and eventual `pending=0`, but still records 79.169 ms and 74.453 ms
slices from whole-pass session-exit start. That start now uses a persistent
`CommittedEventRange` cursor and the existing journal caps so one admitted
unit builds at most one bounded pass-change fragment. Capture
[`204136`](../../captures/session_20260827_204136.log) published `root_rev=8`
when wrap journals drained while session-exit passes were still queued;
playback then dropped the first record pass. Wrap completion now waits for
queued passes, construction progress, and companion changes before
`storePublishedSnapshot`. Stage 1 STOPPED admission is closed on
[`221054`](../../captures/session_20260827_221054.log): Snapshot idle ran
after stop, the candidate stayed private, and publication occurred only at
`pending=0` / `root=0x70400ac8` / `root_rev=10`.
Stage 2 child timing is in tree on `snapshot_idle`
(`start`, `pass_change`, `step`, `finish`, `publish`, `probe`, `enqueue`,
`discard`, `emit`). Capture-serial RAM1 locals **6976**; native **1491/1491**.
Capture [`225659`](../../captures/session_20260827_225659.log) names the
224842 record-stop leftover as `discardPendingIfLoopLengthChanged`
(`discard=15262` on the 20.984 ms PLAYING idle). Exclusive leftover after
that probe is ≤ 39 µs. STOPPED session-exit `start`/`pass_change` reaches
19.996 ms. Publication closed at `pending=0` / `root=0x70431844` /
`root_rev=8`. The Stage 2 falsification review is recorded in
[`snapshot_publication_timing_bugfix.md`](../Plans/snapshot_publication_timing_bugfix.md).
Session-exit length handoff device PASS
[`003015`](../../captures/session_20260828_003015.log): record-stop
PLAYING `discard=47`, first root `pending=0` / `root=0x703248cc` /
`root_rev=1`. Non-fit discard remains a content-sized destructor. Device recapture [`004119`](../../captures/session_20260828_004119.log)
names the 21 ms session-exit as `note_lookup` (86.9% of
`pass_change=21050`). Caller-scoped pitch lookup device PASS
[`005527`](../../captures/session_20260828_005527.log): `note_lookup`
max 857 µs, `pass_change` max 3.219 ms.
Wrap-tick derived reconstruction is removed from the wrap clock interval
([`overdub_wrap_derived_rebuild_clock_interval_bugfix.md`](../Plans/overdub_wrap_derived_rebuild_clock_interval_bugfix.md)).
Musical wrap stays on the clock. Rank 6 next-tick full-loop compatibility
gather is replaced by incremental overdub-seal admission when the one-shot
receipt proves completeness
([`playback_merged_compatibility_rebuild_clock_interval_bugfix.md`](../Plans/playback_merged_compatibility_rebuild_clock_interval_bugfix.md)).
`DEC-049` preserves that merged stream across `stopOverdubbing` so the
same incremental admission can run after stop, and admits Snapshot on 2
of every 6 deferred turns while a publication backlog is present. Device
Stage 2 PASS [`152513`](../../captures/session_20260831_152513.log);
Stage 3 drain PASS [`153113`](../../captures/session_20260831_153113.log)
(`root_rev=7` meets `playback_rev=7`). Live-record snapshot start walks
`capture.store` from a persisted index
([`live_record_snapshot_start_bounded_fragment_bugfix.md`](../Plans/live_record_snapshot_start_bounded_fragment_bugfix.md)).
Device [`190654`](../../captures/session_20260831_190654.log) closed the
copy/scan class; remaining `start` growth was `note_lookup`. That path now
skips `findNoteAtPitch` (`openNotes` only)
([`live_record_snapshot_start_skip_parent_note_lookup_bugfix.md`](../Plans/live_record_snapshot_start_skip_parent_note_lookup_bugfix.md)).
Device [`192422`](../../captures/session_20260831_192422.log) closed the
lookup class: `note_lookup=0` on all 32 live-record starts; after-first
`start` 167–233 µs. Remaining `idle_maint` spikes are display VisualCache
(wrap-stale + `from=passes`). [#41](https://github.com/Lytrix/MidiLooper/issues/41)
Stage 1 device PASS
[`202745`](../../captures/session_20260831_202745.log)
([`display_visual_cache_snapshot_reuse_refinement.md`](../Plans/display_visual_cache_snapshot_reuse_refinement.md)):
no wrap `dsz=0` while notes exist; no wrap `from=passes`; first overdub pass
stays on the paint frame (`183,111` → `184,180`; second wrap `249,246`).
Stage 1b device FAIL [`204132`](../../captures/session_20260831_204132.log):
STOPPED DNTE is short; NOTE_EDIT open rebuild then 291 notes / `presence=0`.
Stage 1c device FAIL [`205138`](../../captures/session_20260831_205138.log)
wrap 2 `205→271` then slice `260`. Stage 1d shipped `d35d64a7`. Device FAIL
[`205917`](../../captures/session_20260831_205917.log): idle Overlay fill
left longs after wrap 2. Stage 1e (in tree): idle rejects Overlay and
fills from current committed
([`display_visual_cache_idle_slice_current_fill_bugfix.md`](../Plans/display_visual_cache_idle_slice_current_fill_bugfix.md)).
Native **1518/1518**. RAM1 locals **6944**. Device recapture required.
Stage 1f (in tree): last-resort STOPPED paint rejects Overlay
([`display_visual_cache_stopped_paint_current_fill_bugfix.md`](../Plans/display_visual_cache_stopped_paint_current_fill_bugfix.md)).
Device gate [`213448`](../../captures/session_20260831_213448.log): first
STOPPED `DISP` must equal visualCache. Stage 1g (in tree): wrap-held
spans dirty On/Off bars only
([`display_visual_cache_wrap_dirty_span_bugfix.md`](../Plans/display_visual_cache_wrap_dirty_span_bugfix.md)).
Device [`221925`](../../captures/session_20260831_221925.log): wrap `dcnt`
inconclusive on a 4-bar all-bar overdub; wrap 3 idle did not fill; STOPPED
**128 / 137** with `DNTE` 39 **144+256**.
Device [`222349`](../../captures/session_20260831_222349.log): wrap first
`DISP` drops then `slice_clean` restores; first STOPPED **159 / 31** then
`OverlayRejected` **159 / 166**; B3 held across stop; no per-note dump.
Stage 1h shipped `4238b422`: `Off@0` does not linear-steal a mid-loop On
([`display_visual_cache_held_note_wrap_pair_bugfix.md`](../Plans/display_visual_cache_held_note_wrap_pair_bugfix.md)).
Stage 1i (native **1525/1525**, RAM1 **6944**): wrap/stop first paint
holds dirty cache
([`display_visual_cache_wrap_stop_first_paint_bugfix.md`](../Plans/display_visual_cache_wrap_stop_first_paint_bugfix.md)).
Stage 1j (native **1526/1526**, RAM1 **6944**): playhead tails are
paint-only
([`display_visual_cache_stop_handoff_playhead_tail_bugfix.md`](../Plans/display_visual_cache_stop_handoff_playhead_tail_bugfix.md)).
Device [`232910`](../../captures/session_20260831_232910.log): 1k wraps
2–8 hold; wrap 1 `104→92` after idle `append_resume` 89 is the grow-vs-prefix
hole (fix: compare to composed hold). Dump `reconstruct` 327 / `frame` 412.
First STOPPED **412 / 208**. 1k/1m device PASS [`002145`](../../captures/session_20260901_002145.log).
Occupy same-start hide is in tree
([`overdub_occupy_same_start_shorter_hides_longer_bugfix.md`](../Plans/overdub_occupy_same_start_shorter_hides_longer_bugfix.md)):
reconstruct keeps both `noteId`s; display adopt/idle keep the shortest end.
Native **1533/1533**. RAM1 locals **6848**. Device recapture required
(`002145` `norow` 75/76; pitch 63 frame 96+288). 1l not in that log.
([`display_visual_cache_wrap_compose_hold_bugfix.md`](../Plans/display_visual_cache_wrap_compose_hold_bugfix.md),
[`display_visual_cache_off_zero_tail_pair_bugfix.md`](../Plans/display_visual_cache_off_zero_tail_pair_bugfix.md)).
Stage 2 is unblocked and not started.
Do not freeze `T_RT_reserve`.
Do not check off OpenSpec `3.5`/`3.6`.
No `step` rewrite is justified yet; and freezing the minimum observed
`T_RT_reserve` is rejected as a guarantee. Construction bounds stay
unchanged.
For open 4b.3 attribution, `TrackManager::updateAllTracks` now emits
capture-only per-track `track_update` maxima and over-budget counts alongside
the aggregate `tracks` timing; device recapture is required.
Display paint fallback and visual-cache fill read published snapshot events
when a root exists; compatibility gather remains the no-snapshot fallback.
A nonempty dirty visual-cache prefix does not authorize the paint window
([`235954`](../../captures/session_20260825_235954.log)). Live overdub keeps
the PLAYING committed handoff only while the paint window stays inside
the last gather ([`001048`](../../captures/session_20260826_001048.log),
[`001907`](../../captures/session_20260826_001907.log)). First gather
includes a short loop tail so a 20-bar loop does not omit the last 2 bars
([`002634`](../../captures/session_20260826_002634.log)). Overdub-stop paint
holds the composed frame — device PASS [`004934`](../../captures/session_20260826_004934.log)
(`ODUB,stop,display` 8.8 / 12.2 ms). 4b.0 wrap/hold device PASS
[`092755`](../../captures/session_20260826_092755.log)
(`ODUB,stop,display` 21.4 / **11.1 ms** after wrap). **Now:** prepare
decomposition D1 **device PASS**
[`221655`](../../captures/session_20260826_221655.log) after 1A measure FAIL.
Next is D2 design session on
[`playback_window_prepare_algorithm_decomposition_investigation.md`](../Plans/playback_window_prepare_algorithm_decomposition_investigation.md)
([#38](https://github.com/Lytrix/MidiLooper/issues/38)). Ring B/RC2 is
**closed.** Residual **4b.1b** lives on
[`playback_window_build_owner_boundary_refinement.md`](../Plans/playback_window_build_owner_boundary_refinement.md)
§ *4b.1b* and is parked. Ring A firmware is not authorized. Ring B/RC2 shipped
the five-owner cursor (snapshot, visual-cache, wrap-append, persistence,
load/display) and RC2 coverage: refuse uncovered visual-cache reads; fall back
to committed-pass reconstruction. Device PASS
[`183452`](../../captures/session_20260826_183452.log) / reconfirm
[`185141`](../../captures/session_20260826_185141.log): wrap `from=passes`,
`DISP` 286→466 / 271→437. First-overdub `midi_gap` 222.596→146.802 ms vs
[`175350`](../../captures/session_20260826_175350.log); later steady 24.325 ms
vs 366 ms. Reconstruct timings stay on the owner-boundary plan.
OpenSpec `4b.2` first-take length updates during RECORDING remain in tree;
live-record snapshot append starts now admit at most one completed bar of
new range per start so setup cost stays smaller in realtime windows;
first-take display keeps continuous playhead length (host implement; device
verify open). Ring B now owns the five-way deferred idle turn; `4b.3`
post-stop `usbdisp` /
`load_frame` / `late_on` is now in tree (device verify open);
`4b.4` overdub `DisplayResolveLiveCapture` owner refinement is now in tree
(device verify open). `DEC-046` accepts the round-robin admission and
representation-coverage direction. Ring B/RC2 firmware is **closed**.
RC4 repair and Ring A remain separate.
Slice 1 telemetry is in tree and device-validated by
[`161834`](../../captures/session_20260826_161834.log) and
[`162522`](../../captures/session_20260826_162522.log): the dominant
long-loop rebuild destination is `T0/S6`, with `T4/S0`, `T6/S0`, and `T2/S2`
also observed; `162522` directly pairs the repeated compatibility gathers with
`T0/S6`. The playback rebuild `<count>` is rebuild-call count before one-shot
emission, not distinct-destination multiplicity or an eligible-set count.
RC4 question 3 is closed from code: `adoptPersistedSnapshot` /
`restorePassesSnapshot` do not publish a `LoopSnapshot`. Question 1 is
closed by [`165024`](../../captures/session_20260826_165024.log) and
[`171159`](../../captures/session_20260826_171159.log):
all 16 `cer_heap` rows report `spill=0`, `ext=0`, `clk=0`, and `iclk=0`.
The measured internal-first spill and nested-clock mechanisms are ruled out.
`cer_recon` is in tree to split internal-heap `filt` from PSRAM-first
`span`/`proj` and to record IntervalTimer ISR `itickus`.
[`170149`](../../captures/session_20260826_170149.log) rebooted at first
clock after Record; probe counters are now file-scope volatiles.
[`171159`](../../captures/session_20260826_171159.log) closes question 1:
`filt` stays flat, `span`/`proj` take the 7.9x, `itickus=0` on every row.
The follow-up `cer_snapshot` formatting probe was removed after
[`172305`](../../captures/session_20260826_172305.log) rebooted at record start.
`playback_build` now carries a deferred `path` field (`snapshot`,
`compatibility`, or `note_edit`) so later investigation captures can identify
the representation behind each emitted maximum sample without formatting on the
clock/interval path. The existing one-shot remains an aggregate: `count` is all
rebuild calls before emission, not distinct destinations.
Device capture [`202654`](../../captures/session_20260826_202654.log) validates
the split: 82 emitted samples contain 74 `compatibility` and 8 `snapshot`
paths, with no `note_edit` samples. `T0/S6` remains compatibility-backed
(maximum 325.541 ms), while `T4/S0` uses the published snapshot
(maximum 358 us). RC4 question 5 is closed as that `T0/S6` compatibility
replay; ignore it for snapshot-path sizing. **G1 is satisfied:**
`representativeRebuildWall` = 358 µs; `prefetchTicks` = 272; horizon
unchanged. Ring A firmware is not authorized.
[`205447`](../../captures/session_20260826_205447.log) `T0/S5` rebuild is
334 µs; the 377 ms gap at that switch is `idle_maint`, not the new-slot
rebuild.
[`172802`](../../captures/session_20260826_172802.log) confirms first-clock
recording and continued overdub/stop operation after removal.
Task 4.3 stays skipped.
[`125527`](../../captures/session_20260826_125527.log) kept unpublished
snapshot in-flight through OVERDUBBING/PLAYING with repeated
`snapshot_idle` slices and `midi_gap` spikes before cadence alternation.
[`130244`](../../captures/session_20260826_130244.log) still stacked
unpublished snapshot with long visual idle reconstruct bursts; host
refinement now constrains/gates that visual fallback during contention.
[`131338`](../../captures/session_20260826_131338.log) still showed PLAYING
stalls (`midi_gap` 195 ms, 184 ms) with first-PLAYING stacked
`idle_maint`/snapshot/persist and later `usbdisp`/`load_frame`/`late_on`
windows; host refinement now broadens unpublished visual constraints to
all pending unpublished work and caps each live-record append-start range.
[`131852`](../../captures/session_20260826_131852.log) confirms the parked
issues remain: same-turn `snapshot_idle` + `PERS,work` overlap is still
visible (`snapshot_idle us=26823` then `PERS payload_slice=20806`), large
OVERDUBBING windows remain (`midi_gap=236090`, `usbdisp=143649`,
`load_frame=25638`), STOPPED still hits `midi_gap=430552`
(`usbdisp=538063`, `load_frame=401498`, `idle_maint=337246`), and overdub
display still spikes (`timing_max,DisplayResolveLiveCapture,215619`) while
`ODUB,stop,display` remains 21.9-50.7 ms. In tree: same-turn lane now blocks
deferred save after snapshot on the same loop turn; recapture pending and
4b.1 remains open.
[`132627`](../../captures/session_20260826_132627.log) no longer showed
`PERS,work` while `snapshot_idle` was active (same-turn overlap not observed
in that run), but 4b.3 and 4b.4 owners remained:
post-overdub PLAYING `midi_gap=175230`
(`usbdisp=121603`, `load_frame=14728`, `late_on=184364`), STOPPED
`midi_gap=198666` (`usbdisp=319516`, `load_frame=191331`, `late_on=30491`),
and `timing_max,DisplayResolveLiveCapture,157754`. In tree for 4b.3:
post-overdub drain now polls MIDI during `runDeferredLoadAndDisplayFrame`
between display/load slices; recapture pending.
[`133517`](../../captures/session_20260826_133517.log) still showed
`snapshot_idle`/`PERS,work` interleaving while pending (example:
`snapshot_idle us=7360` then `PERS,work 86`), and OVERDUBBING spikes remained
(`midi_gap=200549` with `usbdisp=129828`, `load_frame=23600`, `late_on=26036`;
`midi_gap=196173` with `usbdisp=68686`, `load_frame=15178`, `late_on=96834`,
`persist_save=32056`). Post-overdub PLAYING windows in that run were in the
26-52 ms class after stop-display lines (`ODUB,stop,display` 19.6-51.4 ms).
In tree for 4b.1: lane arbitration now includes RECORDING pending snapshot
work (not PLAYING/OVERDUBBING only), and persist lane toggles now use
RECORDING/PLAYING/OVERDUBBING pending snapshot detection; recapture pending.
[`134438`](../../captures/session_20260826_134438.log) showed no `PERS,work`
entries (`0` matches) while `snapshot_idle` remained active, so same-turn
snapshot/persist overlap was not observed in that run. 4b.3 remains open:
post-overdub PLAYING still reached `midi_gap=242938`
(`usbdisp=149450`, `load_frame=53120`, `late_on=69979`) after
`ODUB,stop,display=44222`; STOPPED still reached `midi_gap=323973` with
`loop_rem,idle_reconstruct=148417`, `loop_rem,idle_maint=180315`,
`idle_maint=319996`, and `load_frame=38737`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=225601`. In tree for 4b.3:
`MidiHandler::handleMidiInput` now reduces per-call dispatch batch size from
`128` to `32` while `anyPlayingMidiDrainAfterOverdubStop()` is active, so
post-overdub drain turns bound `usbdisp` burst size before the next poll.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS; recapture
pending.
[`135317`](../../captures/session_20260826_135317.log) again showed
`PERS,work=0` while `snapshot_idle` remained active (4b.1 still open pending
repeated recaptures). 4b.3 remains open: first stop cycle held
`ODUB,stop,display=15428`, but a later `midi_gap=181392` was logged after
PLAYING re-entered OVERDUBBING; later cycles were `midi_gap=42033` and
`midi_gap=22132`. 4b.4 remains open with
`timing_max,DisplayResolveLiveCapture=163008`. In tree for 4b.4:
`DisplayManager::rebuildOverdubCommittedDisplayLayer` now skips overdub paint
snapshot gather+reconstruct when `visualCache` already has notes, preferring
held-layer/window-filter branches to bound resolve cost. Verification:
`pio run -e teensy41-capture-serial` PASS with RAM1 locals `4608`, and
`pio test -e native` PASS (`1472/1472`).
[`140201`](../../captures/session_20260826_140201.log) kept `PERS,work=0`
while `snapshot_idle` remained active. 4b.3 improved but remains open:
post-stop PLAYING `midi_gap` reduced to `48361` (from `142534` in `135317`),
but first-bars OVERDUBBING still reached `midi_gap=133468`. 4b.4 improved
strongly in this run with `timing_max,DisplayResolveLiveCapture=7107`
(from `163008`). The same capture showed overdub reopen source drift
(`DIAG,lcr,src,why=open` notes `0`/`165`/`228` versus nearby PLAYING `DISP`
totals `240`/`253`/`308`) and restamp miss evidence
(`seal_publish_span_cache_restamp ... invalidated_missing_id` together with
`capture hot stop verify: non-canonical storage (check=16)`). In tree:
`Loop::commitPendingCapturePass` now invalidates source-span cache on restamp
miss, and `Loop::rebuildOverdubSourceSpanCache` now seeds from clean
`visualCache` when prepared spans miss and no prior cache seed exists.
`pio run -e teensy41-capture-serial` and `pio test -e native` PASS;
recapture pending.
[`141034`](../../captures/session_20260826_141034.log) confirms 4b.4 stays
bounded (`timing_max,DisplayResolveLiveCapture=8608`), so 4b.4 is closed.
4b.3 remains open: first-overdub windows still show repeated
`load_frame`+`usbdisp`+`late_on` lag signatures around bars 4-8, and wrap
reopen still showed `DIAG,lcr,src,why=open ... notes=0` after a restamp miss.
In tree: source-cache fallback now also seeds from non-empty dirty
`visualCache` when prepared spans miss during an overdub session.

**Queued-dest visual cache:** `invalidateForSlotChange` leaves a PLAYING queued
destination `visualCache` in place. Firmware in tree; HITL open. Plan:
[`playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md`](../Plans/playback_preparation_queued_destination_visual_cache_invalidation_bugfix.md).

**Boot title RAM1:** `cccbeaa` margin regression fixed — DMAMEM cold globals + `assignMissingNoteIdsToNoteOnsInChunkIds` in `.cpp`. Device PASS [`161855`](../../captures/session_20260816_161855.log). Investigation frozen: [`boot_title_loadloopjob_idle_slice_investigation.md`](../Plans/boot_title_loadloopjob_idle_slice_investigation.md).

**NOTE_EDIT entry while overdubbing:** `openNoteEditSession` calls `stopOverdubbing()` first — live capture commits before rematerialize (quick fix).

**NOTE_EDIT lengthened loop select:** committed-content span for bracket/nav; skip detailed-window filter on selectable inventory. Fixture [`015731`](../../captures/session_20260814_015731.log).

**Overdub + NOTE_EDIT exit bake:** device PASS [`025322`](../../captures/session_20260814_025322.log) — `rows=1 saved=1`, session flats 234 held. Native `dropUnrequestedSessionStoreDeletes`. Stage 3 unblocked.

**OLED first-frame mismatch:** RC3 device PASS [`014553`](../../captures/session_20260814_014553.log). Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

**Loop content-only history (DEC-035):** **Archived** 2026-08-14 — OpenSpec `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log).

**NOTE_EDIT leave-restore painted span:** RC1 native shipped; device gate open — [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md). [`200154`](../../captures/session_20260813_200154.log) Restore 5 `720–2255` / select `DNTE` 1535; same-loop [`193838`](../../captures/session_20260813_193838.log) first-select paints length **47**.

**NOTE_EDIT mover wrap-length jump:** RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log) — [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md). No `2351`/`2975`. Note 14 `2256–2304` stays on the wrap-stub plan.

**NOTE_EDIT note-off pairing LIFO:** native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS — [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md). @45.971 seals five overlap rows plus the mover (`canonical=7 pre_commit=7`).

**NOTE_EDIT edit-pass replay row payload:** native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS — [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md). `Length 115 48→287` and `Length 115 1008→1103` seal and hold. Rows replay as stored.

**NOTE_EDIT overlap shorten commit seal:** native shipped; device gate open — [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md). Deselect commit dropped the overlap `Length` row on a parked mover; [`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` then @166.848 seals `Length 10 960–1247` against note 14's focus. Slice B of the resolver contracts plan **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was the correct truncation.

**NOTE_EDIT Length replay loop-boundary:** native shipped; device gate open — [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Persisted `ChangeLength` 14 `2256–2304` replayed as a wrap; same-pitch notes shortened to 2255. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log).

**NOTE_EDIT wrap-stub commit:** RC3 native shipped; device gate open — [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). RC2 device FAIL: 14 still `2256–2304` when cache paints a non-zero span. RC1 withdrawn (67.824 min-length stay-hidden).

**NOTE_EDIT display unification:** Stages 1–2 and 5–9 shipped — [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md). Device [`192007`](../../captures/session_20260813_192007.log): 45 Hide/Restore `1440–1511` (not 2160); 76 as target still `840–2160`; first NOTE_EDIT `DISP` 59/60; resolve 16.8–52.3 ms. Paint gap and undo-warm stay open.

**Active work:** Occupy clock same-tick Off-before-On HITL **PASS** [`001021`](../../captures/session_20260819_001021.log) ([`overdub_occupy_clock_same_tick_off_before_on_bugfix.md`](../Plans/overdub_occupy_clock_same_tick_off_before_on_bugfix.md)) — `n=0 a=1` = 0; `n=1 a=0` = 0. Parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4. Pin [`233247`](../../captures/session_20260818_233247.log) was 11. Interval catch-up in tree. Merged-capture gather **FROZEN**. USB occupy `playMidiEvents` catch-up HITL **FAIL** [`214856`](../../captures/session_20260818_214856.log) **reverted**. Loop-head storage-0 occupy HITL **PASS** [`203948`](../../captures/session_20260818_203948.log). Wrap-S catch-up HITL **PASS**. Occupy reads `ActiveNoteLedger` at `currentTick` ([DEC-041](../DECISION_LOG.md#dec-041-occupy-present-at-s-jit-not-full-loop-lcr-mat)). Occupy lookup: `ledger.noteId(midiChannel, pitch)`. Phase 2 **PASS** [`040236`](../captures/session_20260818_040236.log). PLAYING/STOPPED/MUTED HITL **skipped** ([DEC-040](../DECISION_LOG.md#dec-040-skip-playingstoppedmuted-overdub-participant-hitl)). **Phase 4 in tree**. Phase 3 1-bar occupy **PASS** [`121933`](../captures/session_20260818_121933.log). Stage 1 `from=span` HITL **parked**. Do not shrink `kOverdubSourceWindowBars`. Queued (no firmware): [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md). **NOTE_EDIT hydrate** queued ([`note_edit_hydrate_enhancement.md`](../Plans/note_edit_hydrate_enhancement.md)). **Playback gather** Stage 1 hooks landed. Wrap-move persist parked [`201446`](../captures/session_20260816_201446.log).

**Merged to `dev`:** PR [#35](https://github.com/Lytrix/MidiLooper/pull/35) LoopContentResolution prototype (`be0e05f`); PR [#30](https://github.com/Lytrix/MidiLooper/pull/30) overdub overlap; PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) Stage 5a-1/5a-2 + display RC4–RC5.

**Stage 5a-3:** **`pool_alloc` proof abandoned** (2026-08-12) — see [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](../Plans/long_overdub_stage5a3_critical_reclaim_verification_refinement.md).

**`chore/firmware-ownership-lifetime-review`** — P0/P1 review **closed** (manual MT gates PASS); Phase 5 + layered **`base`** **parked**.

**`feature/deferred-lazy-load`** — Phase B archived `openspec/changes/archive/2026-07-19-deferred-job-scheduler/`. Specs: `openspec/specs/deferred-job-scheduler/`.

*(Parallel: **`feature/persistence-work-queue`** @ `8b3f93e` — B1–B5 on `dev`.)*

### Firmware lines

| Line | Branch | Role |
|------|--------|------|
| v1 | `main` | Frozen — 16×2, 2-button looper |
| v2 | `midi-faders` | Frozen — iPad/DROID, no PSRAM required |
| v3 | `dev` | Active integration |

See [`docs/BRANCHING.md`](../BRANCHING.md).

## In flight

- **§36 companion admission + STOPPED first-paint:** A+B+C closed. Device PASS [`165107`](../../captures/session_20260821_165107.log) / [`183229`](../../captures/session_20260821_183229.log). Later (parked): bound `applyDeferredVisualCacheWrapAppend` (1.13 s), attribute 5 s STOPPED idle. — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Boot title `boot_commit` display stall:** Device PASS [`161804`](../../captures/session_20260821_161804.log) — USB 11.484 s, first `DISP` visual=2638, `boot_commit=288557` (289 ms). Prior gather was `161148` / `154353` (~70–75 s, visual=0) from `onBootSlotLoadComplete` wiping the idle-built cache. Guide: [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md).
- **Boot title startup stall hardening:** capture [`161410`](../../captures/session_20260820_161410.log) stops after `DisplayManager: Display buffer displayed` and never reaches boot load logs. `Looper::setup` now uses bounded SD bootstrap retries (`3` attempts, `50 ms` delay) and an explicit empty-runtime fallback when SD stays unavailable, preventing title-screen startup stalls from requiring repeated manual reboots. Native verification pending.
- **Overdub-stop seal lag optimization:** captures [`163904`](../../captures/session_20260820_163904.log), [`164944`](../../captures/session_20260820_164944.log), [`165534`](../../captures/session_20260820_165534.log), [`165956`](../../captures/session_20260820_165956.log), [`170724`](../../captures/session_20260820_170724.log), [`171246`](../../captures/session_20260820_171246.log), [`171909`](../../captures/session_20260820_171909.log), [`172553`](../../captures/session_20260820_172553.log), [`173206`](../../captures/session_20260820_173206.log), and [`174941`](../../captures/session_20260820_174941.log) keep the stop owner on `companion_seal` while `mark_range` remains low. Deterministic follow-up landed: overdub-stop path uses `SC_REC_FLUSH_PENDING_REVTS(8)` in `Track::stopOverdubbing` and in-edit `handleNoteEditFold`, removing variable USB serial flush work from the critical path before display refresh. Telemetry-visibility follow-up landed: `ODUB,stop` lines are Tier-A in `CaptureLineTier` so stop-stage lines remain visible under ring pressure with `flush(8)`. Display-specific follow-ups are reverted per user request. Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425084 / locals 4768`); HITL re-measure pending — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Idle-gather edit apply Phase A:** `applyNoteEditPassSequence` now performs strict contiguous `Velocity`/`Pitch` update collapse with barrier-preserving execution; non-collapsing baseline parity is covered by dedicated deterministic and randomized equivalence tests in `test_edit_apply`. Validation gates passed (`pio test -e native`, `pio run -e teensy41-capture-serial`) with RAM1 still within budget (`RAM1 code 424668 / locals 4640`) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay:** captures [`165534`](../../captures/session_20260820_165534.log) and [`165956`](../../captures/session_20260820_165956.log) show early-start delays (`Live Overdub -> Overdubbing started` ~2102-2249 ms) and `ODUB,stage,begin_capture` spikes (`2194189 us`, `2248843 us`). Root cause is start-path fallback to full-loop materialize in `Loop::rebuildOverdubSourceSpanCache` when prepared spans are unavailable. Stage now restores overdub-session deterministic fallback (reuse prior cache notes during active sessions; full materialize kept for non-session paths). Native **1397/1397** and `teensy41-capture-serial` build pass (`RAM1 code 425020 / locals 4768`); waiting on HITL re-measure — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start begin-capture long delay (closed):** HITL [`170724`](../../captures/session_20260820_170724.log) confirms the restoration removed the 2.2 s start stalls: `Live Overdub -> Overdubbing started` is now 2-8 ms and `ODUB,stage,begin_capture` is 8198 us / 2150 us. Same run exposes a remaining stop long-tail cycle (`ODUB,stop,seal=310980 us`, `flush=312270 us`, `display=320406 us`, `overlap_hold note_offs=100`), while `DIAG,seal_companion_batch` remains bounded (`rows=5 sealed=5 us=84`). Start regression is fixed; stop-path lag is still open — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub-start reboot:** USB drops right after `[TRACK] Overdubbing started`. Present in [`132145`](../../captures/session_20260820_132145.log) and [`143518`](../../captures/session_20260820_143518.log), i.e. before the 2026-08-20 removals, so commits `4af158a`…`6ff4142` are reverted and the tree is byte-identical to `28cf6e3`. Blocked on instrumentation: `flushCaptureBuffer` budgets `<= 8` take a drop-only branch, and `appendCaptureRecord` refuses Tier-B lines while the ring is full with a Tier-A head, so Tier-B `ODUB,stage` never reaches serial ([`162146`](../../captures/session_20260820_162146.log): last `MI` at 9.387 s, last `LED` at 14.89 s, `DIAG,lcr,phase` to 33.38 s, no `MI` for the 35.198 s button press). `CrashReport` prints before `logger.setup`, so no capture today contains it; SD `crashlog.txt` is the authoritative record and is unread. Native **1397/1397**; RAM1 locals **4768**; not flashed — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Overdub session index reboot undo:** HITL **PASS** [`003854`](../../captures/session_20260820_003854.log) — reboot `entries=2` `kind=1`; one undo 112→14 — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Cleanup branch telemetry/fader compile-gating:** `chore/cleanup-codebase-tidiness` stage shipped — telemetry-only timing work gated by owning feature macros; `MidiHandler` motor-fader USB-host service helper now feature-gated to `MIDI_USB_FADER_PROBE_PASSTHROUGH`; verification passed (`pio test -e native`, `pio run -e teensy41`, `pio run -e teensy41-capture-serial`) with zero warnings.
- **Overdub-stop handoff flash:** dirty `visualCache` must not overwrite the RC5a composed frame after overdub stop. HITL **PASS** [`001925`](../../captures/session_20260820_001925.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Display window follow readiness:** committed paint filters `visualCache.notes` for the moving 16-bar window plus lookahead; source-view is not display authority. Native **1390/1390**; RAM1 code **425276** / locals **4768**. HITL **PASS** [`000553`](../../captures/session_20260820_000553.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Companion-only projection fast path:** Full companion projection cache is admitted only while `capture.phase == CapturePhase::Overdub`. STOPPED/boot gather uses windowed apply. Mixed companion+non-companion rows keep legacy ordered replay fallback. Stage A on [`165107`](../../captures/session_20260821_165107.log) keeps Overdub `tracks` at 190 ms (was 136 s on [`162928`](../../captures/session_20260821_162928.log)).
- **Overdub participant discovery:** `feature/overdub-participant-discovery` — occupy clock same-tick Off-before-On HITL PASS [`001021`](../../captures/session_20260819_001021.log); parent catch-up pin [`235314`](../../captures/session_20260818_235314.log) was 4 `n=0 a=1`; USB `playMidiEvents` catch-up HITL FAIL [`214856`](../../captures/session_20260818_214856.log) reverted; wrap-S HITL PASS; loop-head storage-0 HITL PASS [`203948`](../../captures/session_20260818_203948.log) — [CURRENT_WORK.md](CURRENT_WORK.md)
- **Codebase consistency:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) **NOW** — Phase 4 TrackManager **shipped** PR #22; next extractions in [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)
- **StorageManager TU remaining:** **Merged** PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08); closeout [#16](https://github.com/Lytrix/MidiLooper/issues/16)
- **Firmware ownership lifetime review:** **Closed** 2026-08-06 — [`firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md); layered **`base`** HITL **parked** for dedicated refactor
- **HITL CLI rebuild:** Phase 3 — layered presets locked to `base` + `edit_full`; layered **`base`** device PASS deferred to dedicated HITL refactor
- **Edit-session-action-geometry:** **Archived** 2026-08-05 → `openspec/specs/edit-session-action-geometry/`; Phase 5 HITL matrix parked (`m8_edit_note_edit_hitl_automation_refinement.md`)
- **Note-edit control-surface split:** **complete** on `chore/note-edit-control-surface-split` — Phases 0–8; see [note_edit_control_surface_split_refinement.md](../Plans/note_edit_control_surface_split_refinement.md)
- **Long record display freeze:** queued — [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md); evidence [`012342`](../../captures/session_20260812_012342.log)
- **Next product slice:** loop length during overdub queued — [`overdub_loop_length_during_overdub_enhancement.md`](../Plans/overdub_loop_length_during_overdub_enhancement.md)
- **Archived this branch:** OpenSpec [`deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/) Phase B — gates B.1 [`231510`](../../captures/session_20260718_231510.log), B.3/B.4 [`022107`](../../captures/session_20260719_022107.log); specs in `openspec/specs/deferred-job-scheduler/`
- Memory reclaim + boot/display stack — **merged to `dev`** (PR #4)
- **Hygiene (`chore/codebase-hygiene-sprint1`):** safe debt **complete** — see [`codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md); sprint plans Status Done + [README index](../Plans/README.md#hygiene-sprint-chorecodebase-hygiene-sprint1)

## Shipped on branch (recent)

| Commit / session | Focus | Docs |
|--------|--------|------|
| `3e9253e` | DEC-029 archive + spec sync + Phase 8 closeout | DELIVERABLE_TRACKING, PHASE8_CLOSEOUT |
| `15c5750` | Playing move/length `refreshPlaybackPreview` | note_edit_playing_move_audition_bugfix |
| `7af8671` | DEC-030 §12 R1–R5 orthogonal state | note_edit_resolver_authority_contracts_refinement |
| `5af41c7` | P1 fold wrap unify; NOTELEN exit bracket; HITL serial proxy | firmware_ownership_lifetime_review |
| `85ae7d6` | P1 undo routing docs (session-gated **E:**) | LOOP_MIDI, ARCHITECTURE_RULES |
| `d87d0c6` + [`010126`](../../captures/session_20260718_010126.log) | Windowed display + queued countdown; device gate PASS | boot_load_windowed plan |
| `8b3f93e` | Sync-drain budget, clear-slot restore, post-reboot undo save | persist queue plan |
| `7a89f03` | Retire monolithic deferred save; work queue (B4) | persist queue plan |
| `b3d066f` | `stepPersistenceWorkItem` scheduler hook (B3) | persist queue plan |
| `3143ef0` | `StorageManager::admit*` API (B2) | persist queue plan |
| `736fa33` | NOTE_EDIT move/pitch bracket — mover NoteId lookup | CURRENT_WORK |
| `0f086f4` | Split-focus slot switching + loop-end commit (DEC-025) | Loops.md |
| `f6b496a` | Slot short-press deferred-restore fix | — |
| `346f7ce` | Playhead projection + undo geometry restore | — |
| `b1d3259` | Loop-scoped undo display (DEC-024 Phase 1) | loop_undo plan |
| `50ad01b` | Cold boot + USB Host defer + slot scan hardening | DEC-021/022 |
| `e40f26c` | DEC-020 Phase 4 mid-pass persistence writer | DEC-020 |
| `f0ee520` | DEC-020 Phase 3 cooperative scheduler HITL | DEC-020 |
| *(prior)* | Phase A step 3 + Phase B — one materialize per revision | handoff |
| `d635296` | Partial Phase A — PLAYING defer, REVT bounds; boot + 16-bar smoke | handoff |
| `eea4734` | Serial evidence index (archived captures) | plan |
| `894d2ea` | Runtime architecture docs + DEC-016 | Architecture/ |
| `d05e736` | Display boot/play OLED freeze | — |
| `b1260ce` | Cold buffers → external memory pool | INTERNAL_HEAP guide |

## Active OpenSpec

This branch's active scope is the design-only
[`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
change. The maintained-LCR changes below are retained as historical base context
and are not implementation scope here.

| Change | Focus |
|--------|--------|
| **`immutable-loop-snapshot-passes`** | Phase 2 publication complete; Phase 3 tasks 4.1–4.2 selected; snapshot nodes pack into 32 KB slabs (`DEC-048`); 4.3–4.6 not selected |
| `loop-content-resolution` | Historical base prototype; maintained-LCR continuation not pursued on this branch |
| `loop-effective-event-source` | Historical base context; no new LCR work on this branch |
| **`hitl-cli-rebuild`** | **Phase 3 in progress** — layered `base` + `edit_full`; 3.2 bridge done; next: 3.3 device PASS |
| **`continuous-runtime-persistence`** | Phase 0–4 shipped; Phase 4 HITL **passed**; Phase 5 recovery **parked** (see firmware ownership review) |
| **`runtime-derived-representation-heap`** | M5 Steps 1–1b–2 shipped (`adoptPersistedSnapshot`); lazy load + 64+64 HITL pending |
| **`slot-selection-focus`** | **Shipped (firmware)** — orchestrator + footer extension; manual §8 pending |
| **`unified-interval-projection`** | Phases 1–5 code shipped; native PASS; **5.5 HITL deferred** (DEC-017); Phase **6.1–6.2** synced with geometry (2026-08-04) |
| `set-revision-persistence` | Set revisions, overlay browser — core shipped; loop picker **4.8–4.10** paused |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview; future `Timeline` projection consumer |
| `note-edit-tick-coordinates-and-audition` | Geometry wrap regression: tick spaces + session-store playback preview |
| `linear-loop-tick-storage` | Canonical linear storage + projection alias — prerequisite for UIP |

## Archived OpenSpec (normative specs in `openspec/specs/`)

| Change | Archived | Normative spec |
|--------|----------|----------------|
| **`loop-content-history-persistence`** | 2026-08-14 | [`loop-content-history/`](../../openspec/specs/loop-content-history/) |
| **`note-edit-current-state`** | 2026-08-08 | [`note-edit-current-state/`](../../openspec/specs/note-edit-current-state/) — [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md) |
| **`edit-session-action-geometry`** | 2026-08-05 | [`edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| **`deferred-job-scheduler`** | 2026-07-19 | [`deferred-job-scheduler/`](../../openspec/specs/deferred-job-scheduler/) |

**Side fix (2026-07-03):** `note-edit-tick-coordinates-and-audition` — long-loop pitch edit regression at loop wrap; handoff [note_edit_geometry_wrap_regression_bugfix.md](../Plans/note_edit_geometry_wrap_regression_bugfix.md). HITL pending.

**Archived (2026-07-03):** `note-edit-fader-feedback-regression` → `openspec/changes/archive/2026-07-03-note-edit-fader-feedback-regression/`; normative spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.

**Archived (2026-07-03):** `note-edit-stable-note-id` → `openspec/changes/archive/2026-07-03-note-edit-stable-note-id/`; normative spec `openspec/specs/note-edit-stable-note-id/`.

**Handoffs (shipped):** [note_edit_fader_feedback_next_steps_handoff.md](../Plans/archive/handoff/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../Plans/archive/handoff/note_edit_stable_note_id_phase_a_handoff.md)

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only
- **`edit-session-action-geometry`** — **archived** 2026-08-05; normative `openspec/specs/edit-session-action-geometry/`
- **`loop-content-history-persistence`** — **archived** 2026-08-14; normative `openspec/specs/loop-content-history/`

## Current blockers

- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/Authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012, **shipped**)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (DEC-012): job RAM in `Internal.cpp`; overlay in `Overlay.cpp`; FSM in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator in `StorageManager.cpp`
- HITL serial line buffer in RAM (FLASHMEM `processHitlSerialCommands` cannot access DMAMEM on IMXRT1062)
- **Capture-serial RAM budget (2026-07-04):** note-edit overlap code in FLASHMEM (`NoteEditMem.h`); deferred `#DBG`/`REVT`/`PERF` via 96 KB PSRAM ring in `DebugSessionCapture` — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](../Plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- Normative specs: `openspec/specs/revision-load/`, `storage-session-jobs/`, `storage-session-layout/`
- **Next persistence layout:** `transport.bin` / `global.bin` — not started (post DEC-012)
- **Interval projection:** `IntervalProjection` engine owns all wrap math — **`include/Utils/IntervalProjection.h`** + **`src/Utils/IntervalProjection.cpp`**; Phases 1–5.1–5.4 + 5.6 shipped (grep gate on D7 consumers); `Track.projectionCycleStartTick` + queued start at grid; `PlaybackCursor` retired; **5.5 HITL** pending
- `ButtonManager` / GPIO dormant (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/`
- **Runtime scheduling:** contract [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md); roadmap [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Interval reservation is **not** authorized. Persist `admit*` and `DeferredJobScheduler` (DEC-027) remain separate.
- **Active linkage:** [`CURRENT_WORK.md`](CURRENT_WORK.md) overdub-lag stages `P1`–`P4` feed the roadmap as pre-contract owner-boundary work (causality + arbitration ordering), not as interval-reservation implementation.
