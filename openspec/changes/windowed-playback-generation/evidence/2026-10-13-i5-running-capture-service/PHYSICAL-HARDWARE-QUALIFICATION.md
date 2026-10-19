# I5 — PIT hardware qualification execution packet

## Identity and scope (fixed before code/device work)

User requested hardware qualification of startup/latching, clock accuracy/read latency,
and physical-clock correlation after DEC-128 — ClockManager PIT elapsed reference.
Branch `feature/unified-rolling-playback-path`, baseline `26125ceb` plus preserved dirty tree.
Status: DEVICE VERIFIED for the pinned stopped-only functional scope below; absolute calibration, running correlation and whole-path deadline qualification remain open.

Existing owners: ClockManager owns observations; DebugSessionCapture owns delivery;
StorageManager::processHitlSerialCommands owns the existing serial extension point;
managed HitlConfig/DeviceSession owns capture, latch, twoStop/drain and postflight.
No owner transfer, new persistent timer/queue, hot allocation or production activation.
Decision review: searched DECISION_LOG for PIT/physical clock/DEC-128; reuse YES.
No formal reassessment trigger, migration or SD format change.

Only `SESSION_CAPTURE` (and the isolated native fixture) expose request-only observations.
No new automatic/main/ISR polling. Producer itself stays unchanged. Natural carry is
observed, never forced by preload/reconfiguration. No debugger/sleep qualification.
No Start/Continue, Record/edit/clear/Save/Load, slot selection or SD cleanup commands.
Stopped point correlation does not prove running due-time, wire age or deadline admission.
Parent I5 — running-Capture readiness remains open, progress29/60 unchanged.

## Oracle and HITL test recipe

| Field | Pinned value |
|---|---|
| Acceptance | Fresh Ready/nonzero-era PIT0/1 with MIDI timer coexistence; monotone coherent observations through natural first low carry; nominal-relative rate gross sanity; production read latency observations; stopped musical point bracket |
| Reused flow | Managed `host_midi_hitl.py run`, active_managed_capture, fenced commands, existing MIDI session and canonical teardown; stimulus list inspected (no counter-only preset) |
| New stimulus | TEMP PIT counter-only qualification; dedicated `pit_clock` preset, no canonical edits/promotion |
| Gestures | None; only `!PIT_OBSERVE <uint32 id>`; managed preflight/reset/teardown retained |
| Marker | `#CAP,<micros>,DIAG,pit_obs,...`, Tier A, one/request, nominal1Hz, ClockManager observation → DebugSessionCapture emitter; immediate USB room else ring |
| Tier check | Source-central prefix, native header/ring boundary tests, marker contract required before upload |
| RAM1 | FLASH code/PROGMEM strings, gated, no budget relaxation; before padding1688, after NOT RUN |
| Regression | Actual ClockManager observation in existing physical hardware-only fixture: mask preservation, latch/low carry, invalid/era refusal and no peripheral writes |
| Smoke | Dedicated managed fresh boot, `--pit-duration-s 3`; expect valid fresh `pit_obs` rows, SMOKE_PASS only |
| Full run | Dedicated managed fresh boot, default210s,1Hz; duration<=240s within managed360s watchdog |
| Missing/displaced | Missing/duplicate/invalid rows = unqualified; capture loss not proof of absence; no exit-code-only PASS |
| Evidence | Build identity, raw serial path, all samples with host monotonic brackets, oracle report, shutdown2drains/latch result |

Startup requires first elapsed raw ticks <60 nominal seconds, Ready=2, nonzero era,
configuration mandatory bits `0x27f` and highTIF bit8 clear. Control2 must be TEN|TIE=3,
control3 disabled; allones loads, MCR FRZ1, pair0/1 TEN/CHN, clock gate/select and debug
configuration checked by direct register reads. LowTIF bit7 is expected sticky after carry.

Every point is bracketed by two validated production physical reads under one preserved
outer interrupt mask, with raw lifetime high→latchedlow and actual extended musical tick/
epoch between them. Same nonzero physical era, rawTicks/24 inside [beforeUs,beforeUs+widthUs],
unchanged stopped transport/musical epoch. The extra raw read is observational and cannot
publish validity: both validated samples must agree. Native fakes replace hardware only.

