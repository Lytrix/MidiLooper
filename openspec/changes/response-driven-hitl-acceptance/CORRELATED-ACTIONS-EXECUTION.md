# Correlated gesture stages in the reusable action layer (task 3.1)

Host-only slice: no firmware, no build, no device action. Authority is the change's own
spec — [Response-driven acceptance gestures](specs/hitl-automation/spec.md) (*sent input*,
*recognized gesture*, *accepted intent*, *executed operation* stay distinct; responses
correlated with run/track/slot/interval; elapsed time or unqualified human log messages must
not establish completion) — plus its two scenarios of this task: *Unrelated or stale
response*, *Pending switch prevents a follow-up press*, and *Recognized single gesture*.

## What a gesture is now

`scripts/hitl/actions/correlation.py` owns the four stages. A press used to be
fire-and-forget: it put MIDI on the wire and returned, and every driver then re-derived
"did the device answer *this* press, and how far did it get" inline, with its own substring
set and its own deadline.

| Piece | What it decides |
|---|---|
| `Stage` | `SEND` → `RECOGNIZE` → `ACCEPT` → `EXECUTE`, in that order |
| `CorrelatedGesture.mark_sent(...)` | the SEND stage, with the device clock observed **before** the press (`before_device_us`) |
| `CorrelatedGesture.consider(line)` | whether one row advances **the next pending stage** — only that one |
| `CorrelatedGesture.wait_for(stage, rows, timeout_s=…)` | the bounded wait; `rows()` is the caller's snapshot (a driver flushes its lagging ring inside it) |
| `CorrelatedGesture.snapshot()` / `blocking_stage()` / `contradicted()` | the record: per-stage outcome + the row that decided it, and the first non-met stage by name |
| `StageRules(recognize, accept, execute, refusal, conflict)` | the caller's vocabulary, per stage |
| `Wire(text, identity)` | one acceptable row: a substring **plus** the identity tokens that row must also carry |
| `PendingSwitchGate(pending, released, target)` | whether a requested switch is still counting down, so a follow-up press must not be sent |

Four rules the previous inline versions did not all have:

1. **Stages stay distinct.** Only the next pending stage can advance, so a row that only
   proves execution cannot satisfy recognition or acceptance. The record names which stage
   the device actually reached instead of collapsing to "something happened".
2. **Identity is per wire, not per gesture.** Two families answer the same stage and name
   the same slot differently — the device's directly printed `Loop N: …` line and its in-ring
   `#CAP,<us>,DISP,<slot0>,…` row — so the identity tokens live on each `Wire`. A foreign
   slot's row cannot substitute for this gesture's.
3. **Drained history cannot substitute.** A row stamped older than the device clock observed
   before the press is history, not this gesture's answer. The rule is `hitl.device_time`'s
   (`device_stamp_us`), reused rather than re-derived.
4. **A different answer is a contradiction, not silence.** A row proving the device read the
   press as something else (a double tap where one short press was sent) is recorded as
   `contradicted`, and the wait returns at once rather than reporting a timeout. Absence stays
   a gap — the same absence-versus-contradiction split the acceptance oracle uses.

**Nothing re-presses or retries**: there is no send path in the wait API at all, and a stage
that never arrives stays pending and is named. Time is injected (`now` / `sleep`), so a
bounded wait is testable without real time and without a device.

## The action layer owns SEND

`correlation.record_send(...)` is called by the press helpers themselves
(`hitl/actions/buttons.py`, `hitl/actions/slots.py`), so the stage is written by the code that
put the note on the wire and no caller has to remember to declare its own gesture. Each helper
gained keyword-only `correlate=` and `before_device_us=`; the defaults keep every existing
caller fire-and-forget (all six current driver call sites are unchanged in behaviour). The
recorded `kind` speaks the firmware's own classification vocabulary — `short` / `double` /
`triple` from `MidiButtonManager`, `long` from the clear press.

