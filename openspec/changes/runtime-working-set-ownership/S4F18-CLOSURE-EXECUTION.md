# S4f.18 — record held-note identity prerequisite

Status: identity prerequisite native/build verified; parent gates open. Baseline 8fa916af; existing HITL changes preserved.
Contract: SHARED-CAPTURE-FINALIZATION.md, DEC-093 — shared finalization.

## G0 — current path and decision review

VERIFIED: Track::noteOn creates PendingNote then calls recordMidiEvents, which
allocates the canonical NoteId and appends it. PendingNote lacks this identity.
Both recordMidiEvents(NoteOff) and appendCaptureNoteOffAtPhase search raw rows to
recover it. Loop::appendCaptureEventWithResult also searches if the Off has no ID.
A frozen raw range can outlive the producer. Existing jobs swap the pending-note
map, so a scalar receipt there naturally follows its owning interval.
VERIFIED: overdub wrap rebases held notes through staging reservations; it must
retain that existing authority. This change uses the receipt only for Record.
VERIFIED: normal and armed pre-roll Record input share recordMidiEvents. Existing
aggregate PendingNote initializers must remain valid. No new owner, queue, format,
full-take staging, or worker FSM. G0 PASS for the receipt prerequisite.

## G1/G2 — fixed implementation and oracle before edits

Append a default-invalid Record NoteId receipt to PendingNote, stamp it only after
successful Record NoteOn append, and use it for physical and synthesized Record
Offs. Preserve legacy raw-event callers with no pending-note entry. A pending
entry with an invalid receipt must fail closed rather than select an unrelated
raw On. Overdub lookup/wrap reservations remain unchanged.

Fixture cases: real input creates held notes on two channels; freeze the raw
range so the producer has no original On; add an unrelated same-pitch On to the
producer; physical and stop closures must keep the original channel-qualified
identities, clamp stop ticks as before, and leave the frozen source immutable.
Repeat a NoteOn while held: receipt unchanged. Reject input: no receipt remains.
Pre-roll uses the same accepted input path. No fixture substitutes the asserted
Track/Loop/append owners. A mutation restoring the scan must fail exact ID checks.

G2 PASS WITH TIGHTENING: stamp after acceptance, never a peeked/unaccepted ID;
use only Record mode, since overdub wrap has a different lifetime; size checks
must measure PendingNote while preserving Track/job layout pins. This removes
source-length lookup from legitimate held Record closures, not all record-stop
work. Deferred job closure output, minimum-length normalization, pass construction,
audible deadlines, and admission integration remain mandatory parent gates.

## Verification and closeout

Focused shared-finalization suite; full native; capture-serial build with padding
floor 840; existing stimulus inventory; OpenSpec validation and diff checks.
No device acceptance or upload planned for this prerequisite. No parent checkoff.

## Verification evidence

- Pre-fix real-input fixture: physical Off expected original NoteId 1, observed
  unrelated producer NoteId 3. The raw-source handoff is controlled directly;
  this proves the closure consumer, not an integrated deferred Record stop.
- Final focused suite: **19/19**, 7.285 s. Includes independent physical and stop
  closure probes, duplicate held input, separate channel identities, exact normal
  stop content, accepted pre-roll input, phase-denied input and retry.
- Mutation restoring both raw identity scans: physical closure fails 1 vs 3;
  synthetic stop closure fails 1 vs 2. Normal-stop content still passes. Restored
  source then passes the focused suite. This isolates identity resolution from
  the rest of the existing stop normalizer.
- Oracle correction: the initial normal-stop fixture expected four rows. Existing
  stopped-record projection emits the identity-qualified Off at phase zero; the
  existing wrap normalizer also emits an invalid-ID terminal Off at length-1.
  The fixture now pins those six rows without changing that behavior. This is
  current-behavior coverage, not permission to weaken the shared finalization
  contract. Capture ownership invariant 7 in LOOP_MIDI_STORAGE_AND_VALIDATION.md
  documents the stopped-record coordinate ordering.
- Capture-serial build: PASS, 51.161 s. RAM1 variables 96,160; code 392,376;
  padding **840**; locals 34,912. RAM2 variables 155,232; EXTRAM 9,312.
  FLASH code 1,041,800 (+200). Track/job layout pins pass.
- ARM compiler layout probe (real Track.h, native peripheral stubs): PendingNote
  **1,052 -> 1,056** bytes, Track **384 -> 384**. Native PendingNote **1,072**,
  receipt offset 12 (existing padding). The firmware ELF has no debug type table;
  sizes above come from object-symbol extents, not a debugger estimate.
- Existing stimulus inventory listed with `.venv/bin/python
  scripts/host_midi_hitl.py stimulus list`; reuse `STIM-BASE-2-ONE-OD-01@v1` when
  evaluating the integrated path. No canonical stimulus edit, device upload,
  serial/MIDI run, SD operation, or timing/content acceptance is claimed here.
- Guides/execution bounds updated. No new decision or owner; implements existing
  DEC-093. Playback order, overlap and ledger policy unchanged. No release ships.

- Full `pio test -e native`: **2,508 passed / 7 skipped**, 2,515 total, 164.852 s;
  no failed/errored suites. Production source restored before this run.
- Strict OpenSpec validation and `git diff --check`: PASS.
- Runtime state/current work and task progress updated. No task checkbox changed;
  OpenSpec remains **50/176**. No new architectural exclusion or decision.
