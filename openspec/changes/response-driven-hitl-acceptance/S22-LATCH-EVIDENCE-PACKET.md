# 2.2 — the accepted-stop latch's own sequenced identity evidence (execution packet)

Scope: task **2.2** of this change, the half that needs **firmware rows**
(`Add compact sequenced gesture/accepted-stop/start/switch/publication/retirement evidence at actual
owners, including record and overdub interval identity; verify accepted-latch consumption between polls
remains provable, required event loss is detectable, and unrelated slot/track events cannot substitute`).

The read half of the same task already landed host-side (see `tasks.md` 2.2's 2026-09-30 note and
`docs/Runtime/CURRENT_WORK.md`): the accepted-sequence oracle decides the overdub leg's L1/L5/L6/L7 and its
new `overdub_interval_identity` link from rows the owners already emit — start admission
(`DIAG,reentry,admit`), accepted-stop **binding** (`DIAG,reentry,bound`), execution
(`DIAG,stop_state` 6→7 / 7→5|1) and retirement (`DIAG,canonical_release`). What is still missing is the
latch's **own** timeline.

## 1. What is missing today, and why it is the remaining gap (measured, source-verified)

The accepted stop of DEC-078 lives in `Track::PendingInteractionLatch` (`include/Track.h`), an
externally allocated, size-pinned structure the change calls the *accepted interaction*. Its lifecycle:

| step | owner | evidence today |
|---|---|---|
| accepted — the stop bounded to the interval and retained | `Track::latchAcceptedStop` | **none**. A repeat of the same request is a documented no-op (`matches(...)` early-return) and emits nothing either. |
| waiting while the canonical target is held | `Track::parkAcceptedStopForCanonicalOutput` | `ODUB,stop,admission,…,accepted_stop_finalizer_wait` (once per episode, `waitCount`-guarded) and `…,accepted_stop_park_terminal` after 8 waits — **neither carries a slot, track or interval**. |
| consumed exactly once | `Track::consumeAcceptedStopObligation` | **none** — it clears the latch and returns. The admitted path emits nothing; the superseded path emits only a human logger line. |
| denied request counterfactual | the deny branch | `DIAG,reentry,deny,<tick>,<interval>,<slot>,<depth>` — identity, but only on the *denied* path. |

Consequences — each one is a clause of task 2.2:

1. **"accepted-latch consumption between polls remains provable" fails.** The latch can be accepted and
   consumed inside one poll gap (the deferred lane runs many turns per second). A poll of the on-demand
   observation before and after the gap sees an empty latch both times, and nothing counts that it was ever
   held: `waitCount` alone is not evidence of an acceptance, and the consumption is not recorded anywhere.
2. **"unrelated slot/track events cannot substitute" fails for the wait rows.** `accepted_stop_finalizer_wait`
   names no slot, so on a multi-track run the row cannot be attributed to the interval that owes it. (The
   `deny` row does carry identity — but only a *denied* stop produces it, and DEC-078's whole point is that a
   denial is not the acceptance.)
3. **"required event loss is detectable" fails for this family.** `RecordFamilyStats` is keyed by tag and
   covers `RECA,`/`RECS,` (+ `RID,`): `ODUB,stop,…` is Tier-A but has no counters, so an evicted or refused
   latch row is unattributable — the same defect class already fixed for the `DIAG,reentry*` family (which is
   Tier-A precisely because its absence was silent).

A stale entry exists for a family nobody emits (`DIAG,odub_stop,` in `include/Utils/CaptureLineTier.h`);
this packet does not touch it and does not name the new family that way.

## 2. Scope (what this slice changes, and what it deliberately does not)

**In scope — instrumentation only.** One compact Tier-A one-shot row per latch transition, emitted by the
latch's own owner, carrying the identity the latch holds; plus loss accounting for that family.

Wire form (compact, stable literals, inside the 192-byte `kMaxCaptureTextBytes` cap):

```
#CAP,<us>,DIAG,latch,<transition>,<track>,<slot>,<interval>,<destination>,<outcome>
```

* `transition` — `accept` | `repeat` | `wait` | `park_stall` | `consume`
* `track` — `resolveTrackIndexForPersistence(*this)`, the same 0-based field `DIAG,stop_state` uses
* `slot` — the **latch's** slot (0-based), never the active slot at emit time
* `interval` — the latch's `captureInterval_`, printed as its low 32 bits so device and native fixtures read
  the same value (`reentryDecision` prints the same quantity the same way)
* `destination` — 0/1, the request's stored destination (Playing/Stopped)
* `outcome` — `playing`/`stopped` for `accept`; `noop` for `repeat`; `waiting` for `wait`; `stalled` for
  `park_stall`; for `consume`: `admitted`, `superseded`, `sync_stop`, `transport_stop`, `discarded`, `?`

**Out of scope, recorded rather than implied.**

