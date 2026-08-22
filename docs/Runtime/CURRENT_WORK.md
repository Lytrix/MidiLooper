# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-08-22 (paired companion consume reverted; §36 closeout retained)

Authority hygiene (does **not** change § Now implementing): execution bound class is a gated property — [ExecutionBounds.md](../Authority/Architecture/ExecutionBounds.md), [DEC-043](../DECISION_LOG.md#dec-043-execution-bound-class-is-a-gated-architectural-property). Architecture-checkpoint question 3 is now a hard stop. No firmware, no scheduler, no interval reservation.

---

## Now implementing

No authorized firmware. §36 Stage A+B+C is closed. Later tasks are parked below — do not start the 1.13 s wrap-append bound or the 5 s idle attribution until a new slice is opened.

### Paired companion consume reverted

**Evidence (from the reverted branch):** [`163135`](../../captures/session_20260822_163135.log) — gather served `from=paired_proj` **0** times and `from=win_apply` **33** times; `idle_gather` median 1.61 s / max 7.75 s; `midi_gap` max 7.82 s; one `#CAPTURE_RECONNECT`. The `LoopContentResolution` device gate spent 331 s in `prep` and never completed, ending `DIAG,lcr,skip,dirty`. In [`155153`](../../captures/session_20260822_155153.log) the same gate advanced its `idx` phase at **13.5 events/s** and also never completed. Those `paired_proj` / `win_apply` gather tags were part of the reverted work and are **not** emitted on this branch.

**Decision:** `PairedCompanionState` and its gather consume, suffix catch-up, and PLAYING-idle advance are reverted. Retained tip is `ce9961c`; the reverted commits remain on `feature/overdub-start-stop-lag-optimizations` for reference. The subsystem never served a gather on device, so the revert removes no measured capability.

**Invariant:** There is one derived authority for effective committed notes. A second cache that wipes on undo must not be wired to the playback-window build while a cache that survives undo exists.

**Current behavior on this branch:** every `copyEffectiveCommittedEventsInRange` call with `includeActiveEditRows` replays active companion rows through `applyNoteEditPassSequence`. `tryCopyCompanionProjectedWindow` is copy-only and its cache is never populated — `rebuildCompanionProjectionForCurrentRevision` has no callers, so `companionProjectionEvents_` is dead scaffolding. This is the predictable-but-slow baseline, not a regression.

**Direction (not authorized firmware):** `LoopContentResolution` spans / checkpoints become the single derived authority — it already carries incremental wrap append (`publishPreparedOverdubPass`), in-place undo patch with query-time restore (`setPreparedCapturePassState` / `setPreparedEditPassState`, `eraseDisabledSounding`, `restoreDisabledCompanions`), and present-at-tick (`tryCollectPreparedPresentNoteIdsAtTick`). Blocker before any consumer wiring: the prepare grain. Plan: [`loop_content_resolution_span_consume_refinement.md`](../Plans/loop_content_resolution_span_consume_refinement.md).

**Status:** Native **1429/1429**. Overlap resolution is unchanged — it reads `ActiveNoteLedger` at note-on and `overdubSourceViewNotes_` at note-off, neither of which touched the paired projection.

### STOPPED undo / track-switch first-paint

**Evidence:** [`183229`](../../captures/session_20260821_183229.log) device PASS. Prior fail [`161804`](../../captures/session_20260821_161804.log) / [`165107`](../../captures/session_20260821_165107.log) undo `idle_maint` 850 ms. Plan: [`runtime_scheduling_owner_boundary_inventory_investigation.md`](../Plans/runtime_scheduling_owner_boundary_inventory_investigation.md) §34 / §36 Stage C.

**Owner:** `DisplayManager::invalidateForSlotChange` / `Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice`

**Invariant:** STOPPED undo and track switch keep `visualCache.notes` and fill through budgeted idle slices with wrap-append deferred. No wipe. No unbounded gather-plus-append in one STOPPED turn. No companion full replay on STOPPED.

**Status:** Device PASS [`183229`](../../captures/session_20260821_183229.log) — undo `stale_all` notes=2988, first `DISP` visual=2988, first post-undo `idle_maint` 224 ms; 68-bar switch notes=3040 / visual=3040. Native **1429/1429**. Closed.

### Overdub companion admission and source-span prewarm

**Evidence:** [`162928`](../../captures/session_20260821_162928.log) empty open + 136.77 s freeze. Device PASS [`165107`](../../captures/session_20260821_165107.log): 5/5 opens `notes` 1319–1394; 34/34 consume `idsel>0`; max `tracks` **190 ms**. Residual `norow` 1–4 with working select is parked.

**Owner:** `Loop::tryCopyCompanionProjectedWindow` / `Track::processDeferredIdleMaintenance` → `Loop::prewarmOverdubSourceSpanCache`

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