One independent production `getPhysicalClockStamp()` read per request is bracketed by
DWT cycle reads with the incoming interrupt mask unchanged. DWT enable and actual CPU
frequency are reported; min/median/p95/max are observations INCLUDING possible IRQ costs,
not WCET or deadline bounds. No measured distribution threshold is fitted after a run.

Natural first low carry at2^32 ticks (~178.957s nominal): adjacent observed high words
increment and low words decrease, no backwards/fault/era change, lowTIF asserted afterward.
This is a functional coherent-carry test, not a forced high/low read straddling the exact edge.

Rate: before each managed command send record host monotonic_ns; receipt of the matched
end fence closes its bracket (includes USB/process/poll latency). For first/last brackets
[s0,r0], [s1,r1] and raw tick delta D, rate interval is [D/(r1-s0),D/(s1-r0)] when positive.
The interval must overlap predeclared23.9..24.1MHz: gross relative functional check only
(~0.42%), not ppm calibration, a calibrated host reference or absolute oscillator accuracy.
Report bracket uncertainty, not nominal24MHz as measurement.

## Verification and completion

Required: focused observation/tier/ring native, host parser/oracle/fence tests, marker check,
fullnative, default `teensy41-capture-serial` build/RAM1, one focused independent probe/scenario
review, fresh managed device preflight/image pin, dedicated smoke, then full carry run.
Dedicated HITL upload exception applies after successful local checks/build/recipe. No SD
mutation authorization inferred. Keep serial owner open through canonical final drains.

All verification: NOT RUN for this slice at packet creation. Hardware evidence and actual
counts will be appended; no parent/task checkoff from prerequisite observations.

## Startup diagnostic continuation (2026-10-14)

First managed no-reset preflight completed2drains/nolatch/freshstoppedoverlayidle.
Dedicated image eaa0a0d5b383c5eb9ef54bf9e880e3c9d427823bf09b1b4c49680a22dc189bec
uploadedSUCCESS/RAM1unchanged. First smoke attach refused before scenario while boot
restore progressed; complete2drains/nolatch. Smoke02 freshbootcrash0 reached marker:
`#CAP,8147732,DIAG,pit_obs,0,4,0,0,0,0,0,0,10096,600000000,0,0,0`.
Status4 ConfigurationInvalid/era0: NOT qualified, no carry/rate/latency/correlation proof.
Extend same probe to show configuration predicate bits also on invalid rows, with
peripheral access conditional on enabled bus gate. No new marker or producer policy.
Oracle unchanged; invalid row never gains a nonzero identity from diagnostic bits.
Review found ring fallback can follow its end fence; repaired bounded host completion
to require both exact fences and unique request-ID row, bracket closes after both.
Actual128Broom start/observation/end native sequence and host delayed/wrongID/duplicate
tests pin repair. One review only; do not reopen producer resource policy.

Smoke03 diagnostic imagebd4ba15dfdae2be1ad38fe19ddc816d14c614beb5e897df5cf103ee98eeb3dbb
reports status4/era0/config97: moduleFRZ1, clockgate/selection valid, control3disabled,
debug predicate false, no reservedpair. Next same-row config diagnostic bits10/11/12
name DHCSR C_DEBUGEN/S_HALT/S_SLEEP respectively; mandatoryvalidmask unchanged0x27f.
No clearing/weakening/debug-policy change authorized or performed. Full carry run
must not start until producer Ready; all measurements remain unqualified.

## Owner-approved guard correction (2026-10-14, before code)

Smoke04 image21d6a5e7a7fe0351a5cc0c6e4af024141f37db699c044e2e1d16e1ce00bcf835:
`#CAP,12090666,DIAG,pit_obs,0,4,0,0,0,0,0,0,3221,600000000,1121,0,0`.
DHCSR C_DEBUGEN1/S_HALT0/S_SLEEP0 on runningboard; config97+1024, source neverreserved.
The user explicitly approved narrowing the guard to halt/sleep refusal, not C_DEBUGEN
alone. Preserve FRZ1/Runmode-only/unsupported debugger/sleep continuity; do not clear
debug registers. Cause of C_DEBUGEN remains UNKNOWN (not proof of attached debugger
or bootloader causality). Add actual running-enable native RED before mask edit;
retain halt/sleep refusal tests and all previous configuration/overflow/era guards.
No counter/lifetime restart/queue/clockrouting policy change. Mandatoryconfigmask
still0x27f; C_DEBUGEN bit10 is diagnostic, permitted in Ready rows. Next freshboot
smoke, then bounded210s naturalcarry/rate/readlatency/stoppedpoint run if startupReady.

