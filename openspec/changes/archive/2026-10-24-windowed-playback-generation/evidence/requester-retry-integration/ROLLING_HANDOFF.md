# Gated current-root rolling lookahead/handoff — execution packet

User-approved slice after dependency correction. DEC-108 owners/identity/readiness;
no capacity, span, quantum, gap,H or margin changes. Initial launch unchanged.
One assessment returned REVISE with4 concrete draft defects. Rejected:4-bar span,
dropping refused demands, ambiguous consumer, unspecified gate. Revised author
text was not independently passed; this executable contract pins the corrections.
No claim that the plan factory passed; no additional broad review loop.

## Contract and callers (before firmware edits)
`main.cpp` PlaybackViewMaintenance turn calls `Track::serviceRollingPlaybackWindow`
for each track before offers; native calls the same Track-level symbol because
main.cpp is not native-linkable. Request only for ordinary active committed long
loops with prewarmed initial-window roles and current source; no allocation or
source scan in this prologue. Existing positioned request/qualification/order/
completion/retirement remains the producer. One runtime frame records stage,
clock epoch, operation generation, loop geometry, projection origin, selected
start phase, original absolute deadline and last observed consumer tick.

Selector uses the existing declared basis `requiredGrains()*(denials+1)*gap +
leadTicks` ONLY as a first lookahead probe (4352ticks today), rounded up to an
aligned two-bar boundary. Actual admission still prices measured candidate work
at H8, even in a one-track fixture. Span is1536, never3072. At phase0 the target
is[4608,6144); initial[0,1536) then fallback[1536,4608) -> immutable target ->
fallback. This is non-contiguous lookahead integration, not continuous capability.
No old demand is overwritten, dropped or re-positioned by the selector. If one
exists, return Busy; a refused owned request keeps its original deadline and
becomes Blocked, with fallback still audible. No automatic new allowance.

`Track::playCommittedLoopMidi` calls `resolveRollingPlaybackWindow` after initial
launch selection and before generic stale-cache handling. At the exact absolute
boundary: require ready complete PRIVATE candidate, exact admitted frame/interval,
current revision/generation/geometry, same epoch/projection, retired source pins,
no invalidated/late completion, and the preceding tick serviced (otherwise keep
fallback). Adopt using existing lifecycle and seed nextEventIndex=0; existing
crossing predicate excludes previously emitted events. No whole-vector seek,
closure-plan scan, allocation or sort at adoption. Same revision at entry preserves
held notes; later revision changes do not tear the already-bound copied interval.
Generation/epoch/geometry invalidation rejects further use. Existing fallback
rebuild/reanchor handles exit; reset its cursor seed before fallback if necessary.
Empty served windows advance playback position without emitting/closing notes.

Only the off-clock owner cancels a rolling episode on stop/slot/mode exit, releasing
only matching pending/frozen/build/ready ownership. Epoch invalidity/missed boundary
blocks adoption without renewing the obligation. No arbitrary old-revision source
reconstruction, cross-revision closure completion, preview/undo enablement or
sustained timing claim; these remain gates before normal enablement.

Gate: PLAYBACK_WINDOW_ROLLING_ENABLED defaults0 in RuntimePlaybackView.h. A value1
requires native or an explicitly marked SESSION_CAPTURE qualification build;
normal firmware envs define neither. Only the ownership test TU defines1 before
project includes. Other suites cover default0. No platformio.ini change/upload.
Metadata updates use the existing PRIMASK save/restore pattern; source traversal,
ordering and retirement never run in that fixed-size metadata section.

## Fixed oracle / checks
Existing real queued-launch fixture -> real automatic selector -> bounded worker
with monotonically advancing native deadline clock -> actual updateAllTracks
boundary/emitter. Exact known bar-spaced NoteOn/Off trace across both crossings,
no mutable build at adoption, held-note/empty case, duplicate tick, natural wrap,
stale source before boundary, missed boundary, epoch/geometry invalidation, old
demand preservation and stop/slot cancellation. Adjacent two-bar admission remains
negative under the unchanged contract; positive later target uses real lead.
No fabricated ready publication, no H1 override, no test-only timing budget.
Focused ownership/prewarm, full native, capture/RAM1, gate compile rejection,
strict OpenSpec/diff, scoped mutations with byte-identical restoration.
No task3.7/3.10 completion or device qualification from this slice alone.
Status: bounded slice implemented and natively verified; enablement and continuous
qualification remain gated. No full-task checkoff.