* **No behaviour change.** No guard, ordering, early-return or state transition moves; the emit is a queue
  append beside code that already runs on the same path. The pause must remain invisible to the latch.
* **The existing rows stay.** `ODUB,stop,…` (including `accepted_stop_finalizer_wait`) is read by
  `scripts/hitl/retention_probe.py` and the S4f oracle, and `DENIAL_STAGE_RE` anchors its `deny`-stage row's
  **end** — appending fields there would break a live consumer. The new family is additive; the wait episode
  therefore appears in both families (the `ODUB` row stays the sibling oracle's evidence, the `DIAG,latch`
  row is its identity-bearing copy).
* **Not a new authority or a new poll.** The counters are read back through the existing stopped-only
  `!CAP_IDENTITY_STATS` request (no new command, no new owner, no new state in the hot path).
* **Gesture/switch rows are not added here.** A slot press already leaves an in-ring `DISP,<slot>,…` row and
  the reentry family names the admit/bind/deny edges; what no row names today is the *latch*. If a later run
  shows a gesture whose only evidence is a human line, that is a separate, evidence-driven addition.

## 3. Transition oracle (fixed before any edit)

For one accepted stop on one interval, the new family must show, in this order and with **identical**
`(track, slot, interval)` on every row:

1. exactly one `accept` (with the request's destination),
2. zero or one `wait` — and, if the wedge reaches 8 waits, exactly one `park_stall`,
3. exactly one `consume` with the reason the owner actually took (`admitted` for the admission that binds
   the job, `superseded` for the S4f.4 transition-3 retirement, …),
4. **no** second `accept` or `consume` afterwards, and zero rows for any other slot/track;
5. a repeated press before the drive emits `repeat` and **no** second `accept`;
6. accounting: `entered == enqueued + rejected` for the family, `evicted == 0` in a clean run, and the
   read-back row `RING,record_stats,DIAG,latch,entered=…,enq=…,rej=…,tx=…,evict=…` agrees with the emitted
   rows. A refused append (ring full) bumps `rejected` and must not lose the counter.

Fixtures (controllable production path, no device):

* `test/test_continuous_capture` (the suite that owns the latch fixtures; it substitutes the emitter macro
  before the harness include): B02-shaped accept → **repeat** → wait → `consume(admitted)` with identity and
  exactly-once; and the superseded leg (S4f10c/S4f10e shape) → `consume(superseded)`. Mutation: dropping the
  emit from `consumeAcceptedStopObligation` must fail the fixture; making `repeat` emit `accept` must fail it.
* `test/test_capture_ring` (real ring, real counters): one emitter call → family counters and the
  `RING,record_stats,DIAG,latch,…` read-back, plus the row text carrying the identity; a full ring bumps
  `rejected` for the family.

Budgets and gates, to be recorded with the numbers: `sizeof(Track)` / `sizeof(CaptureStopJobs)` pins
unchanged (no new member), full `pio test -e native`, and `pio run -e teensy41-capture-serial` with the
post-link RAM1 padding line against the documented floor of 840 bytes.

## 4. Device verification plan (needs explicit approval)

An upload is **not** part of this packet's host work. After the build gate, the device leg is: upload the
capture-serial image, run the registered accepted-sequence OD leg, and confirm in the log that a retained
stop shows `accept` → (wait) → `consume(admitted)` with one identity, that `!CAP_IDENTITY_STATS` reports the
family with `rej=0,evict=0`, and that a run whose successor-overdub start is refused still shows the latch's
`accept` and its later `consume`. Until that run exists, the slice claims host verification only.

## 5. Completion record (host, 2026-09-30)

Landed as instrumentation only. **No guard, ordering, strictness or state transition moved**: the emit is a
queue append beside code that already ran on the same path, and the 104 pre-existing latch fixtures in
`test_continuous_capture` pass unchanged.

| item | measured |
|---|---|
| new row | `#CAP,<us>,DIAG,latch,<transition>,<track>,<slot>,<interval>,<destination>,<outcome>`; `transition` ∈ `accept`/`repeat`/`wait`/`park_stall`/`consume`, `outcome` ∈ `playing`/`stopped`/`noop`/`waiting`/`stalled`/`admitted`/`superseded`/`sync_stop`/`transport_stop` |
| emitters | `Track::latchAcceptedStop` (`accept`, `repeat`), `Track::parkAcceptedStopForCanonicalOutput` (`wait`, `park_stall`), `Track::consumeAcceptedStopObligation(const char* reason)` (`consume`), through `SC_ACCEPTED_LATCH` (`#ifndef`-guarded so a native suite substitutes its recorder) |
| consumption reasons | `admitted` (both deferred and stopped admissions), `superseded` (S4f.4 transition 3), `sync_stop` (in-edit fold completed the stop), `transport_stop` |
| loss detection | new `RecordFamilyStats interactionLatch`, tag-matched on `DIAG,latch,`, reported by the stopped-only `!CAP_IDENTITY_STATS` as `RING,record_stats,DIAG,latch,entered=…,enq=…,rej=…,tx=…,evict=…`; `capture/append/CaptureLineTier` classify the family Tier-A |
| transition fixture | `test/test_continuous_capture` **106/106** — `test_task22_a_retained_stop_reports_its_own_sequenced_identity_evidence` (one `accept(playing)` → `repeat(noop)` → ≤1 `wait` → one `consume(admitted)`, all on slot 0 / the interval / one track) and `test_task22_the_consumption_reason_names_the_edge_that_took_it` (exactly-once consumption, reason named); the S4f10e supersession fixture additionally pins `consume(superseded)` on the pre-replacement interval |
| accounting fixture | `test/test_capture_ring` **12/12** — family `entered`/`enqueued`/`transmitted`, the row text `DIAG,latch,accept,3,4,4660,0,playing`, the `RING,record_stats,DIAG,latch,…` read-back, a refused append counted on `rejected`, and `CaptureLineTier::isTierALine` true for a latch row |
| full gate | `pio test -e native` **2,643 cases / 2,636 succeeded / 7 skipped / 0 failed** |
| build | `pio run -e teensy41-capture-serial` **SUCCESS**; RAM1 budget post-action: `variables 96,192 / ITCM 393,216 (code 391,288 + padding 1,928) / locals 34,880`, floor 840 — **the line is unchanged**, so no budget re-base; `interactionLatchTransition` confirmed in the linked ELF (`arm-none-eabi-nm`), image sha256 `577841d6…` |
| mutations (each red-proved, restored byte-identically) | the `consume` emit removed → 3 failures; `repeat` reported as a second `accept` → the chain fixture fails; the `entered` bump removed → both ring fixtures fail; the interval printed as `0` → the identity assertion fails; the Tier-A entry removed → the tier assertion fails |

Owed and **not** claimed: the device leg (§4) needs an upload; the sequenced *gesture/switch* rows are still
absent (a slot press leaves only its in-ring `DISP` row and a human `Loop N:` line); and the counters are read
back by a stopped-only request rather than by the on-demand observation poll, so "consumption between polls" is
provable from the ring rows plus the counter read-back, not yet from a single poll.

## 6. Device status (2026-09-30): accounting verified on hardware, the latch rows still owed

Image flashed: `teensy41-capture-serial`, sha256 `577841d6…`, `interactionLatchTransition` present in the ELF.
Three capture attempts through the authoritative entry point (`scripts/host_midi_hitl.py run --preset base
--track 5 --loop-slot 1 --midi-channel 1`):

| attempt | run | what it shows |
|---|---|---|
| plain base preset | `captures/session_20260930_164133.log`, report `…164240.json` | **the family is wired on hardware**: the stopped-only `!CAP_IDENTITY_STATS` returns `RING,record_stats,DIAG,latch,entered=0,enq=0,rej=0,tx=0,evict=0` — present, counted, read back. `entered=0` because **both overdub stops were admitted immediately** (`OVERDUBBING → OVERDUB_STOPPING → PLAYING` within 26 µs each; no denial), so no latch existed to report. Record 32/32, overdubs 16/16 + 8/8, `overflow_free=true`, `tier_a_evicted_count=0`; the run's only issue is the documented pre-existing `persistence_stage_missing:result`. |
| successor-reentry reproducer (task 7.2's own flags: `--record-bars 8 --overdub-bars 5 --overdub-start-delay-bars 1 --stop-press-advance-clocks 48 --second-overdub-bars 0 --successor-reentry-delay-ms 400 --successor-reentry-stop-delay-ms 0 --successor-reentry-stream-bars 2`), twice | `captures/session_20260930_164337.log` (report `…164504.json`), `captures/session_20260930_164616.log` (report `…164616.json`) | both aborted at the **host** guard `overdub start refused: no predecessor stop-completion receipt`, so the overdub (and therefore every overdub stop) never ran: the run's own counters are `DIAG,latch,entered=0` for the whole boot and it carries no `successor_stop_denied_capacity`, no `DIAG,reentry,*` and no `ODUB,stop,` row at all. **The new instrumentation emitted nothing in these runs, so it cannot be implicated in that abort** — the guard is the documented host refusal to press without the predecessor's stop-completion receipt, and the 8-bar shape that satisfied it on 2026-09-17 predates two weeks of Record-publication work. |

**So, on device: the accounting half is verified and the latch rows are not.** A `DIAG,latch,accept` →
`consume(…)` sequence needs a stop that is actually *denied*, and both documented recipes that produce one
(the retention handoff driver; the stop-interruption wrapper) **clear a named scratch slot**, which needs
content authorization in this session. Until such a run exists, the row content rests on the native transition
fixture (which drives the real latch, park and consumption paths) and on the read-back showing the family at
zero rather than being absent.
