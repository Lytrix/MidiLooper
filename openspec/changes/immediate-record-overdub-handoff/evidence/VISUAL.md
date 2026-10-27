# P1 — visual-source-miss counterexample (task 1.6)

## Verdict / execution packet

**BLOCKED for complete task 1.6 acceptance; counterexample implemented and demonstrated.**
Do not check off task 1.6: a reachable **bounded** source-miss continuation producer,
private-work supersession/retirement and its enforced work-unit oracle are missing.
Four GREEN characterization controls do **not** establish the proposed visual bound.
One independently selected desired-bound case is intentionally RED at the named
whole-pass fallback assertion, after exact content succeeds.

Scope: test/measurement only, on `feature/unified-rolling-playback-path`, using the
existing dirty source/display harness without changing it. Only the new probe and
this evidence file are owned by this slice. Contract: proposal/design decision 4,
`specs/realtime-visual-cache/spec.md`, task 1.6; DEC-159 — immediate first-overdub
sealed custody. No production behavior, new success-domain stubs, hardware, upload,
SD operation, shared handoff/source fixtures, suite-registration edit or timing claim.

Owner/entry: actual `Loop::rebuildVisualCacheIdleSlice`; presentation remains actual
DisplayManager. Test borrows `test_live_capture_display_boundary.cpp` with main
renamed, including its real Track/Loop/content/edit/display closure and native
clock/allocator/physical-display boundaries. `CommittedChunkIdTestHelpers.h` supplies
the existing ownership-transfer fixture helper. No copied resolver/state machine.

## Fixture and independent content oracle

New suite: `test/test_immediate_visual_cache_probe/test_immediate_visual_cache_probe.cpp`.
The parent must register it as a firmware-boundary suite **and prepend the existing
`test/test_live_capture_display_boundary/firmware` include directory** (generic
firmware boundaries alone are insufficient for the borrowed display closure).

- Selected track 0 actually Records and accepts a distinctive pair; background
  track 1 / slot 0 owns the reconstruction. This is a direct production Loop entry
  test under another real recording track, not a proof of complete manager scheduling.
- Background: 33 bars, Record with 8 pairs/bar, 16 committed overdubs with 1 pair/bar:
  17 passes, 792 pairs / 1,584 stored input events; no root, no prepared index, no
  publication-gap shortcut. Pool/allocation operations must succeed.
- Independent expected inventory is constructed from the seeded identities and
  explicit endpoints, not from another resolution algorithm. Record ids 1000–1263;
  overdub ids 10000–10527. Same-pitch overlapping Record/overdub bodies require
  pass-local pairing. Length row shortens id1000 to [12,40); Delete hides id1001.
  Exact oracle checks every surviving NoteId once, pitch, velocity and start/end,
  plus exact cardinality (no extra/hidden identities). DisplayNote has no channel
  field, so no fictitious display-channel assertion is made.
- One-bar request, constrained gather and fallback budget=1: actual gather-source
  observation is `PassesResolve`; exactly 23 first-bar notes survive, other bars
  remain dirty and partial publication advances the cache revision once.
- Subsequent 32 actual slices produce all 791 effective notes. Capture remains
  Record with its original accepted identity and accepts another distinct pair.
- Revision/source-content replacement between slices disables the first overdub
  (33 identities removed), replaces Length with [12,28), increments the fixture's
  committed revision and invalidates display caches. Exact final inventory is
  758 notes, with no disabled-pass residue. This exercises old cache-row removal
  and replacement append. It does **not** establish cancellation of an in-flight
  resolver cursor: none exists on this path.
- First/last-bar append paths run; a separate initial slice denies wrap append,
  then changed-revision rebuilding plus `applyDeferredVisualCacheWrapAppend`
  exercises the existing append handoff. There is no synthetic held/wrap note in
  this new inventory; exact wrap-held pairing, retired source/reference lifetime
  and bounded append/free costs remain unproved.
- Reused production held-presentation controls:
  `test_busy_transfer_keeps_complete_frame_pending_and_receipts_separate` and
  `test_moving_window_gap_never_mixes_old_prefix_with_new_viewport_overlay`.
  They check immutable DMA bytes/receipts and qualified held interval through a
  publication gap. They are controls, **not** a source-miss deferred-frame proof.

## Actual work exposure and measurement limits

Actual path in `src/Loop/LoopVisualCache.cpp`:

1. `rebuildVisualCacheIdleSlice` chooses the dirty interval (lines 320–346).
2. Current-root/prepared attempts miss (371–401); fallback state is reset, not
   advanced. `lastVisualCacheIdleGatherSource()` reports `PassesResolve` (409–410).
3. Line 445 synchronously calls `LoopContentResolution::resolveDisplayNotes(passes,
   loopLengthTicks, paired)` **before** filtering notes by the window (451–455).
4. `src/LoopContentResolution.cpp:1683–1709` walks every active pass, copies each
   complete chunk list, applies active edits, pairs pass-local notes, sorts the
   active-overdub list and appends all effective notes. Its counter bump is only
   `out.size() + layerCount`, not actual total scan/edit/pair/sort work.
5. Additional whole-vector costs remain: reserve/filter/destruction of `paired`,
   wrap append (475–481), cache erase/append/duplicate handling (526–537), dirty
   search/scan and eventual vector/source retirement.

The probe calls the same production resolver **separately**, with its existing
`ResolutionCostCounters` parameter: **791 full notes + 17 layers = 808
`resolutionOperations`**. It checks the complete independent oracle before using
this number. This is reproducible partial accounting, not a timer and not an
instrumented count of the actual idle invocation: that invocation passes no
counter. No production observation seam was added. Pair/scan/edit/append/free
work is NOT fully accounted by 808, and zero in other counter fields would NOT
mean those operations did not happen.

