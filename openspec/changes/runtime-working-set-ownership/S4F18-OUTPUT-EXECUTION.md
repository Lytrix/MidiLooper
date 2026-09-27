# S4f.18 — job-owned Record closure output

Status: consumer verified; production integration and parent task remain open. Baseline bbd2a5a3. Existing unrelated HITL/doc edits preserved.
Contract: SHARED-CAPTURE-FINALIZATION.md; DEC-064 — exclusive raw ranges;
DEC-093 — shared capture finalization. Within authorized connected implementation.

## G0 / G1 / G2

VERIFIED: the job already owns the pending-note map, raw range, slot/LoopId/mode,
geometry and admission receipt. The slot runtime has a reserved canonical output,
chunk credits, output rewrite cursor and generation-qualified stop-owner binding.
Use these existing owners to append one identity-qualified terminal Record Off per
call. A source-owner pointer and generation extend the existing output cursor by eight
bytes; no new state owner, queue, full-take array, or alternate publication path.

Validate job identity, raw borrow generation, runtime owner/generation, phase,
credit count and output cursor before mutation. Initialize an empty prewarmed
output as Record; append using reserved chunks only. Erase a pending obligation
and consume its credit only after its row was accepted. Denial preserves both.
Saved job phase/length determines the closure tick; selected/live slot is irrelevant.
Ordering, synthetic wrap normalization and pass construction remain separate grains.

G0 PASS for this consumer. G2 PASS WITH TIGHTENING: output reset/replacement must
invalidate its cursor; same-address raw reuse must compare the saved generation;
an invalid NoteId must not fall back to searching another producer. Empty and
repeated completion must not fabricate rows. Use one map entry, not a full walk.
The physical MIDI key domain bounds held-note entries; no overdub staging array is
used for Record closures. The current admission contract still has overdub's
128-note / 2,048-row limits: this consumer must not be used as evidence that Record
admission or the full input envelope is solved. That mode-specific gate remains open.

## Oracle before code

Construct a controlled Record job binding using the real raw ownership cut,
pending-note swap, runtime output reservation and owner receipt. This is explicitly
a consumer fixture, not a fake proof of production Record admission. Start a live
successor through real Record input on another slot. Each grain emits exactly one
old-job Off with its original ID/channel/pitch and saved geometry, consumes one
old obligation, and leaves successor rows/pending notes and frozen raw bytes intact.
Denied resources/invalid ID/stale runtime/raw/output/Loop identity must preserve
pending obligations and credits; replenishing resources permits exactly-once retry.
Retargeting destination does not change saved closure geometry. Repeat completion
is idempotent. Empty closure set produces no rows. Fixed drains use initial count.

## Verification

Focused shared-finalization fixtures; discriminating wrong-slot/raw-lookup mutation;
full native; capture-serial build (existing layout pins, RAM1 padding >=840);
strict OpenSpec validation and diff checks. No device run or task checkoff.
The new private consumer is not yet dispatched by the production stop worker:
Record admission must stay disabled until normalized pass/output and playback
handoff are ready. No early enabling or weaker acceptance oracle.


Review tightening: runtime binding alone cannot reject a same-address new job if
both runtime and admission generations advance while partial output survives.
Stamp the output cursor with its source generation and compare it each grain.
A fixture advances both bindings, requires denial, then restores them and finishes
without duplication. Measure cursor/runtime layouts before accepting the change.

## Consumer verification evidence

- Complete owner+generation focused run: **24/24**, 7.354 s. Five new
  cases exercise live successor isolation/retargeting/repeat completion, denied
  resources and identity faults with retry, raw/output replacement, 257 held
  notes crossing a chunk boundary, and no held obligations. The large-case fixture also
  checks every large-case output identity exactly once against its input row.
- Four separate mutations each fail the intended assertion: selected-slot
  lookup denies a valid old-job grain; removing the output source-generation
  comparison accepts a new binding over partial old output; removing the owner
  comparison accepts another descriptor with the same generation; removing the raw
  generation comparison accepts a reused raw object. Each mutation was restored
  before final regression. Each final mutant failed at its intended assertion, not compilation. No changed oracle accepted a mutant.
- Fixture setup correction: the controlled binding originally left Track in
  STOPPED_RECORDING, which cannot start Record directly. It now supplies the
  declared PLAYING binding result, then uses real successor start/input. This
  does not claim production admission/transport transition coverage.
- Earlier generation-only build passed with unchanged runtime size and RAM1.
  It is superseded by the pointer+generation receipt below; final build evidence
  must cover the complete identity and the measured runtime-size adjustment.
- Strict OpenSpec validation and diff whitespace checks: PASS. No serial action,
  device upload, SD operation, or hardware timing claim. Registered baseline
  STIM-BASE-2-ONE-OD-01@v1 remains the integration stimulus; it was not changed.
- No publication/undo/persistence path, overlap or playback-order policy changed.
  This implements existing decisions; no new exclusion or decision-log entry.
  Parent gates remain open, no release shipped, and no task is checked off here.

Further review tightening: generations can coincide across different descriptors.
The cursor must retain both the admission address and generation. Added an owner
pointer and an independent wrong-descriptor fixture. ARM compiler probe measures
cursor 48 -> 56 and playback runtime 6,344 -> 6,352 bytes. Update the existing
placement pin to this measured value; keep the RAM1 padding floor unchanged.
This is eight bytes per allocated runtime, not a second owner or full-take
allocation; total cost follows the number of resident runtime instances. The earlier full regression was interrupted for this
change and is not final verification evidence. Fresh regression/build results are recorded below.

Final complete-receipt firmware build and restored-source rebuild both pass:
FLASH code 1,042,680; RAM1 variables 96,160 / code 392,376 / padding 840 /
local headroom 34,912; RAM2 variables 155,232; static EXTRAM 9,312.
ARM runtime/cursor are 6,352/56 bytes; native cursor is 72 bytes (compiler assertion).
Track and job placement pins pass unchanged. No upload performed.

Final restored-source full native regression: **2,513 passed / 7 skipped**,
2,520 total, 4m13.289s. Shared-finalization suite passes **24/24** within that run.
Strict OpenSpec validation and whitespace checks pass. Runtime state/current work,
OpenSpec task progress, current placement references and execution bounds updated.
No deliverable shipped or new architectural decision/exclusion; existing
DEC-093 — shared capture finalization remains the implementation authority.
Next: bounded normalization/pass construction, then integrate admission and
first-bar/onward playback without weakening the connected acceptance oracle.
