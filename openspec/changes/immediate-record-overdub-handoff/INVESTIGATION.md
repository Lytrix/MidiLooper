# Investigation — immediate Record/overdub handoff

Status: read-only source/capture investigation and one host metadata probe; no firmware or test-source edits, board action, upload or content mutation. Branch `feature/unified-rolling-playback-path`, baseline `faa089b8` plus existing dirty handoff/display work. Installed image74392e15 remains unchanged.

## Scope and execution path

`MidiButtonActions::handleToggleRecord` → `retainOverdubStartAfterRecord` currently stores Start while the first Record pass is private. `serviceAcceptedInteractionTurn` skips while a stop job exists; `driveAcceptedInteraction` then requires committed passes before ordinary `TrackManager::startOverdubbingTrack` → `Track::startOverdubbing` can run. This is the deliberate queued-only policy, not a missing manual rolling switch.

Record stop: `beginDeferredRecordStopForSlot` proves reservations, seals `capture.store` into `presentations[slot].rawRange`, transfers held-note receipts, binds canonical output and a Record job, and returns to PLAYING. Existing bounded closure/staging/normalization/pass-transfer/publication follows later. The producer directory is already free after sealing.

Overdub stop: `beginDeferredOverdubStopForSlot` currently rejects any pending job, requires `captureStopOwner`/canonical output to be free, freezes and exchanges live/frozen staging, and attempts to seal into **the same slot presentation range**. Thus starting B earlier alone does not make early Stop safe. A four-entry FIFO does not supply four independent raw extents or four finalizers.

## Ownership and lifetime map

- Track owns live interval identity, admitted jobs, accepted interaction and stable presentation storage; TrackManager owns actual record/overdub orchestration. `activeLoopIndex` remains sole activation authority.
- Loop owns producer Capture, passes, pending overlap changes and session source. `sealCaptureRange` is an O(1) pre-admitted ownership cut; the range is exclusive immutable raw input, not a committed pass/root.
- LoopPlaybackRuntime owns live/frozen staging and the one canonical output/worker binding. The shared worker is serialized; pre-admitted sealed custody must not imply parallel canonical writers.
- Record presentation outlives interaction FIFO retirement (DEC-142). Source borrows are keyed to stable owner address/generation. Reusing a job slot cannot recycle a still-read range.
- CaptureRange has bounded release; no destructor/content-sized walk on the command edge. Extent references remain pinned until readers/finalizer/persistence continuations release them.

## Deferred source, publication, undo and persistence

`Loop::beginCapture(Overdub)` calls `establishOverdubSourceView` when unestablished. `rebuildOverdubSourceSpanCache` can reconstruct committed passes synchronously when snapshot/prepared sources miss. It is not proof that the private Record predecessor is a qualified overlap source. The generic capture read borrow can expose sealed rows, but overlap and committed-root readers have separate qualifications; raw/private data cannot be mislabeled committed currency.

`stepDeferredRecordPublication` commits prepared content, calls `TrackUndo::pushRecordPassAdded`, queues the root and persists the job slot. `pushRecordPassAdded` currently erases prior slot undo entries. `stepDeferredRecordEmptyResult` may call `resetPassTimeline` and zero geometry. Immediate successor ingress requires explicit guards/receipts so neither path alters B/C. Accepted pending Hide/Shorten work must remain attached to its causal predecessor even when A publishes.

StorageManager remains the I/O owner; no synchronous SD or schema change is needed/proposed. Save/undo must retain committed authority and accepted operation order, not advertise private intermediate state as complete.

## Device evidence

Source: `captures/session_20261025_121228.log`, bounded streaming extraction (43,380lines).

- Record stop246.972s; first overdub command248.398s; publication255.695s; actual start255.820s. Stop→start~8.848s; command→start~7.422s.
- First manager admission74.319ms, begin-capture73.945ms; next manager7.980ms, begin-capture7.613ms. The capture assigns a stage, not an exact inner allocation/source subcall.
- Normalization phase2 takes1,400calls before completion. No deadlock claim; whole-take preparation lies on the current interaction dependency.
- The delayed start occurs just before another recognized press at255.822s stops it. This illustrates why retaining intent is not equivalent to immediate capture.
- Ring loss is explicit (including668initial evictions and later19); logs support positive observations, not full content/parity or absence claims.