Observed output:

```text
VISUAL owner=track1 slot0 source=PassesResolve bars=33 passes=17 input_events=1584 full_notes=791 separate_resolver_ops=808 idle_call_counter=UNAVAILABLE requested_bars=1 fallback_budget=1
```

The RED test is a **symbolic forbidden-route oracle**, not an invented numerical
per-turn threshold: a real-time source miss must not select the current synchronous
whole-pass-before-filter route. It checks exact first-bar content before failing
at `test_desired_bound_source_miss_must_not_resolve_whole_passes`. Removing that
route alone would not prove eventual progress or numerical bounds.

## Existing producer/wakeup map and readiness blockers

- `Track::processDeferredIdleMaintenance` invokes the same visual entry on eligible
  active tracks (`src/Track/TrackDeferredMaintenance.cpp:1079–1187`) and stopped
  tracks (1205–1227), using nominal fallback budgets 96/192. Those arguments do not
  bound the fallback resolver. Wrap work is separately serviced at 1034–1038 and
  1230–1232, but has no pass-pair/append unit continuation.
- `Loop::advanceFallbackGatherSession` exists at
  `src/Loop/LoopVisualCache.cpp:628` and has revision/window-qualified traversal
  state. **It is not called by this visual miss path.** The path resets it at
  398. A dormant traversal is not a producer and does not solve exact pass-local
  edits/pairing by itself. This agrees with ExecutionBounds owner-inventory row 5.
- Candidate prepared producer is `Track::maybeQueueContentResolutionDeviceGate`
  → `processDeferredContentResolutionDeviceGate` →
  `LoopContentResolution::deviceGateRunOneSlice` → `deviceGateComplete`.
  In `TrackDeferredMaintenance.cpp:394–432`, queueing requires SESSION_CAPTURE,
  selected Track and a **clean** visual cache. Worker checks selected identity,
  edit state, committed source/geometry, dirty policy and restore/hydrate deferral
  (435–494); dirty same-length work skips, length mismatch resets. Its call at
  1074 is on the locally fully-idle branch. It cannot be counted as a wakeup for
  a dirty background track while track 0 records. Native builds here do not set
  SESSION_CAPTURE. Manually invoking the gate to manufacture readiness would not
  prove the production wakeup and is deliberately omitted.
- First-record bar service at 1019–1026 is another existing producer, but produces
  that recording Track's first playback bar, not an arbitrary background visual
  pass-local source. Snapshot owner at 1041–1043 services a pending publication;
  an absent request is not auto-created by this miss.

**BLOCKED:** no demonstrated reachable bounded producer for this dirty background
source, no declared enforced complete work-unit bound, no generation/geometry/slot
supersession oracle for private visual pairing, no persistent scan/pair/edit/compose/
append/retire cursor and no eventual exact replacement-frame proof under that
producer. Current between-slice revision control and held-frame controls must not
be relabeled as these missing proofs. Device input-service gap/output-lateness
qualification is NOT RUN and requires a later authorized stimulus/contract.

## Commands and results

No PlatformIO invocation was made (avoid concurrent parent build). Standalone
macOS compilation uses the prescribed native flags plus the registered firmware
boundary include/dead-strip settings; outputs stay in session scratch.

```sh
cc -I .pio/libdeps/native/Unity/src -c .pio/libdeps/native/Unity/src/unity.c \
  -o "$PI_SCRATCH_DIR/visual_unity.o"
c++ -std=gnu++17 -DPIO_UNIT_TEST_NATIVE=1 \
  -Itest/test_live_capture_display_boundary/firmware -Itest/test_support/firmware \
  -Iinclude -Iinclude/EditStates -Ilib/SSD1322_OLED/include \
  -I.pio/libdeps/native/Unity/src -ffunction-sections -fdata-sections \
  test/test_immediate_visual_cache_probe/test_immediate_visual_cache_probe.cpp \
  src/Loop/CapturePhaseState.cpp test/test_support/ProjectionGateWeakStub.cpp \
  test/test_support/EnvelopeInstrWeakStub.cpp "$PI_SCRATCH_DIR/visual_unity.o" \
  -Wl,-dead_strip -o "$PI_SCRATCH_DIR/immediate_visual_probe"
"$PI_SCRATCH_DIR/immediate_visual_probe"
IMMEDIATE_VISUAL_CASE=bound "$PI_SCRATCH_DIR/immediate_visual_probe"
```

- Initial compile caught an invalid DisplayNote channel assertion; corrected to
  the fields actually represented. Final compile PASS; existing borrowed
  `DISP_COLD_MEM` macro-redefinition warning remains unchanged.
- Default / `IMMEDIATE_VISUAL_CASE=controls`: **4 tests / 0 failures / 0 ignored**, exit0.
- `IMMEDIATE_VISUAL_CASE=bound`: **1 test / 1 intentional failure / 0 ignored**, exit1,
  exact content passes, failure message:
  `EXPECTED RED: one-bar budget=1 still selects whole-pass resolveDisplayNotes; bounded producer absent`.
- Unknown selector exits2 (not silently treated as a passing default).
- Full native / PlatformIO registration: parent-owned, not run here.
- Firmware build/RAM1/device: NOT APPLICABLE to this test-only delegation; no hardware claim.

After parent registration, intended focused commands are
`pio test -e native -f test_immediate_visual_cache_probe` and
`IMMEDIATE_VISUAL_CASE=bound pio test -e native -f test_immediate_visual_cache_probe`.
The second command must report the named behavioral failure, not build/link failure.
