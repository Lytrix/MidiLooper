## Why

Recording must feel like the later responsive overdub cycles: stopping Record must release live input ownership without waiting for whole-take normalization/publication, and the first overdub must also stop at its requested boundary. `session_20261025_121228.log` demonstrates an ~8.85 s Record-stop→overdub-start delay plus independent 135–144 ms visual reconstruction stalls during recording; preserving a button press through that delay does not meet the product requirement.

## What Changes

- Pre-admit the resources for the supported handoff envelope; seal Record into an immutable, identity-qualified predecessor and activate a separate live Capture role at the command boundary.
- Reuse ordinary overdub admission/stop ownership rather than clone its legacy synchronous fallback. Accept first-overdub input before predecessor canonical publication; resolve source-dependent overlap without blocking ingress or treating an empty/stale pass set as the base.
- Include **overdub stop while Record remains private**, followed by another start/stop, as mandatory behavior. A retained Stop that continues appending past the requested endpoint is not a passing implementation.
- Make late Record publication, empty-result cleanup, undo attachment and persistence predecessor-local; preserve successor input, overlap changes, geometry and history.
- Bound visual-cache reconstruction on real-time turns, including snapshot/prepared misses; keep a coherent valid presentation while existing off-clock preparation progresses.
- Replace the queued-only policy of DEC-158 — post-Record retained intent once this new contract is implemented. Until then, the installed behavior and historical evidence remain accurately documented.
- **Readiness condition:** prove per-interval sealed custody and price the overlap before firmware implementation. Existing ring depth is not proof of independent raw ranges or canonical output capacity.

## Capabilities

### New Capabilities

- `realtime-visual-cache`: bounded real-time visual-cache work with exact content/pairing and coherent presentation across deferred reconstruction.

### Modified Capabilities

- `capture-state-guards`: add immediate first-Record/overdub handoff, early successor-stop and late-finalization isolation requirements while preserving idempotent start and single-writer capture.

## Impact

Existing owners remain Track/TrackManager, Loop/LoopPlaybackRuntime, TrackUndo, StorageManager and DisplayManager. Primary paths are `TrackOverdubLifecycle.cpp`, `TrackCaptureStopCommit.cpp`, `TrackCaptureInput.cpp`, `TrackManagerCaptureQueue.cpp`, `LoopCapture.cpp`, `LoopOverdubSession.cpp`, `TrackUndo.cpp`, `LoopVisualCache.cpp` and `LoopContentResolution.cpp`; their exact firmware edit packets depend on the custody proof. No new Manager or publication scheduler is proposed.

Brownfield authority and context: [project intent](../../../docs/Authority/PROJECT_INTENT.md), [ownership rules](../../../docs/Authority/ARCHITECTURE_RULES.md), [readiness gates](../../../docs/Authority/IMPLEMENTATION_READINESS.md), [delivery tracking](../../../docs/DELIVERABLE_TRACKING.md), [current scope](../../../docs/Runtime/CURRENT_WORK.md), [capture guards](../../specs/capture-state-guards/spec.md), [commit semantics](../../specs/loop-commit-semantics/spec.md), [prewarm](../../specs/playback-runtime-prewarm/spec.md), [single rolling reader](../../specs/windowed-playback-generation/spec.md). Related active changes: `shipping-rolling-activation`, `runtime-working-set-ownership`, `successor-capture-reentry-unguarding` and `unified-capture-commit-owner`; this item is not blanket permission to execute their remaining queues.

## Evidence and readiness

**VERIFIED:** Record stop seals a job-owned range and frees its producer directory. Ordinary overdub has live/frozen staging roles. However both stop routes point at one `presentations[slot].rawRange`; overdub stop also requires the predecessor's shared canonical output/owner to be free. The current Start latch deliberately waits for publication. First source establishment may synchronously rebuild spans. Late Record publication prunes slot undo; empty cleanup may reset the timeline. These are dependencies, not reasons to remove guards.

**VERIFIED device observation:** Record stops246.972 s, first requested overdub248.398 s, publication255.695 s, actual start255.820 s. Initial begin-capture stage73.945 ms versus7.613 ms on the next start. Recording-start visual reconstruction repeatedly135–144 ms; input-service gaps~142–176 ms. Captures contain shedding/evictions, so this is attribution evidence, not parity/timing acceptance. See [investigation](INVESTIGATION.md).

**Preferred direction, not yet proved:** extend existing owners with pre-admitted, generation-qualified sealed-interval custody and prepare the first-overdub source while Record is live. Keep shared canonical workers serialized; do not duplicate whole-take derived buffers.

**UNKNOWN / readiness blockers:** exact custody/retirement representation and byte price for Record A plus stopped overdubs B/C; how the source covers pending same-pitch edits before canonical publication; how all-filtered Record cleanup preserves accepted successors; quantitative supported-envelope response/service bounds. Firmware tasks are withheld until these are resolved and any lifetime/undo contract change is explicitly ratified.

## Non-goals and open decisions

No manual rolling-mode switch, second playback reader, schema migration, content clearing, whole-take sort on a command edge, arbitrary larger work quantum, scheduler redesign, retroactive accepted-content truncation, or copied legacy synchronous stop. No new track count/layered/jam feature or blanket removal of compatibility emitters. The prior stop-display viewport defect remains separate unless it blocks the defined handoff oracle.

TBD before firmware: measured maximum outstanding sealed intervals and admission/reserve policy, numerical command-to-ingress/stop and input/output timing envelope, and verified source readiness mechanism. These cannot be replaced with a silent queue wait or an unmeasured memory claim. Existing safety/fault policy remains authoritative outside the eventually declared supported envelope; no new UX is invented here.
