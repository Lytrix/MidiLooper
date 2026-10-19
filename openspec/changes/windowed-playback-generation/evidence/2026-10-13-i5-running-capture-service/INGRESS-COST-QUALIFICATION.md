# I5 — ingress-cost arithmetic qualification

## Identity, discovery and scope

Status: **native verified; default build verified; no target probe implemented**. Baseline `26125ceb`, branch
`feature/unified-rolling-playback-path`, preserved accumulated dirty tree. This is a
local prerequisite of **3.10 — bounded owner service** and **3.15a — running Capture
oracle**, not their completion. Parent progress remains 29/60.

G0 — evidence: `MidiIngressReceipt` owns packet/direct observations; existing target
`IngressMask` preserves PRIMASK and native fixtures expose nesting. Generated pinned
receiver bodies and installed dependencies remain unchanged. `PhysicalClockStamp`
already represents extended time/nonzero era. DWT32 modulo subtraction cannot distinguish
an extra full wrap. USB receiver callback scope is not entire USB IRQ scope. See
[owner/IRQ disposition](OWNER-IRQ-QUALIFICATION.md) and
[native hook/mask qualification](INGRESS-MASK-QUALIFICATION.md).

G1 — architecture: extend the MidiIngress namespace with passive caller-owned types;
no resident instances, allocation, timing manager, queue, publication, transport, undo,
persistence, source-concurrency or hot-path change. Existing unbounded owners are not
reclassified. Existing DEC-125 — pinned receiver ingress receipts and DEC-128 —
ClockManager PIT elapsed reference remain controlling; this is conditional arithmetic,
not a new clock/resource/producer decision. Decision review searched ingress, timing,
instrumentation and the applicable decision entries; no equivalent cost validator found.

## G2 — execution packet fixed before code

Primary files: `include/Utils/MidiIngressCost.h`,
`test/test_midi_ingress_cost/test_midi_ingress_cost.cpp`. Header-only, no callers in firmware.
No feature/build flags, target mask/source transforms, numerical budget, marker, serial
schema, installed dependency or hardware changes. Independently native-buildable boundary.

Inputs: a cycle endpoint contains a DWT low word, reported CPU Hz, configuration identity,
nonzero physical era and extended before/after microseconds enclosing that cycle read.
Caller supplies a **continuous-counter contract** with an upper bound on counter Hz.
This contract is an explicit assumption, NOT inferred from endpoints or established by
these tests. Producer must guarantee it across the whole interval, including no hidden
reset/halt/rate excursion. Same endpoints do not prove continuity. Physical clock
accuracy/calibration remains unqualified; a future target producer must justify a
conservative bound relating its time units to maximum counter advance before wiring.

Validation refuses missing contract/rate/era/Hz/config, malformed or overlapping endpoint
brackets, backwards extended time, changed era/Hz/config, endpoint Hz above the supplied
maximum, and any enclosing duration that could include a full DWT wrap at that maximum.
Allow zero duration/cycles and one unambiguous low-word carry; use division of a 64-bit
constant to avoid multiplying unbounded extended elapsed values. No overhead subtraction,
cycles-to-time guarantee or WCET inference.

Four explicit lanes: complete hook-call bracket, conservative pre-disable/post-restore
mask bracket, publication overhead, empty-probe overhead. These are labels for future
producer boundaries; the model does not observe them or prove coverage. Independent local
(noncopyable) tokens permit nested observations without a global start slot. Begin on a
pending token refuses without overwrite. Finish consumes the token even on invalid sample;
a second finish consumes/counts nothing. Caller serializes aggregate access; no ISR-safe
publication or lock is supplied by this model. There are no global instances.

Per-lane count/min/max/sum are exact until saturation. Rejections do not alter accepted
statistics. Accepted count or sum overflow refuses that sample, marks the lane saturated,
and freezes accepted statistics terminally; rejected count saturates without wrap.
No reset API: a new caller-owned aggregate is a separate observation session.

### Oracle / coverage

1. Initial empty aggregate: all counts/sums/max zero; min sentinel UINT32_MAX.
2. Hook samples 11/23/0 cycles -> count3/min0/max23/sum34, other lanes unchanged.
3. Local outer hook and inner publication tokens -> finish inner then outer, separate exact
   sums; busy begin retains original outer start; second finish makes no change.
4. Table-driven invalid endpoints/contract -> one rejection, zero accepted stats; fresh
   valid token can recover. Endpoint-era fault consumes, does not leave pending work.
5. Low carry `UINT32_MAX-4 → 5` ->10 cycles with short enclosing bracket. Same low endpoints
   plus a full-wrap-length extended duration -> refusal, including exact threshold and
   UINT64_MAX-scale duration. At illustrative 1MHz upper rate, 4294967295us is last
   allowed enclosing duration, 4294967296us must refuse. Fixture numbers are not target budgets.
6. Count/sum/rejected saturation -> no wrapped value, accepted fields preserved, terminal
   saturation visible; min/max never change for rejected samples. All four lanes independent.
7. Deterministic generated short intervals compare against independently specified exact
   cycle counts across low carry; separate rate/era/config refusal cases.

Missing API means pre-fix compile cannot establish behavioral RED. Once linked, a temporary
removal of full-wrap refusal must fail the explicit resurrection assertion, then restore
byte-identically. No copied clock/receiver path is claimed: tests exercise this passive
production header only; target producer and actual hook wiring remain NOT IMPLEMENTED.

### Verification recipe

