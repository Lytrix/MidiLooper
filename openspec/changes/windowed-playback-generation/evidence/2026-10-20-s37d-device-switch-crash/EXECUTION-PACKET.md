# G2 execution packet — gate-row dedupe state in `DMAMEM` proves "written" with a magic

Authority: [IMPLEMENTATION_READINESS.md](../../../../docs/Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
This slice is the blocker fix for 3.7d's owed device read; it is not a new task in the change.

## Identity and scope

- **Packet/task:** 3.7d blocker (device reboot on a queued slot switch). Contract: DEC-121's class — `DMAMEM`
  (`.bss.dma`, NOLOAD) state is never initialised by the image or the startup code, so "never written" must be proven
  by a written marker; DEC-151 records this instance.
- **Status:** device verified.
- **Owner and path:** `Track::reportPlaybackLaunchGateRow` (the deferred-idle turn that prepares a queued switch and the
  launch) and `Track::reportPlaybackRollingGateRow` (the rolling lane turn) own their per-entry "last emitted" state;
  the reset is a pure function of the entry (`GateRowLastState::ensureInitialized`).
- **Primary files:** `include/Utils/GateRowLastState.h` (new), `src/Track/TrackPlaybackWindowBuild.cpp`,
  `test/test_gate_row_last_state/test_gate_row_last_state.cpp` (new).
- **Preconditions present:** both tables already live in `DMAMEM`; both rows already dedupe by content. **Prerequisite in
  this slice:** the written magic. **External blocker:** none for this fix; 3.7d's read has a *second* blocker recorded
  below.
- **Mutation / postconditions:** on first use since boot an entry is reset to its declared defaults and stamped with its
  table's magic; a written entry is untouched; the row, its token and its cadence are unchanged.
- **Explicit non-goals:** no row/format change, no cadence change, no new owner, no DTCM state, no change to the launch
  or rolling decisions, no audit of the other 48 `DMAMEM` declarations (named limit).
- **Commit boundary:** header + two call sites + the fixture are independently buildable; no compatibility shim.

## Oracle fixed before behavior edits

| Case | Initial state | Action | Expected |
|---|---|---|---|
| L-stale | `LaunchState` filled with `0xA5` (non-null `exit`) | `ensureInitialized(kLaunchMagic)` | returns true; `exit == nullptr`; magic set; `loopLengthTicks/flags/launchPreparation` zeroed |
| L-written | stamped, then `exit = "launch_ready"`, `len = 24576`, `flags = 0x0A5A`, `prep = 3` | same call | returns false; every field preserved |
| R-stale | `RollingState` filled with `0x5A`, `seen = true` | `ensureInitialized(kRollingMagic)` | returns true; `seen == false`; `exit == nullptr`; key/slot/len/result zeroed |
| R-written | stamped, key `0x93FF`, slot 3, `seen = true`, literal exit | same call | returns false; all preserved |
| M-wrong-magic | launch entry stamped with `kRollingMagic` | `ensureInitialized(kLaunchMagic)` | returns true and clears (a copy/paste of the wrong constant fails here, not on the wire) |

**Counterexample that must fail:** with the magic check removed, a stale non-null `exit` survives and the first
`strcmp` reads it — the device's own fault. Red direction: `if (state.magic == magic)` → `if (true)` fails all five
cases (measured).

## Controls and coverage

- **Seam reused:** none — the helper is a pure function of the entry; the fixture feeds it the only state shape the
  defect needs (stale bytes with a non-null pointer).
- **Production path exercised:** the device leg drives the real switch → `preparePlaybackLaunchForSlot` →
  `reportPlaybackLaunchGateRow`; no stub sits on the asserted path.
- **Relevant cases:** first use after boot (the defect), re-use after a write (must dedupe), the masked rolling case
  (`seen` stale), and the wrong-magic case.
- **Independent expected content:** the flags/keys asserted are values a reader can check against the ignores; the
  device oracle is "the run survives the switch and emits `launch_gate` for the target slot", both visible in the capture.
- **Untestable-before-link:** the real `DMAMEM` non-initialisation is not reproducible natively (native builds are
  ordinary statics); the fixture simulates it by construction, and the device leg is the proof of the invariant.
- **Bound:** five native cases, no fuzz.

## Verification recipe

- Focused: `pio test -e native -f test_gate_row_last_state` → **5/5**.
- Red: the same command with the guard disabled → **5 failed**, header restored byte-identically
  (`sha256 a9a634f2…`).
- Broad: `pio test -e native` → **3343 cases / 7 skipped / 3336 succeeded / 0 failed**.
- Build: `pio run -e teensy41-capture-serial-window-qualification` → SUCCESS, RAM1 `variables 96,192 / code 392,376 +
  padding 840` (floor unchanged), RAM2 `168,384` (+288 B for the two magics); `pio run -e teensy41-capture-serial` →
  SUCCESS, RAM1 `variables 96,192 / code 392,216 + padding 1,000` (unchanged).
- Device: registered preset `windowed_content_parity` (track 8 / slot 4 / channel 8), `--record-bars 32 --boot-reset
  --switch-target-slot 5`; runs 06 (settle 60 s) and 07 (settle 120 s) on image `fa7197e7…`.

## Completion record

- **Oracle/contract unchanged:** the row tokens, fields and cadence are untouched; only "is this entry written" changed.
- **Fixture discriminating:** yes — the red direction fails exactly the five cases and the file is restored
  byte-identically.
- **Native:** 5/5 focused; 3343 / 7 skipped / 3336 succeeded full suite.
- **Build/RAM1:** both envs SUCCESS; RAM1 lines unchanged (0 B of DTCM); RAM2 +288 B.
- **Device:** crash gone. Runs 06/07: `crash=1` rows **0** (was 4/4 crashes), 7 `launch_gate` rows for `t=7,s=4`
  (`launch_requested`, `launch_requested_match` ×3, `launch_ready`, `launch_ready_owns_source`, `launch_building_match`).
- **Still open (recorded, not claimed):** 3.7d's named-`rq` read. Both runs show the queued switch **not committing**
  in the settle window (`snap_gate,t=7,s=4,a=3` for 450 turns at 120 s; no `win_gate` for the target), so
  `win_blocked_by_request` is still unreached — a second, separate blocker.