## Vocabulary, grounded in the firmware

`PendingSwitchGate`'s defaults are the device's own rows, not invented ones:

* pending — `Loop %d: Queued playback switch` (`src/MidiButtonActions.cpp:484`), its filled-slot
  variant (`:538`, matched by the same substring), `Loop %d: Reaffirmed playback switch` (`:458`);
* released — `Loop %d: Switch gesture from slot %d` (`:407`);
* `Playback started` is named in `released` **and still cannot discharge a gate**, because
  attribution refuses it first: `src/Track/TrackTransportControl.cpp:28` emits it through
  `logTrackEvent`, so the row carries no slot or track at all. Measured 2026-09-27: a queued
  switch can leave **two** countdowns for one switch, which is why `released` needs its own row
  instead of "the next thing that happens".

## Evidence

`scripts/test_hitl_correlated_actions.py` — **19/19**, one case per clause above: stage
distinctness and the execute-only row; foreign-slot refusal at acceptance; drained history;
a refusal as its own outcome; a differing classification as a contradiction; another loop's
press as *not* a contradiction; the injected clock with a spent bound; a stage that never
arrives named with nothing re-pressed; a refusal returning immediately; no real `sleep` call
when one is injected; the queued and re-affirmed switch blocking until its own release row;
an unattributed `Playback started`; another slot's switch not blocking; the press helper
recording its own SEND; the classification vocabulary; a press with no correlation staying
fire-and-forget; press + own rows completing the four-stage chain; and a foreign-slot `ARMED`
row leaving acceptance pending.

Neighbouring host suites, all green: `test_hitl_actions` 2/2, `test_hitl_import_gate` 2/2,
`test_accepted_sequence` 90/90, `test_managed_record_drain` 21/21, `test_hitl_serial_timing`
33/33 (from the repository root), `test_hitl_command_forwarding` 26/26,
`test_retention_handoff_plan` 23/23 — **216 tests / 8 suites**. Two unrelated pre-existing
failures were re-confirmed identical with this slice's tracked edits stashed
(`test_layered_verifiers`, `test_occupy_paint_serial_verify`: one each) and
`test_edit_full_layered` still requires its bar-sync fixture record.

Twelve mutations across the two halves, each red-proved and restored byte-identically (sha256 checked before
and after every file):

| Mutation | Tests that caught it |
|---|---|
| identity check dropped in `_matches` | foreign-slot refusal, foreign-slot `ARMED` |
| the gate stops attributing rows to its target | unattributed `Playback started`, another slot's switch |
| the staleness floor removed | drained history, the seed's pre-press row |
| the contradiction clause removed | dissimilar classification, contradiction ends the wait |
| the press helper stops recording SEND | press records SEND, press + rows complete the chain |
| the floor exclusive again (`<` for `<=`) | a row stamped exactly at the floor |
| a rule-less stage awaited again | every receipt-only wait |
| the seed arm wires lose their slot identity | another slot's row, the wire vocabulary |
| the seed wait stops requiring recognition | the swallowed press, the merged classification |
| an unqualified row alone proves the take | the take after this gesture's own arm |
| the runner's switch guard becomes `blocked()` | a switch that later landed still invalidated the leg |

`test_hitl_import_gate` stays green: flows may not import `hitl.serial`, `hitl.verify`,
`hitl.scenarios`, `hitl.reporting` or `hitl.legacy_*`, and this slice adds no such import —
the gate is a denylist, so `hitl.device_time` and `hitl.control_constants` (both port-free)
are usable from the action layer, which is what lets the staleness rule keep one owner.

## The drivers consume it

Both device drivers' gesture waits are now the primitive's, with the control flow, the report fields and
the gate budgets they had before:

* **`hitl_accepted_sequence.py`** — `_await_slot_disp` (the selection, arm and recording receipts) builds a
  `CorrelatedGesture` whose ACCEPT wires are the caller's vocabulary (`states` on this slot's `DISP` rows
  plus the named human rows) and whose floor is `_gesture_clock_us`; it returns the deciding row, and
  `_slot_disp_rows` is now a pure row source with no freshness rule of its own. `_slot_ready_state` stays a
  *state* read and applies the floor itself (`_is_news`). `_switch_pending` keeps the leg's window and policy
  but takes its rule from `PendingSwitchGate` (`state(...) != "clear"`, i.e. a queue row for the target ends
  the leg even if the switch later landed); the runner already aborted before pressing again, and a test now
  pins that.
* **`hitl_retention_handoff.py`** — the seed's arm and take waits became `_await_seed_receipt`, whose stages
  are `recognize` (the device's own `Button press: Loop N (…)` row for **this** slot), `accept`
  (`DISP,<slot0>,ARMED|RECORDING` or `Loop N: Start Recording`), `execute` (`DISP,<slot0>,RECORDING`,
  `,RECA,<slot0>,` or `Recording started`) and whose `conflict`/`refusal` vocabulary names the two losses the
  device has actually produced — a merged classification (`Loop N (double)`) and a press the firmware read as
  the other action (`Loop N: Stop Recording`, `Loop N: Queued playback switch`). The arm gate is now
  slot-scoped: the track-state family (`#,ST,Track,STOPPED,ARMED`) names no slot and is no longer accepted as
  this seed's receipt. `_device_floor_us` (both row families, via the new
  `hitl.device_time.newest_row_clock_us`) is the floor, and the seed's failure message is now
  `slot N never started recording: blocked at accept (send=met, recognize=met, accept=pending, …)` instead of a
  bare timeout, with the same record in the run's events.

New evidence: `test_retention_handoff_plan` **30/30** (7 new: the arm receipt must name the slot, an
unattributed transition is not it, an unrecognised press is named, a merged classification is a named
contradiction, a pre-press row is never the receipt, the take needs this gesture's own arm first, and the
per-family wire vocabulary), `test_accepted_sequence` **91/91** (1 new, above), `test_hitl_correlated_actions`
**23/23** (4 new: the floor is inclusive, a rule-less stage is not awaited, a state wait awaits no SEND, and a
stale *human* row cannot satisfy a stage), `test_hitl_device_time` **16/16** (1 new). Neighbouring suites green
together: `test_accepted_sequence_corpus` 3/3 (the four pinned device fixtures keep their verdicts),
`test_retention_probe` 43/43, `test_device_shutdown` 15/15, `test_capture_transitions` 61/61,
`test_hitl_serial_timing` 33/33, `test_managed_record_drain` 21/21, `test_hitl_command_forwarding` 26/26,
`test_hitl_actions` 2/2, `test_hitl_import_gate` 2/2 — **244 tests / 9 suites** for the migrated slice. The two
unrelated pre-existing failures (`test_layered_verifiers`, `test_occupy_paint_serial_verify`) were re-confirmed
identical with this slice's files stashed.

## Not done, and not implied

* **3.2 (cancellation) and 3.3 (watchdog, versioned report schema, verify-only replay, the narrow
  observation-wait facade) are untouched.** They remain the open tasks of this section.
* **Not every wait is a gesture wait.** `_await_markers` (the commitment/persistence receipts) and
  `_wait_for_any` (transport edges and the commit) stay as they are: their rows are not a press's answer to a
  stage, and the transport edges (`Transport started`) are not slot-scoped at all. Converting them is 3.3's
  observation-wait facade, not this task.
* **No firmware row, build or device action is part of this slice, and none is implied.** The two drivers are
  host tooling whose next verification is a device run; this slice's evidence is host-side, and the device leg
  of task 2.2 and the reload/`LIBOBS` poll remain where
  [S22-LATCH-EVIDENCE-PACKET.md](S22-LATCH-EVIDENCE-PACKET.md) recorded them.