- `pio test -e native -f test_midi_ingress_cost` with all registered cases present.
- Native neighboring receipt, actual receiver, checkpoint and clock integration suites.
- Standalone ASan/UBSan run of this same test body; target ABI compile/pricing of passive types.
- `pio test -e native`; default `pio run -e teensy41-capture-serial`; RAM1 budget unchanged.
- One focused independent review of these new files/packet only; fix/retest concrete findings.
- Strict OpenSpec validation, relative-link and diff hygiene; record actual results below.
- Device/HITL: NOT APPLICABLE to passive arithmetic; no marker or HITL recipe authored here.
  Target opt-in middleware, actual DWT/PIT brackets, protected aggregate publication,
  probe overhead, complete hook/mask/receiver observations and dedicated managed stimulus
  remain future dependency-closed work, not implied by native acceptance.

## Completion record

- Focused `pio test -e native -f test_midi_ingress_cost`:12/12 PASS. All12 registered cases present, including16 endpoint/contract faults and1,028 generated exact short observations across four lanes.
- Neighbor command: `pio test -e native -f test_midi_ingress_cost -f test_midi_ingress_receipts -f test_midi_ingress_transport -f test_checkpoint_deadline -f test_midi_input_clock_integration`:87/87 PASS across five suites.
- Full `pio test -e native`:3226total/3219passed/7skipped/120passed suites/0failed,389.532s. New cost suite12/12 present. Latest assertions also exercised by final standalone sanitizer run.
- ASan/UBSan: same test body compiled with `clang++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DUNITY_SUPPORT_64 -Iinclude -I.pio/libdeps/native/Unity/src test/test_midi_ingress_cost/test_midi_ingress_cost.cpp -x c++ .pio/libdeps/native/Unity/src/unity.c`;12/12 PASS including review-suggested3MHz cutoff1431655765us accepted/1431655766us refused. Initial standalone invocation used a nonexistent Unity config header and failed at compile setup; corrected invocation above passed, no behavior assertion weakened.
- Scratch-only omit-wrap guard control:12tests/3specific failures (`test_low_word_carry_and_full_wrap_resurrection_refusal`, `test_exact_wrap_boundary_and_huge_span_are_not_multiplied`, `test_enclosing_bracket_not_inner_difference_controls_wrap_gate`), expected InvalidSample2 vs Accepted0. Latest assertions rerun with same3 failures. Live header never mutated; SHA256 `1eec2499093c17e171d4b3894e1d1e82b463c7eefba162978da8b66747c150f0` unchanged.
- Host source/pin/middleware: `.venv/bin/python -m unittest discover -s scripts -p 'test_midi_ingress*.py'`:30/30 PASS. Receipt implementation hash remains `40c5d4b172e5bdd39204aa3494ae9b03adc1ef10b480609e48f0bae7fdf286d7`; transform hash remains `e092b471751975cc86a59078f13d2fe01b4be7a7dc2c499f6a9666e430e592b4`.
- ARM Cortex-M7 `arm-none-eabi-g++ -std=gnu++17 -mcpu=cortex-m7 -mthumb` and host compile pricing: endpoint32B/contract8B/stats32B/aggregate128B/token40B, same bothABIs. Host `-Wall -Wextra -Werror` header compile PASS. No firmware includes/callers/instances under `src/` or other project headers; no RAM1 allocation inferred from these passive type sizes.
- One focused independent review `cecc8411-bf4c-47ec-a5ad-479d4ea9a29d`: no concrete implementation bug; nonblocking rate-dependent division/rounding coverage suggestion added and sanitizer-retested. No review restart.
- Strict `openspec validate windowed-playback-generation --strict` PASS; packet/guide7 relative-link targets resolve; final diff hygiene/resource verification recorded below.
- Session closeout: no new architectural decision, owner/resource transfer or support-envelope exclusion, so no new DEC entry. Existing decisions unchanged. Runtime/tasks/design/continuity/receiver guide/owner disposition/tracking linked to this packet. Overlap/ledger/playback ordering unchanged. No task eligible for parent checkoff.
- Target instrumentation, managed HITL, upload/device/SD/Git: NOT RUN/NOT IMPLEMENTED. No target elapsed/wholeIRQ/WCET/calibration/arrival/source-concurrency/740→768 readiness claim. Parent29/60/defaultrollingOFF/targetcontinuationdisabled/receiptsdefaultOFF retained.
- Default `pio run -e teensy41-capture-serial`:SUCCESS106.665s; RAM1variables96192/code391528/padding1688/locals34880, RAM2variables163392, EXTRAM11680. Padding floor840 unchanged/margin848; RAM1 budget PASS. Default ELF has no ingress-cost model symbols. Final `pio test -e native -f test_midi_ingress_cost`:12/12 PASS after all review assertions.
- Cached default HEX `29b3ba1e3d52021739335c812dac5512a57365b1d3615a29b33a2381a4b24a17`, ELF `514cc9f64d138cf3291b25c5d5a66bcbdf1701c3a6963ab4bde5620c17bfcc91`:NOTuploaded. Prior evaluated motion HEX/ELF remain the device evidence, not these newly built cache artifacts.
- Check logs under session scratch: `i5-cost-focused.log`, `i5-cost-final-focused.log`, `i5-cost-neighbors.log`, `i5-cost-full-native.log`, `i5-cost-default-build.log`, `i5-cost-sanitized.log`, `i5-cost-wrap-mutant.log`, `i5-cost-host.log`, `i5-cost-arm-price.log`, `i5-cost-host-price.log`, `i5-cost-openspec.log`. Exact results above are the durable record; scratch logs are session-local.
- Final diff/relative-link hygiene rechecked at closeout; no unresolved implementation blocker for this passive prerequisite. Remaining target work is explicitly not completed by this slice.