## Execution results and limits

**Gated rolling request → preparation → validated handoff → emission verified.**
`serviceRollingPlaybackWindow` runs off-clock before offers; the existing bounded
worker prepares the selected current-root interval; `resolveRollingPlaybackWindow`
validates absolute deadline/epoch, generation, geometry, revision, prior serviced
tick, completion and retired pins before adoption. The existing emitter/ledger is
reused; physical view/storage generation guards prevent reuse, and empty windows
preserve held notes. Refused original debts and unrelated newer demands survive.

`PLAYBACK_WINDOW_ROLLING_ENABLED` defaults0; enabling requires native or explicit
capture qualification marking. The ordinary `platformio.ini` is unchanged.
Capacity2048, span1536, fill64/order48, gap64,H8,margin768 remain unchanged. At
phase0, the selector targets [4608,6144): fallback covers [1536,4608) and resumes
on expiry. This proves later-window handoffs, NOT contiguous two-bar service.

Verification: focused329/329; full native2,779 passed/7 skipped/0 failed; three
mutations caught/restored; default and explicit qualification capture builds pass
RAM1 floor840 (padding1,640/984). Runtime metadata +48 external bytes per holder
(target7,120/host9,144), no event-buffer growth. No upload/commit. Native covers
Track caller/worker/emitter; main wiring is build-verified, not native linked.
Continuous timing, device qualification (including masked flash-boundary duration),
old-source reconstruction, preview/undo, full recovery/closures and paged enablement
remain open. Tasks3.10 service qualification and3.7 fallback removal stay unchecked.
Evidence: `ROLLING_HANDOFF.md` and `rolling-handoff-verification.json` in
`openspec/changes/windowed-playback-generation/evidence/requester-retry-integration/`.

Nine added cases cover successive handoffs with33 exact events, fallback gaps,
no mutable build at adoption, duplicate ticks, natural wrap, physical reuse,
empty-window sustain/closure, original adjacent refusal debt, stale epoch/geometry/
revision/generation, missed boundary, newer pending preservation, stop/slot exit,
queued-zero debt and generation change inside completion. The last defect was
red-proved: old-operation completion must not publish a rolling ready candidate;
the guard now rechecks before publication. Focused ownership132 + prewarm197 pass.

Mutations: late-selector, epoch guard and adoption cursor each fail the intended
fixtures and restore byte-identically (manifest carries exact failures/hashes).
Initial enabled target builds failed RAM1 padding at488 then792. Boundary-only
`rollingCandidateReadyAtBoundary` and rare `rejectRollingFrame` moved to flash;
steady resolver remains ITCM. Final qualification padding984 exceeds840 without
rebasing. This placement is not a duration measurement; masked boundary duration
remains device work. The unmarked-enabled compile fails with the expected gate
error; preliminary probes with missing MIDI/mbed includes were setup failures,
not evidence for that gate. Target/host size probes measured7,120/9,144.

Full native:2,786 total /2,779 passed /7 skipped,130.451s. Default capture build
4.954s; qualification build4.647s. Images and exact RAM1 figures are in the manifest.
Neither image was uploaded; the last device image remains the earlier rootless fix.
Strict OpenSpec and whitespace checks passed; final docs are checked separately.

Continuous two-bar remains infeasible under the current declaration: H8/gap64
supplies3 turns/holder/1536ticks, versus5 empty turns and1,191 dense lifecycle
turns. This slice does not hide the mismatch with longer spans, smaller capacity,
renewed deadlines or denser-service promises. Cold root preparation remains~28s.
No second plan-assessor PASS or independent final-code review is claimed.
