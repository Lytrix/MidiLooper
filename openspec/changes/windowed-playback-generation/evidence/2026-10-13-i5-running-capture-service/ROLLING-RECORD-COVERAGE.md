# I5 — subsequent live Record coverage

Status: implemented and locally verified; hardware NOT RUN and physical readiness/all-route migration remain open. User “continue” authorized the continuation after connected first-bar result. Active change ready/spec-driven29/60; branch feature/unified-rolling-playback-path/HEAD26125ceb plus preserved dirty tree. Extends 3.15a–c — one rolling owner/reader, DEC-116 — one rolling path, and DEC-133 — connected first-bar Record. No parent checkoff.

## Scope and dependency closure

Owner: existing Track requester/offers/RR/stepPlaybackViewMaintenance/current-ready-build/clock-edge fixed reader; Loop accepted Capture/raw prefix; existing frozen Stop job. Primary files TrackPlaybackWindowBuild, TrackPlaybackHotPath, TrackCaptureStopCommit, existing runtime/Fill only if concrete coverage qualification requires it. No new owner/view/queue/emitter/Track field, hot allocation, Clock delay/reorder, input policy, default activation, budget relaxation, Git delivery or device action.

G0 — subsequent coverage discovery PASS, session scratch i5-growth-discovery.md: existing raw Fill can filter arbitrary intervals and lifecycle rotates sequential windows. Included dependencies are current interval geometry/adoption and playback-origin handoff, exact private geometry cancellation/rebuild, bounded next-interval retention and interval-qualified Stop matching. No timing instrumentation project or re-opening completed prerequisites.

## Transition oracle

1. Every live view remains one-bar (768tick) explicit raw coverage; its period is a positive bar multiple. First-bar behavior remains compatible. Desired live period grows monotonically to include accepted raw extent (bar-rounded maximum tick+1); predicted presentation length does not redefine raw projection or force growth without new content.
2. New period may become authoritative only at a full wrap of the current served period. That adoption reanchors playback phase0 at the actual boundary, together with exact pending/live frame origin; raw Capture start, storage coordinates, NoteIds and input timestamps do not change. New period must never silently derive phase from an incongruent old anchor. Before adoption current role/storage/token/bytes/cursor authority stay unchanged.
3. Within a multi-bar served period, prepare the next one-bar raw interval through the same requester/offers/RR/maintenance. New content for a larger period cannot replace an already owed within-period successor. Coalesce newest token on that interval, keep oldest owed boundary/miss accounting, then offer growth at the full-cycle wrap. Ready must match current request token+coverage+period+operation/lease/source; never extend private Fill/order across changed coverage geometry. Same-coverage certified appends retain useful work.
4. Real ClockManager/USB input fixture: source On64/Off96 pitch60; initial replay in one-bar period; second-bar On832/Off864 pitch62 grows next period to1536 at boundary1536. Current copied first interval emits source64/96 at1600/1632. Third-bar On1600/Off1640 pitch64 arrives while second interval remains owed; interval[768,1536) still adopts at2304 and emits source832/864 at2368/2400. At full wrap3072 period becomes2304 with explicit origin3072; bars0/1/2 emit source64/96 at3136/3168,832/864 at3904/3936,1600/1640 at4672/4712. No modulo-folded third-bar rows in earlier views, exact MIDI/channel/velocity/NoteIds and ledger0→1→0, duplicate ticks silent.
5. Crossing-note control: sourceOn740/Off800 shares NoteId across adjacent raw intervals; reader must keep ledger sounding at the interval edge and consume matching Off in the next interval, without synthetic closure/duplicate On. Empty intervals are complete silence, not failure. Nonzero Record origin and phase wrap use raw relative ticks and an explicit playback anchor.
6. Matching Stop may transfer a complete served subinterval of a multi-bar period when its source token, final geometry, raw projection and closure match the frozen job, without a full raw walk or full-take-count requirement. Preserve physical role/storage/cursor/ledger. Geometry/token/closure mismatch continues via corrected frozen-source ready and retains source through root-before-edge/release; never relabel served content.
7. Withheld maintenance/late ready cannot adopt mid-interval, cannot skip an owed distinct interval, cannot reset miss allowance by coalescing/growth. Preserve valid prior version where its coverage applies; missing coverage is a named miss/refusal, not partial output or legacy-as-readiness. Source/epoch/slot/transport cancellation retains existing lifecycle contracts.

## Boundary outcomes and verification

Existing Waiting/Serving/Rejected, GrainServed/BuildComplete/GrainDenied and named lag/deadline outcomes remain; no universal enum. Mutation only under exact existing preparation/public-call/physical-generation protocols. Loops stay interruptible; no content-dependent whole-call PRIMASK guard.

Fixture first: add ordinary real-input/ClockManager growth tests and reproduce behavioral RED on initial-interval-only baseline. Use hardware-boundary stubs only and explicit controlled owner opportunities (not main cadence/WCET). Then focused ownership/prewarm/input, ASan/UBSan, discriminating scratch route-removal controls, fullnative, default+explicit rolling qualification builds and measured ABI/RAM1≥840. One focused independent lifecycle review, fix/retest concrete findings without repeated broad review. Record commands and results here before any parent checkoff.

Hardware/default/timing non-goals: physical target continuation remains compile-rejected and batch clocks can cross the edge before main. Native controlled opportunities do not prove complete-owner/IRQ/arrival/calibration or eight-holder service. No target activation or hardware claim without actual registered managed recipe/gates; no repeat default-OFF compatibility milestone. Scope remains authorized scratch8/4–5 only for any later separately prepared device leg, no SD cleanup.

