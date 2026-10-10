# S7.5a — continuity envelope and timing oracle (increment 1: the derivation frame)

**Status: increment 1 of S7.5a — the derivation frame, not the declaration.** This records the transport/tempo
basis the limits must be justified against, the numbers that are already **recorded** elsewhere (cited, never
re-derived by guess), the arithmetic that follows from them (labelled *proposed*, because it is new here), and
the metrics the spec names for which **no number and — for two of them — no instrument exists at all**. No value
in this document is owner-approved, no limit has been exercised against a qualifying run, and nothing here
changes firmware, tests or a bound class. **S7.5a therefore stays unchecked**, and the sibling's statement stays
true: *"S7.5a is open — it has no numeric limits recorded"*
([`CONTINUITY-HANDOFF.md`](../../../windowed-playback-generation/CONTINUITY-HANDOFF.md), §1).

## 1. What S7.5a must produce, and the rules that constrain how

| Source | Requirement on the envelope |
|---|---|
| [`spec.md` § *Continuous runtime operation has one end-to-end acceptance contract*](../../specs/runtime-working-set-ownership/spec.md) | Declare simultaneous holders, loop lengths, effective density, same-tick bursts per physical route, sustained input/output rates, window span, tempo and clock-source assumptions, resource headroom and capture replenishment, competing persistence/display/load work. **Fix numeric limits, units, measurement points and clock correlation before qualifying runs**; justify limits against musical deadlines and transport capacity, never by fitting an observed run. Report interaction acceptance, scheduled execution, action-to-audible latency and publication freshness **separately**; the one-pass-late allowance is not an immediately audible edit. Name unsupported combinations and unproven targets instead of shrinking concurrency. |
| [`tasks.md` S7.5a](../../tasks.md) | *"Derive limits before qualifying runs, never fit them after observing a run. State unsupported combinations and unproven targets without silently shrinking required concurrency."* Budget every simultaneous holder, resource reserve/replenishment and competitor. |
| `DEC-108` §13(b) (`docs/DECISION_LOG.md`) | *"the envelope is derived from grain costs and the worst permitted service gap — no invented microsecond budget"*; round-robin fairness, one declared grain per admitted turn. |
| `DEC-100` (`docs/DECISION_LOG.md`) | The declared envelope is **reported, never enforced**; refusing at admission was rejected. A breach is reported and does not block activation. |
| [`ExecutionBounds.md`](../../../../../docs/Authority/Architecture/ExecutionBounds.md) (bound classes, rules) | A µs budget checked *between* steps does not make a step structurally bounded; a measured unit is not frozen as a ceiling; each interval-path owner needs an admissible class + policy. |

## 2. The transport and tempo basis (what "musical deadline" means here)

Recorded constants (`include/Globals.h`): `INTERNAL_PPQN = 192` (:72), `QUARTERS_PER_BAR = 4` (:73),
`TICKS_PER_CLOCK = 8` (:75), `TICKS_PER_BAR = 768` (:76), `TICKS_PER_16TH_STEP = 48` (:77).
Reachable clock tempo is the one `ClockManager` enforces, **20–300 BPM** (`DEC-108` item 7, which also records
that the 60/187 pair in `TempoMapping.h` is the CC102 fader mapping, **not** the clock), and external pulses
advance playback directly (`TICKS_PER_CLOCK` per pulse), so **batched pulses compress the effective deadline**
and the proof must run against actual clock advancement, never a nominal tempo.

Derived wall-clock windows (arithmetic only — `ms = 60000 / BPM * ticks / 192`):

