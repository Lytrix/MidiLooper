# S4f.18 — bounded Record tail-repair consumer

Status: verified prerequisite; production normalization integration remains open. Baseline d34b7976; unrelated HITL changes preserved.
Authority: SHARED-CAPTURE-FINALIZATION.md, DEC-093 — shared capture finalization,
and the fixed normalization inventory in S4F18-NORMALIZATION-EXECUTION.md.

## G0 evidence and G1 boundary

VERIFIED: `prepareRecordStop` projects overflow before `sealCapture`; seal may sort,
assigns missing identities, repairs open tails, then filters short pairs. Existing
`LoopStopFinalize::finalizeWrapWindowOnStore` first walks rows in source order,
keeping the latest encountered tail On per channel/pitch and clearing it on a
subsequent window Off. It then scans all rows repeatedly for head wrap/later Offs.
This requires neither a whole-take mirror nor a per-event ordering array: one
12-byte lane per MIDI channel/pitch can retain active tail tick, earliest head Off
and latest Off. Source traversal order remains significant and must be preserved.
The consumer must not silently sort or project rows itself.

Extend the existing LoopStopFinalize policy with caller-owned bounded scratch and
an incremental cursor. No new manager, queue, capture owner or publication route.
Scratch is exactly 16 * 128 lanes * 12 bytes = 24,576 bytes, independent of take
length. It belongs to one admitted preparation role, not each job/slot/Track.
The primitive does not allocate or add a resident firmware field. Future integration
must prewarm/reuse that role and include its memory in admission; no production
Record dispatch or full normalization is claimed by this isolated consumer slice.
This implements tail repair only. Ordering, minimum-length filtering, geometry,
closure input, Record admission and playback integration remain required.

## G2 fixed oracle and review

- Reset initializes at most Q lanes per turn; no begin-time array clearing.
- Input consumes exactly one supplied event per call, without allocation or source
  mutation. Geometry/policy are copied at begin, owner generation remains current.
- Head/tail overlap, same-tick ingress order, retriggers, channels, outside-window
  Offs and velocity-zero NoteOn must match the existing helper's decisions.
- Missing tail closures use the stop phase unless earlier than the On, then the
  last loop tick. Synthetic Offs retain the existing invalid NoteId policy.
- Enumeration examines at most Q lanes. An emitted Off remains pending until an
  explicit acknowledgement; failed downstream append can retry without loss or
  duplication. Completion is idempotent and denies further input.
- Stale generation, invalid MIDI note lane, zero quantum and premature finish deny
  before state/output mutation. Caller keeps generation and exclusive workspace
  alive until discard; discard permits explicit restart.
- Fixed 2,048-key domain supports all MIDI channel/pitch identities. Test more than
  2,048 total input rows; no smaller overdub event limit is introduced.
- Independent hand-written inventories plus deterministic differential checks
  against the real existing helper. Compare synthetic event inventories: legacy
  unordered-map enumeration does not promise an order between independent lanes.

G0 PASS; G2 PASS WITH TIGHTENING: do not call an input stream complete before its
source is exhausted; upstream source identity/ordering/projection remain obligations
of the existing preparation owner. No partial result is publishable. Workspace
must be exclusive (as with existing borrowed prewarmed helper scratch); invalidate
its generation before reuse. Consumer reads no current selected slot or globals.
No immutable raw range, undo, storage request or live successor is mutated.

## Verification recipe

Focused native tail-repair and real shared-finalization fixtures; mutate wrapped
pair suppression, stop-phase clamp and acknowledgement to demonstrate discrimination.
Full native, capture-serial build and layout/floor checks (padding >=840), strict
OpenSpec and whitespace checks. Existing registered STIM-BASE-2-ONE-OD-01@v1 remains
the integration stimulus. No upload or device timing claim. No parent checkoff.

## Implementation evidence

`TailRepairCursor` borrows scratch and the caller's generation. Input changes no
source row; enumeration retains its lane until acknowledged. All worker methods
use `LOOP_COLD_MEM`; no production Record caller is switched in this checkpoint.
ARM object-symbol measurement: cursor **48 bytes**, workspace **24,576 bytes**.
No job-ring, Track, Loop or per-slot runtime field was added. This does not claim
admission for simultaneous workspaces: integration must budget and prewarm the
exclusive role before binding it to a job.

Six native policy fixtures pass. They pin explicit inventories, source-order
same-tick behavior, wrapped half-length strictness, outside-window later Offs,
close-phase fallback, short loops, stale generations, invalid MIDI lanes and
acknowledged retries. All 2,048 MIDI keys are exercised with 4,096 input rows.
Eighty deterministic arbitrary-order input cases compare synthetic inventories
against the real `finalizeWrapWindowOnStore` implementation. Legacy hash order
between independent lanes is not an output-order contract.

Three source mutations were independently rejected by the fixtures: removed wrap
suppression, removed close-before-On fallback and premature output advance. Each
failed on assertions, not compilation; the original source was restored before
full regression/build. Logs: `/tmp/tail-mutation-{wrap,phase,ack}.log`.

Session closeout: implementation extends DEC-093 — shared capture finalization;
no new owner, queue, policy exclusion or input cap. No new decision entry required.
Nothing shipped; deliverable tracking unchanged. Parent task remains unchecked.
The next slice must address bounded ordering/minimum-length filtering and integrate
normalization with the frozen Record source before changing production admission.

## Final verification

- Full native: **2,533 passed / 7 skipped**, 2,540 total (`/tmp/tail-final-native.log`).
  Includes the six new policy fixtures and 31 real shared-finalization fixtures.
- Capture-serial build passed (`/tmp/tail-final-build.log`), with unchanged footprint:
  FLASH code 1,044,504; RAM1 variables 96,160, code 392,376, padding **840**,
  locals 34,912; RAM2 155,232; EXTRAM 9,312. New helper is currently unreferenced
  by firmware callers, so linker removal means this is not its integrated budget.
- ARM measurement: 48-byte cursor / 24,576-byte workspace; no resident field added.
- Strict OpenSpec validation and `git diff --check` passed. Progress remains
  **50/176**; no parent checkoff. No upload or hardware acceptance claim.
