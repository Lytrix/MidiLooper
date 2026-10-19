# ClockManager physical elapsed reference

Decision: [DEC-128 — ClockManager PIT elapsed reference](../DECISION_LOG.md#dec-128--clockmanager-pit-elapsed-reference). Contract, NXP manual/source evidence and verification: [I5 — physical producer execution](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/PHYSICAL-PRODUCER-EXECUTION.md).

## Normal operation and ownership

ClockManager owns one fixed scalar reference state and the Teensy PIT0/PIT1 lifetime-counter pair. It is a free-running elapsed counter, not another musical clock. It runs in ordinary firmware without debug logging; no periodic callback/overflow interrupt or poll loop keeps it counting. Initial reservation occurs inside ClockManager setup before its existing MIDI IntervalTimer starts. Reservation requires allfour channels disabled; it never steals a live channel or disables a foreign timer. The PIT bus gate may be enabled before register inspection. With the pair reserved, the existing MIDI timer uses channel2; channel3 is not unrestricted permission for a future unmediated timer allocation.

State is16B in explicitly initialized RAM2/NOLOAD, with32B linked alignment cost in current builds. No ClockManager member, Track/LoopPool working set, payload queue, view or publisher is added. Installed framework sources remain read-only.

## API and failure behavior

- `getPhysicalClockStamp()` samples elapsed microseconds plus nonzero era, or returns an unavailable zero-era stamp. It masks only a fixed register sequence, preserves prior PRIMASK, reads high then hardware-latched low, checks configuration/overflow before and after, and performs no hot register writes/allocation/logging. Nominal24MHz conversion floors to microseconds; this is not measured accuracy/WCET.
- `physicalClockReferenceStatus()` reports the stored lifecycle/fault reason without sampling or advancing time. Ready describes producer mechanics, not a qualified physical deadline.
- `invalidatePhysicalClockReference()` invalidates availability/era before a future supported suspend/reconfiguration. Requalification requires owner setup; an intact owned pair preserves its running count rather than resetting the shared module. Era exhaustion, full-width overflow and observed backwards count are terminal within that boot lifetime.
- MIDI timer start failure leaves the physical source unavailable even when the pair is reserved. Ordinary musical startup is still attempted when reservation fails; accepted input is never discarded to manufacture source validity.

The stock shared debug-freeze setting FRZ=1 is retained. Current DHCSR S_HALT/S_SLEEP sampling refuses; C_DEBUGEN alone is diagnostic, not proof of a halt or attached debugger. The running board reported C_DEBUGEN1/S_HALT0/S_SLEEP0, and the owner approved this guard correction during hardware qualification. Timing references across debugger halts, sleep or unmediated peripheral reconfiguration remain unsupported: register validation cannot detect an invisible stop-and-restore interval. No ordinary application sleep path is currently wired. Any future low-power/debug support must explicitly invalidate and requalify rather than assume continuity.

## Consumers and qualification boundary

In the optional ingress profile, ClockManager setup installs the existing MidiIngress sampler. It timestamps receiver observations; unavailable source falls back to labeled rawmicros32 metadata. Default receipts remainOFF. Sampler observations are not sender arrival or bounded queued-message age.

Passive `PlaybackPhysicalDeadline::admit` and `selectInputCheckpointOwner` can consume caller-supplied timestamps, but production checkpoint dispatch/correlation is not activated by this increment. Loop identity/Track retention/MIDI ordering do not read PIT directly. Rolling playback staysOFF and target input continuation stays compile-rejected.

Native fixtures verify producer/observation mechanics. The dedicated210s managed hardware run qualifies startup/normal lowcarry/sticky lowflag/MIDI-timer coexistence and stopped point brackets; the subsequent guarded motion run qualifies sampled internal/external point consistency only (below). Absolute calibration, relevant errata, IRQ/FIFO/USB delay and finite complete owner-to-input/IRQ costs remain gates. This does not qualify running-Capture successor readiness at740→768 or all-route source ISR/main synchronization.

## Request-only hardware observations

Capture builds expose `!PIT_OBSERVE <uint32 id>` via the existing serial owner;
there is no automatic poll or persistent diagnostic state. ClockManager observes a
production physical read under DWT brackets, then two validated physical samples
bracketing raw hardware latch and actual musical tick/epoch under preserved PRIMASK.
The TierA `DIAG,pit_obs` response is immediate when USB has room, ring-delayed otherwise.
The managed `pit_clock` TEMP preset requires both fences and its ID-matched row,
including a row arriving after the endfence, before closing the host-time bracket.
Invalid samples remain era0; debug/configuration bits are diagnostic, never validity.

Config bits0–6: MCR/loads/controls0–3/clock selection+gate; bit7 lowTIF; bit8 highTIF;
bit9 current halt/sleep predicate valid; bits10–12 C_DEBUGEN/S_HALT/S_SLEEP respectively.
The accepted guard correction is DEC-129 — Run-mode physical-clock debug admission.

### Verified scope (2026-10-14)

`captures/i5-pit-full-01/pit-clock.json`:211/211 Ready/era2 points over210s,
natural first lowcarry, highTIFclear, MIDItimer2 coexistence, stopped tick0/epoch1.
Host enclosing brackets give23.967714–24.058948MHz relative rate interval: gross
predeclared functional sanity only, not absolute oscillator/ppm calibration.
DWT-bracketed production read min11.970/median29.555/p9531.391667/max34.090µs at600MHz;
includes barrier/call/possibleIRQ costs, distribution only, not WCET. Stopped point
brackets7–22µs do not qualify moving-clock correlation, due-time or wire age.

[I5 — hardware recipe, failures, exact identities and evidence](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/PHYSICAL-HARDWARE-QUALIFICATION.md).
Qualified uploaded image9b50680a…b770c; subsequent clean cached imagef3d02128…d9581
was NOTuploaded. RtcTime compile-time data makes clean builds time-dependent;
never substitute a cached build hash for installed-image proof. No byteidentity claim.

## Guarded moving observations (2026-10-14)

Capture builds additionally expose strict `!PIT_MOTION <uint32 id>` and a separate Tier-A `DIAG,pit_motion` row; the old `pit_obs` schema is unchanged. ClockManager snapshots current nominal tick period under its existing preserved mask; no resident state or hot poll. The serial owner composes TrackManager's all-track Capture/pending-action guard and storage-idle getters. Before each Start the managed `pit_motion` TEMP preset requires guard15, stopped transport and a fresh closed idle overlay. This is a point-in-time bench precondition under exclusive host input, not an admission lock or source-concurrency protocol. Only existing playback/Clock actions are used; ordinary automatic persistence may occur.

`captures/i5-motion-full-01`:51 matched Ready/era2 rows including15 moving points across internal and USB-device external120/300BPM. Each moving leg has increasing tick/raw time, stable nonzero per-leg epoch/expected source and9–31µs raw/musical read brackets. Internal nominal-period residual min−408/median−131.125/max520.5µs is an observed quantized sampled-point residual, not calibrated accuracy/edge-due error/WCET. Host Clock send intervals at300BPM ranged0.005291–23.401125ms (median8.313709ms), explicitly including near-back-to-back catch-up, not bounded wire pacing. No sender/wire-arrival or receiver-loss-free proof. Internal Stop can remain queued in the first post-gesture receipt; each later Start waits for a new actual stopped/idle guard.

Exact evaluated HEX96e4160a…d6be24 and ELF c801239e…0c75bf retained before verified no-build upload. Both shutdown drains/postflight complete, no safety latch. Fullnative3203passed/7skipped/119suites/host105/defaultbuildSUCCESS/padding1688 unchanged. [I5 — motion recipe and evidence](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/PHYSICAL-MOTION-QUALIFICATION.md); [I5 — complete owner/IRQ qualification gates](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/OWNER-IRQ-QUALIFICATION.md). Calibration is blocked by no independent calibrated reference; RTC/CPU interrupt/derived MIDI cross-checks cannot replace it. Only host USB-device input exists; receipts stay defaultOFF, rollingOFF and target continuation disabled.