## Completion evidence (2026-10-14)

### Checks and corrective evidence

- Initial focused run: clock/tier passed; new ring fixture failed because it used
  `flushCaptureBuffer(0)` (no-USB timing-critical path). Corrected to existing idle
  drain64, not a production behavior change. Restored focused80/80 passed.
- Displaced raw sample +2400ticks: two specific native bracket assertions failed;
  removed control restored original observation anchor A9D3 byte-identically.
- One focused review `caa61352-fe44-4a3f-b2f9-8247c0976092` found delayed ring row can
  follow end fence. Concrete fix implemented and retested, no broad review restart.
  Host completion now requires both exact fences and unique request-ID row before
  closing its time bracket; actual constrained128Broom native emitter sequence,
  missing/wrongID/duplicate/timeout host controls pass.
- Owner-approved debug-enable correction: genuine pre-edit running-C_DEBUGEN case
  failed at Ready; after mask change passes while S_HALT/S_SLEEP each still refuse.
  No debug-register clearing, FRZ change, counter reset or clockrouting change.
- Final focused `pio test -e native -f test_midi_input_clock_integration -f
  test_capture_ring -f test_capture_line_tier`:84/84 passed.
- Final full `pio test -e native`:3203total,3196passed,7skipped,119suites PASSED,
  zero failures,134.133s. Optional-ingress native actual-clock suite33/33 passed.
- Host `PYTHONPATH=scripts .venv/bin/python -m unittest test_pit_clock
  test_hitl_live_observation test_hitl_stimulus_contract test_hitl_stimulus_authorization
  test_stimulus_yaml_view`:102/102 passed. New PIT tests22; narrow broker grammar tested.
- Marker preflight: emitted in DebugSessionCapture, source-central TierA prefix
  `DIAG,pit_obs,`13/31bytes, maximum row fits192B. Strict OpenSpec valid.
- Default capture build SUCCESS: variables96192,code391528,padding1688≥840,
  locals34880,12banks,848B margin; RAM2vars163392/EXTRAM11680 unchanged.
  Observation/emitter/cycle seams and format linked in FLASH; no new resident state,
  hot poll, allocation, working-set member, numeric-budget or installeddependency edit.

### Device identity and lifecycle

Managed no-reset preflight `captures/i5-pit-preflight-01` observed fresh track7/slot3
STOPPED/transport0/overlayclosed/phase0 and completed2drains/nolatch. Its cached image
hash was explicitly not treated as installed-image attestation.

Dedicated smoke05 and full01 use uploaded default capture HEX
`9b50680ad750fd46656d124ddfb194ecc60a86f740d098fa04e1c029161b770c`, unchanged after run.
Git `26125cebb08980342ba56ed2b77e5397783ac8e5` plus preserved dirty tree.
Prior failed smoke01–04 retained: first attach refused during restoration; later rows
isolated C_DEBUGEN false refusal, not successful clock qualification. All recorded
shutdowns complete2drains, no latch. Smoke05 reports SMOKE_PASS, not carry qualification.

Full authoritative invocation (no captured command replay):

```sh
.venv/bin/python scripts/host_midi_hitl.py run --preset pit_clock \
  --track 8 --loop-slot 4 --midi-channel 8 \
  --capture-port /dev/cu.usbmodem154944801 --capture-boot-wait 30 \
  --boot-reset --out-dir captures/i5-pit-full-01
```