### Recording-start contention is independent

Record command183.116s→actual start183.145s (~29ms). The short-press expiry is152ms overdue; button-Off→start~482ms includes gesture recognition and must not all be attributed to start preparation.

At183–200s:33 sampled `idle_reconstruct` spans135.286/138.275/144.405ms min/median/max, two append spans91.741–93.122ms; enclosing maintenance is nested, not additive. First note input-service gaps141.582,170.620,175.551ms, followed by0.738ms catch-up. These timestamps mark firmware service, not physical USB arrival; MI has no tick column.

The matching rows report `VCACHE,idle_src,PassesResolve`. `Loop::rebuildVisualCacheIdleSlice` falls back to whole-pass `LoopContentResolution::resolveDisplayNotes` before filtering to the window; its fallback event-budget argument does not bound that branch. This explains a content-sized child in a nominal slice. Exact owner Track/slot is UNKNOWN: the `255` suffix is an active-load probe, not visual caller identity, and other tracks can receive deferred work while selected Record runs.

The earlier `session_20261025_112515.log` has near125ms input cadence and3.5–4.3ms maintenance maxima, but differs in transport start/background state. Observed timing regression is real; which patch made it reachable is not established. The new state/latch hooks do not perform per-clock visual reconstruction.

## Resource probe and constraints

Compiled a header-only native size probe with `c++ -std=gnu++17 -DPIO_UNIT_TEST_NATIVE=1 -I include` into session scratch. Result: host CaptureRange112bytes, LoopEventStore88bytes, pool512chunks, chunk capacity256events. Existing range reservation also prewarms a directory of at least512 uint16 entries (payload at least1,024bytes) and a successor tail; object sizeof is NOT the total reserve or target price.

Required envelope terms: live producer + outstanding sealed intervals + presented/retiring immutable predecessor + one serialized canonical scratch/output + directory/tail credits + accepted event bodies + undo/root/display/persistence overlap + recovery reserve. Charge shared event bodies once; no extra whole-take derived copy. Existing Track384byte target and32byte latch pins must not be changed casually; keep added custody under externally allocated active Track roles and measure target ABI/linker banks. Config defines8tracks/8slots today; do not claim16tracks or maximum overlap on all tracks without proof.

## Existing validation and missing discriminator

Useful actual production harnesses: `test_shared_capture_finalization` (including private Record/queued Start, early source return, clear/empty/undo/publication and manager geometry), `test_continuous_capture` (staging/reentry/Stop), `test_live_capture_display_boundary` (real resolve/paint/DMA and exact identities), `test_record_preparation` (bounded cursor, differential policy/pressure).

Missing decisive test: withhold A publication, accept B input, Stop B at its command boundary, accept C, Stop C before A completes, retain distinct sealed identities while the FIFO/presentation roles retire, then compare final content/overlap/undo/save to an independent sequential oracle. The queued-only tests explicitly require B to stay PLAYING until A publishes, so they cannot prove the requested behavior.

## Classification and readiness

**VERIFIED:** current private-publication gate; producer seal primitive; shared per-slot range/canonical-owner conflict; synchronous source/display fallback; late undo/empty cleanup hazards; positive device timing above.

**APPROVED DIRECTION:** user explicitly permits pre-admitted per-interval sealed-range custody inside Track with measured memory cap, no new Manager or SD schema. Preserve the existing presentation/interaction lifetime split and serialized canonical worker.

**ASSUMPTION TO TEST:** that a bounded extension of these roles can meet A+B+C overlap within the product memory/timing envelope without an extra whole-take derived store.

**UNKNOWNS:** target custody cap/byte price and retiring-reader overlap; exact incremental source mechanism/deadline; bounded visual fallback continuation and its production wakeup; numerical supported service/response envelope.

**CONFLICT TO RESOLVE AT IMPLEMENTATION:** new immediate behavior replaces DEC-158/active shipping delta's queued-only policy. Do not call Start-before-publication successful until early Stop/source/undo/empty behavior is proved. Lifetime/undo changes require the repository's existing reassessment/decision protocol before production edits.

**G0 verdict — evidence gate:** sufficient to define the new item and concrete verification packets; **firmware readiness BLOCKED** on the listed custody/source/bound contracts. No implementation approval or new behavior is claimed.