## Execution evidence

### Implemented path

`retainLiveRecordDemand` retains one-bar raw intervals over monotonic bar-rounded accepted extent. A later interval remains owed when newer content extends the period. Exact source+coverage+period publication/adoption uses the existing private builder/current/ready roles; geometry changes cancel/rebuild instead of extending the wrong private coverage. Clock-edge growth reanchors Track playback origin, pending recipe and frame together at the full served wrap, without changing raw Capture start or input coordinates. Reader retains next demand at its edge so withheld main cannot erase the owed deadline; missing live coverage never falls through to legacy emission.

Matching Stop now accepts a complete one-bar subinterval of the final multi-bar period with exact frozen token/projection/closure, preserving current physical view/cursor/ledger. Mismatch uses corrected frozen source/geometry. Concrete Stop blockers found and fixed: successor publication previously lost the retired idle role when no ready shell existed; it now reclaims that role without adding a view. Frozen preparation now permits the same successor publication for a matching-period Capture current, not only geometry mismatch.

The single focused lifecycle review found a genuine retrigger: On736 prepared before768 sounds at1504, then a longer period could resend that same still-open identity at2272 before Off800 at2336. A new normal-maintenance witness reproduced expected1/actual2 NoteOns. Qualified live Record ledger admission now checks the existing bounded identity set (`isActive(channel,pitch,NoteId)`) and suppresses only a still-open identity's On, retaining its original Off. No global committed/overdub behavior or ledger layout changed. Retest and removal control pass; no repeated broad review.

### Functional and resource verification

- Initial unchanged-firmware real-input baseline: two behavioral REDs at expected period1536/actual768;41otherinput casesPASS (`i5-growth-baseline.log`).
- Eight ordinary new cases: multi-bar growth/oldest owed coverage, withheld-main crossing-note recovery, matching subinterval Stop, mismatching corrected geometry, late-ready/no-mid-traversal adoption, nonzero raw origin, complete empty middle interval, and normal-maintenance no-retrigger. Explicit expected MIDI source ticks, channels, velocities, NoteIds, ledger closure, current identity and duplicate dispatch remain asserted. Production lane passes actual sampled absolute tick as main does; a test-only zero-phase argument was corrected during diagnosis.
- Focused ownership/prewarm/real-input **500/500PASS** (`i5-growth-focused-final.log`,27.459s), including original first-bar/Stop/lifetime regressions.
- Full `pio test -e native`: **3275passed/7skipped/0failed**,3282cases,364.131s (`i5-growth-full-native.log`).
- ASan/UBSan **8/8PASS**, `detect_leaks=0` (`i5-growth-sanitizer.log`); no leak claim.
- Three scratch-only controls remove growth, idle-role retention or open-identity suppression; each fails its intended period, Stop-build-role or duplicate-On assertion (`i5-growth-controls.log`). Live source hashes unchanged.
- Default `pio run -e teensy41-capture-serial` SUCCESS100.664s: RAM1variables96192/code392056/padding1160≥840/locals34880, RAM2163392/EXTRAM11680 (`i5-growth-default-build.log`).
- Same env with scratch config retaining every base flag plus rolling-enabled/qualification defines SUCCESS99.068s: RAM1variables96192/code392264/padding952≥840/margin112/locals34880; RAM2/EXTRAM unchanged (`i5-growth-rolling-build.log`). No budget relaxation.
- Compiler-measured ABI unchanged: ARM runtime7872/builder264/Fill168/Track384/Loop1008/request168; host runtime10056/builder408/Fill240/Loop1600/request176. No new persistent field, view, allocation, manager, queue, resource pin or Track DTCM growth. The identity lookup adds no stored state.

### Exact retained images and hardware disposition

Session scratch `i5-growth-default-image/`: HEX `b1560ca3ad42d4775c79d9389ddfdc739d99be82d4769b313666f0df1d875b50`, ELF `9a65a90c23c2d5d8ce384722cdcafebd5d1fbf13ad33fc71d905c81017fb4a5e`. Enabled `i5-growth-rolling-image/`: HEX `0abb01a2a670c7e46c22b668ff38fe680d33f36d38c797d7cc8583cf9b27a796`, ELF `a7f1c346259b8b713f7055cda11fc6ce00ccd2742a0cce3f04c7a222cfc050c3`. Neither uploaded. Installed acquisition default-OFFf48aca66… unchanged; no device/serial/SD/Git delivery action.

This extends beyond the former first-bar-only functional limitation. It does **not** qualify finite total source inspection: per-grain work remains charged, but total live raw inspection still depends on retained extent. Target continuation remains compile-rejected; queued Clocks can cross the adoption edge before main gets a maintenance turn. Complete owner/IRQ costs, wire arrival/calibration and dense eight-holder/device behavior remain unqualified. Functional controlled8turns-perClock scheduling is not production cadence,740→768 proof or WCET. Hardware NOT RUN; the existing prepared recipe is not a registered/executed growing-Record device leg. Keep default rollingOFF, all-route parents unchecked29/60 and legacy retirement separate.

### Session closeout

Scope/transition oracle preceded firmware changes; original twoRED and reviewRED retained, all new cases ordinarily registered. Existing owner extension only, no reassessment trigger. Runtime/current work/tasks/design/handoff/ledger guide/tracking/decision updated to cite this current result; older first-bar-only limitations remain historical. No commit/push/PR/archive requested or performed; accumulated unrelated changes preserved.