Default210s/1Hz under unchanged360s managed watchdog. Exact invocation/metadata/raw
samples are retained in that run directory. Report `pit-clock.json`:PASS/oktrue,
issuesempty; actual211/211 request-ID-matched rows, all Ready/era2/CPU600MHz.
One distinct fresh boot timestamp2164003/crash0 (duplicate capture replay of same
timestamp is not a second boot). No loss markers observed; no RINGstats row was
present, so no global ring-loss-free assertion is made. Every required row is present.
Canonical final2Stops/2verifieddrains, postflight complete heap observation and no latch.
Only read-only observations plus managed reset/Stop/drain; no Record/edit/clear/Save/Load,
slot gestures or SD cleanup/destructive commands. Ordinary boot/restore behavior retained;
no byte-for-byte SD immutability claim. No Git commit/push/PR/archive.

### Measurements and honest scope

- Startup first raw51529336ticks (~2.147s since counter reservation), nonzeroera2,
  config1663=`0x27f` mandatory bits+C_DEBUGEN diagnostic. PIT0/1 pair and MIDIchannel2
  coexistence observed, channel3disabled, highTIFclear.
- Natural first low carry between IDs176/177:
  raw4275339988→4294973643 (high0→1; low4275339988→6347), no backwards/fault/era change.
  Config1663→1791 adds sticky lowTIF128, highTIF remainsclear. This verifies observed
  coherent carry, not a deliberately forced high/low read crossing the exact carry instant.
- All211 points: rawTicks/24 inside validated physical before/after bracket. Width
  min7/median15/max22µs. Actual musical tick0/epoch1/sourceinternal/transportstopped
  unchanged. This is stopped point correlation, not moving musical-clock correlation,
  internal due-time/external arrival provenance or deadline-health qualification.
- Host enclosing request brackets69.742–821.723ms; first/last formula yields relative
  rate interval23967713.816681–24058947.762814Hz, overlapping predeclared23.9–24.1MHz
  gross acceptance. No absolute calibration, calibrated host oscillator or ppm assertion.
  Thresholds were not fitted after observing data.
- DWT-bracketed actual production read,211samples at reported600MHz: min11.970µs,
  median29.555µs,p9531.391667µs,max34.090µs. Includes barrier/call and possible IRQ costs;
  observed distribution only, not WCET, hot-path reservation or complete owner-to-input cost.

### Artifact hashes and remaining gates

Full report SHA256 `669dcf03be2236c316b60387e098b017af9f2f5521bb4f0e7d4207e150261c65`;
metadata `c10408c7401b038c8af59ac1f2755f03e9d399df3ac01c1d56541c4373424a40`;
shutdown `7e612fed5f3e9923c390d6a4adc035cded5176b4e542126ffe1a54a26eae617f`;
serial `session_20261014_142946.log` SHA256
`24eaa4fc7eae404841aef97f1b0760a93b9b985c2103eab2eb35144418118c16`.

Session closeout: runtime/currentwork/tracking/tasks/design/guide/decision/handoff
updated. DEC-129 — Run-mode physical-clock debug admission records approved correction.
No parentcheckoff:29/60,rollingOFF,targetcontinuationdisabled,receiptsdefaultOFF.
Moving-clock correlation, external arrival age/pacing, absolute calibration, WCET/whole
owner-input/IRQ costs, relevant errata qualification and source ISR/main protocol remain
open. Running request/anchor/coalescing/adoption/stop still gate740→768 readiness.

### Final cached-build distinction

Optional-ingress native flag switching removed the target cache. The final default
clean rebuild succeeded98.038s with identical RAM1 figures, but generated HEX
`f3d02128cb32ca093dc4ee91e44d48d3606aa2b5404a162a22b9a3cb2b3d9581`, NOTuploaded.
RtcTime.cpp embeds __DATE__/__TIME__; clean builds are time-dependent. The earlier
HEX was not retained for exact byte comparison, so the entire difference is not
attributed/proven to be only timestamp bytes. Hardware evidence remains pinned to
the successful upload9b50680a…b770c, verified unchanged immediately after full01
before this rebuild. Do not substitute current cached artifact for that device image.
Final symbols in FLASH: observation60001e9c/size0x270, emitter6008d5c0/0xc0,
format600bd3dc/0x4c, DWTread6008b4d8/0x14, CPUclock6008b4ec/0x24.
Producer.cpp/ClockManager.cpp/main.cpp source hashes remained respectively
eb9f653f…cfd8d/52a7b09a…c5b9a/a10aad16…0708a; guard correction is the shared header
constant only. This is not a whole-tree or installeddependency change claim.