| Musical unit | Ticks | @300 BPM | @120 BPM | @20 BPM |
|---|---|---|---|---|
| 1/32 beat (`DEC-108` item 7's lower bound) | 6 | 6.25 ms | 15.6 ms | 93.75 ms |
| 1/16 beat (`DEC-108` item 7's upper bound) | 12 | 12.5 ms | 31.25 ms | 187.5 ms |
| one internal clock pulse | 8 | 8.3 ms | 20.8 ms | 125 ms |
| 1/16 note (`TICKS_PER_16TH_STEP`) | 48 | 50 ms | 125 ms | 750 ms |
| one beat / quarter | 192 | 200 ms | 500 ms | 3000 ms |
| one bar | 768 | 800 ms | 2000 ms | 12000 ms |

`DEC-108` item 7's recorded "1/32–1/16-beat window is **6.25–187.5 ms** across 20–300 BPM" is exactly this
table's first two rows.

## 3. Table A — numbers already recorded (cited; the envelope's anchors)

| Quantity | Recorded value | Kind | Where recorded | Applies to |
|---|---|---|---|---|
| Per-lane-turn permitted phase advance `Delta-max` | **64 ticks** (66.7 ms @300 BPM, 166.7 ms @120 BPM, 1000 ms @20 BPM) | declared, **set from measurement** | `include/Track.h` `kPlaybackViewPermittedAdvancePerTurn` (:552-560): measured internal clock p50 0 / p90 9 / p99 15 / **max 17**; host clock @300 BPM p50 8 / p90 16 / p99 48 / **max 48**; declared at/above both maxima (1.33× the worst) | the rolling playback lane's scheduling advance, per opportunity whose predecessor was eligible |
| Across-track rotation `H` | **`NUM_TRACKS - 1` = 7** lane turns | structural | `include/Track.h` (contract); measured load-bearing in `evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md` | every eligible track served within H lane turns |
| Composition cost law | `drainTurns = H x (1 + K) x serviceTurns`; measured `H` 4→8, `K` 2→3; service turns **2 / 7 / 33** for 5 / 20 / 80 notes; **span is not an input** (identical at 17 and 41 bars) | measured | `evidence/2026-10-05-s310x-hk-contention-sweep/EVIDENCE.md` | admission lead for a windowed request |
| Window admission limit `L` | **2,048 events** = `kEventsPerView`, with a reserved closure capacity counted *inside* `L` | chosen, explicitly provisional | `include/RuntimePlaybackView.h` (:30, :38, :61); `DEC-108` item 8 | one window on one route; accepted content is never truncated to fit |
| Window span | **two-bar** initial comparison configuration (smaller spans to be evaluated) | chosen | `DEC-108` item 8 | the rolling lane's initial comparison configuration |
| Role envelope | **8** sounding roles supported today; **target 16**; 32/48/64 to be measured **without promising** | declaration | `DEC-108` item 9 | sounding roles |
| Memory anchors | pool **8 MiB**; measured ceiling **< 8,192 KB** (refusal `bytes=8192 … live=8527KB`); boot `live` on a full library **1,801 KB** (1,949 settled); settled `live` at N=8 with one take's runtime **6,505 KB** (margin **1,687 KB**); per-holder window cost `P` **331 KiB** windowed / **831 KiB** whole-take | measured | `S2C23-CLASS-BUDGET.md` | whole library; committed slots are descriptors only |
| Same conservative sum | **12,626 KiB vs 8,192 KiB → over by 4,434 KiB** (recorded, unresolved; its own rule forbids shrinking it silently) | derived from measurements | `S2C23-CLASS-BUDGET.md` | simultaneous holders at N=8 with capture + finalizer + switch |
| Action-to-audible (pre-existing normative limit, **not** the continuity envelope) | NOTE_EDIT session-open feedback **≤ 3200 ms** (fader1 **≤ 1600 ms**) | normative | `openspec/specs/note-edit-fader-feedback/spec.md` (:38) | NOTE_EDIT session open/select feedback |
| One-pass-late allowance | serve the affected interval's **previous revision** for that traversal; publish the replacement before the following eligible traversal; a **second** consecutive miss is a named contract breach | normative | `DEC-108` item 4 | affected playback interval |
| Lag / fault policy | one-pass-late is served and reported; the second consecutive miss latches `deadlineContractFaultLatched`; the deadline is never silently re-granted | normative | `DEC-108` §13(a),(e); `evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md` | rolling lane |

## 4. Table A2 — arithmetic that follows from Table A (**proposed here; needs owner sign-off**)

These are new statements, not records. They are listed separately so nothing in Table A appears approved by
association.

| Proposal | Derivation | @300 BPM | @20 BPM | Caveat |
|---|---|---|---|---|
| Worst declared deferred-owner **denial** for one eligible lane | `H x Delta-max` = 7 x 64 = **448 ticks** of phase advance | 155.6 ms | 2333 ms | The sibling records that a *blocked* run of lost turns can exceed this; the seconds-scale starvation episodes (2,138 / 4,355 / 8,520 ticks) are exactly that and no per-turn constant can hold them (`EXECUTION.md`, increment 17) |
| Musical-deadline candidates for the envelope's input/output limits | `DEC-108` item 7: expect the limits to sit in the 1/32–1/16-beat band, i.e. one of the first two rows of §2 | 6.25–12.5 ms | 93.75–187.5 ms | Which band each *metric* takes is a musical decision, not arithmetic, and is owed (§6) |
| Action-to-audible for a slot switch / undo handoff | not derivable from Table A: the only recorded action-to-audible number is the NOTE_EDIT feedback one | — | — | **Parked 2026-10-11 — kept out of this envelope's limit table (owner decision).** The instrument measures *acceptance → first emission row* positionally, and the increment-4 device legs show what that endpoint really is (control-surface feedback, not the awaited note: `MO,224,16,…` 3.24 s after the accept, against the device's own `GS` accept row at +642 µs and `LED` feedback rows arriving earlier still), so the ≤3200/1600 ms NOTE_EDIT limit must not be attached to it. Still to be derived from musical intent (a switch that lands one bar late is a musical failure, not a numeric one) **if** it is reinstated as an identity-paired, class-split pair |

## 5. Table B — metrics the spec names with **no number**, and the instrument each one needs

`—` in the instrument column means **no instrument exists today**. Existing measurement points available to bind
to are the S0 timing telemetry families emitted as `DIAG,<family>,<maxUs>,<overCount>`
(`src/Utils/RuntimeTimingTelemetry.cpp`, documented in `include/Utils/DebugSessionCapture.h`): `midi_gap`,
`midi_input`, `clk`, `tracks`, `usbdev`, `din`, `hosttask`, `hostdrain`, `usbread`, `usbdisp`, `usbcap`, `usbthru`,
`usbclk`, `usbnote`, `usbcc`, `usbtrans`, `noteappend`, `notechg`, `noterecon`, `notepair`, `idle_maint`,
`load_frame`, `persist_save`, `late_on`, `late_off`, `late_clk`, `extclk_arrival`, `extclk_dispatch`,
`clk_batch`, `service_period`, `track_rebuild`, `track_order`, `track_send`, `track_slot`, `track_resid`.

| Metric the envelope must declare | Instrument today | What must exist/be measured first |
|---|---|---|
| Maximum MIDI **input-service gap** | `DIAG,midi_gap` (+ `midi_input`) exists as telemetry; the leg-local constant **200 ms** (`scripts/hitl/verify/note_edit_reselect_stacked.py:24`) is a leg threshold, **not** a declaration | Choose the capture point (host-clock vs device-clock correlation), state the correlation uncertainty, then derive the limit from the deadline basis (§2) rather than inheriting the leg constant |
| **Capture timestamp error** | **—** | An instrument must be built first: there is no timestamp-error probe in `src/`/`include/` at all, so this metric currently cannot be measured, let alone declared |
| Per-route **output lateness** | `track_send`, `track_resid`, `late_on`, `late_off`, `usb*` families exist; no declared limit | Per-route capture point + limit; must account for physical serialization/USB scheduling **separately** from firmware lateness (spec) |
| Per-route **jitter** | **—** as a declared quantity (the families above are maxima, not distributions) | A distribution (p50/p99) capture point per enabled route |
| **Queue residence** | **—** (ring numbers are capacity: 1 MiB ring, 32 KiB record reserve) | Owner identification per queue + age probe |
| **Route throughput** | **—** (nearest recorded is the composition cost law in Table A) | Aggregate per physical route: sustained events/s and per-tick burst capacity, measured under load |
| **Same-tick burst limit** | structural only (3 x 128 = 384 page slots; 128 closure slots inside `L`) | A declared number per route; note the recorded falsification (370/4,200 events lost on a burst 5.5x the ring capacity) |
| **Held reserves** as envelope lines | named reserves exist (closure slots inside `L`; canonical chunk admission; undo protected horizon); none is budgeted as an envelope line | Budget lines with values |
| **Resource headroom / capture replenishment** | structural (canonical output reserves chunks cold, replenishes 1 chunk/turn; `captureStagings[2]`; 4 stop jobs/track) | A declared replenishment **latency** bound, not just a structural one |
| **Action-to-audible** for undo/redo and slot switch | only the NOTE_EDIT feedback spec limit exists | Musical justification + limit per interaction class |
| Bounded deferred-owner service **denial** in time | the 7-turn rotation (Table A) | Convert to the declared time form and decide whether the lost-turn case is a declared exception (see the caveat in §4) |

## 6. Holders, reserves and competitors — the budget the spec requires, and where it stands

Recorded (not yet budgeted as envelope lines): 8 sounding roles today with 16 as target (`DEC-108` item 9); two
capture stagings per track; four record stop jobs per track; 1 MiB capture ring plus a 32 KiB record reserve;
canonical output chunk admission; the undo protected horizon; the per-holder window cost (`P` = 331 KiB
windowed, 831 KiB for a whole take) and the conservative simultaneous sum that already exceeds the pool by
4,434 KiB (`S2C23-CLASS-BUDGET.md`).

**Status:** every line above is a *recorded anchor or a measured component*, not a budget line of the continuity
envelope. The envelope's own holder/reserve/competitor budget table is **owed** — and it is the piece that
decides whether the 8-role support claim survives at the declared contention, which the spec forbids resolving by
silently shrinking concurrency.

## 7. Unsupported combinations and unproven targets (named, not shrunk)

- **Sounding roles above 8** — 16 is a *target*; 32/48/64 are to be measured **without promising** (`DEC-108` item 9).
- **Whole-take residency per role** (831 KiB/role) at the declared role count — the conservative sum already
  exceeds the pool; nothing here asserts it fits.
- **Same-tick density beyond the admission limit `L`** — accepted content is never truncated, so a burst above `L`
  is unsupported, not handled.
- **Batched external clock pulses** compress the deadline below the nominal tempo basis (`DEC-108` item 7).
- **Seconds-scale lost-turn runs** (2,138 / 4,355 / 8,520 ticks recorded) — outside any per-turn constant; their
  status under the envelope is undecided (§4 caveat).
- **SD write latency** — recorded as unbounded until measured; persistence therefore has no declared residency
  guarantee.
- **Route throughput, queue residence, per-route jitter, capture timestamp error** — unproven and (two of them)
  unmeasurable today (§5).
- **The 12,626 KiB conservative simultaneous sum** — recorded as over the pool; no silent reduction is permitted.

## 8. What remains owed for S7.5a to be earned

1. **Owner sign-off on every declared value.** No number in this document is approved; Table A's values are
   recorded engineering declarations (the 64-tick figure is explicitly *not* owner-confirmed — the 2026-10-04
   increment still lists the owner's confirmation as owed) and Table A2 is new.
2. **Instruments** for the Table B rows — `capture timestamp error` first, since it has none, then queue residence,
   per-route jitter and route throughput.
3. **A measurement plan per row**: capture point, clock correlation and its uncertainty, which loop
   length/density/route/clock source, and which registered leg or fixture supplies it.
4. **The holder/reserve/competitor budget** of §6 as envelope lines.
5. **Then, and only then**, the oracle row: which metric each continuity verdict reads, at which capture point, and
   which qualification run exercises it.
6. **A `DEC-###` entry** (`ARCHITECTURE_RULES.md`: new architecture decisions must be recorded in
   `DECISION_LOG.md`). Not appended here: the numbers it would carry are not yet owner-approved.

**Not claimed by this increment:** no numeric limit is declared, no qualification run was made, no firmware,
test, bound-class, task-checkbox or device claim; no `windowed` sibling number is restated as a runtime
continuity limit (its declared window-owner service envelope stays its own,
`evidence/2026-10-04-s310t-declared-envelope/EXECUTION.md`).
