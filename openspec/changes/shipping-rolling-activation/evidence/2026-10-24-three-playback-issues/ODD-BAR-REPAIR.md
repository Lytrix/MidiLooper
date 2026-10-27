# Odd-bar rolling repair — execution packet

Scope: owner-approved expansion of shipping-rolling-activation 2.2 — successor continuation;
only TrackPlaybackWindowBuild.cpp and its runtime-working-set production fixtures. Preserve
preexisting dirty edits. Parent owns documentation closeout, full native/build/device gates.
Branch: feature/unified-rolling-playback-path.

## Fixed oracle (before production edits)
- Extend the existing production switch/launch fixture to a 33-bar (25,344 tick) committed target.
  Provision, immutable preparation, actual switch bind and first consumer must leave the same
  source-qualified, storage-generation-qualified rolling frame Serving, with no whole-take reservation.
- Drive the actual requester/worker/clock/emitter across the terminal interval and natural wrap.
  A demand at phase 26 bars targets phase 32 bars, covers ONLY [24,576,25,344), adopts at its
  original absolute deadline and expires after 768 ticks. Later windows after wrap adopt on their
  original deadlines with exact ordered MIDI (one bar-spaced On/Off pair per bar), role/storage/source
  identity intact; no inline mutable build at adoption. Existing even-bar fixtures remain unchanged.
- Prove behavioral RED with fixtures before changing production. Focused command:
  `pio test -e native -f test_runtime_working_set_ownership` (logs in session scratch).

## Geometry / decision review
EventWindowCursor::begin (src/Loop/LoopSnapshot.cpp) explicitly refuses length > loopLength-start;
it cannot read a wrapping window. Extend the existing Track owner with loop-local two-bar starts
and a clamped terminal span, not a new reader or whole-loop buffer. Frame boundary/adoption/expiry
checks must derive the same terminal span. Position pricing must include the terminal bar's pinned
membership/read/retirement units; retry budget counts ceil(length/two-bar-span) intervals.
DEC-156 — committed future-occurrence positioning remains authoritative: the bounded occurrence
search and chosen absolute deadline are unchanged; no service constants, capacity, source currency,
role ownership or crash protection changes. This is local geometry extension, no ownership transfer
or formal reassessment trigger. Authority read: PROJECT_INTENT, ARCHITECTURE_RULES, Playback,
current shipping tasks/current scope, DEC-156 and the existing fixed/bridge execution packet.

## Verified trace / results
- Behavioral RED before production edits: `odd-bar-red.log` in session scratch reports launch
  `Expected 3 Was 0` (Serving vs Idle at the actual switch consumer), and ordinary requester
  `Expected 3 Was 1` (Requested vs Excluded). The other 243 fixtures passed; PlatformIO reports
  ERRORED/SIGINT after the two assertion failures. Neither failure was a compilation/setup failure.
- Production now accepts whole-bar lengths, aligns lookahead on the loop-local two-bar grid,
  clamps coverage at the loop end, validates/adopts/expires that exact span, includes terminal
  pinned-bar work in the conservative maximum and counts 17 retry intervals for 33 bars.
  The source/role/generation predicates and DEC-156 absolute-occurrence transfer are unchanged.
- Verified production trace with interference enforcement enabled: request at bar 26; immutable
  terminal [bar32,bar33) adopts at origin+24,576 and expires at origin+25,344. Successors adopt
  at origin+29,952 (bar39, phase6) and origin+36,096 (bar47, phase14), each for 1,536 ticks.
  Ready role/storage generation becomes the serving role/storage generation at the unchanged
  promised tick, with current snapshot/revision identity and no mutable rebuild on adoption.
  Per-tick ordered emission across bars26+1tick..51+48ticks is exactly 51 rows: 25 Ons and
  26 Offs, pitch48, no duplicates/missing/foreign rows, including fallback gaps and phase-zero wrap.
  MIDI timing is checked by per-tick capture-count deltas; stored event.tick is source-coordinate
  metadata, not a send-time oracle.
- Additional wrap-position fixture requests at bar27: lookahead adopts phase0 at origin+25,344,
  not phase1 from an infinite two-bar grid; On at wrap and Off48 ticks later each occur once.
- Existing gate-row refusal fixture now uses a fractional-bar length instead of an odd whole-bar
  length, preserving its unsupported-geometry oracle. Existing even-bar behavioral tests retained.
- Final focused command PASS: **246/246**, 16.290 s, exit0; log
  `/Users/eelkejager/.pi-desktop/scratch/67e348ef-3368-4a38-8ec6-20a53a10e747/odd-bar-green-final.log`.
  Scoped `git diff --check` PASS. No hardware, uploads, full native or builds run by this slice.
  Parent retains full-native/build/RAM1/review/device gates; no continuous-coverage or physical
  latency qualification is claimed, and no source/doc outside the delegated paths was modified.
