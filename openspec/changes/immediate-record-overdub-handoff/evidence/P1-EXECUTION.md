# P1 — controlled verification execution

Scope: tasks1.1–1.6 and readiness2.1 of immediate-record-overdub-handoff. No firmware behavior changes, upload/reset/content/SD operation or new finalization resource admission. Existing dirty source remains authoritative baseline; test/measurement findings may return BLOCKED rather than generate production tasks2.2.

## Handoff proof suite

New `test_immediate_capture_handoff` wraps the actual shared-finalization harness with the same rolling-enabled compilation used by existing rolling retirement tests. No copied domain state machine. Clock/pool/I/O boundaries are native; real button, manager, Track, Loop, normalization, undo and persistence functions remain linked. Native boundary registration is test tooling only.

Run modes: default controls characterize current sealed ownership and independent producer admission. `IMMEDIATE_HANDOFF_CASE=<name>` selects each intended-acceptance case independently. These cases deliberately keep desired assertions RED until production implementations exist; default controls never imply the immediate behavior passes. Each explicit profile requires its named case and intended assertion; compile/link failures are not counterexamples. Normal native runs must not be made artificially green by weakening those assertions. Register the acceptance cases normally when their implementing slices become GREEN.

Fixed oracles:

- `start`: production clear/32-bar Record/stop, no worker turns (A private), real button request, next accepted pair must enter B now with A still pinned. Existing publication-gated state should fail; record row/ID/source diagnostics before assertion.
- `stop`: use actual direct Track start, not a forced state/ready stub, to bypass the independently tested manager gate; B accepts distinctive input, Stop B must cut it now and independently retain A/B. A row sent after the requested Stop must not enter B. Existing pending-owner/custody guard should fail separately from Start.
- `source`: while A private, direct B starts at a known A same-pitch note's hold phase; source participants must contain A's accepted NoteId and correct geometry, not an empty/stale pass/cache. Record actual source rows and pending changes. Independent expected source is A's explicitly accepted pair.
- `empty`: known minimum-length-filtered A, direct B has accepted distinct rows; drive actual Record workers while A remains owner. Empty cleanup must preserve B content, interval/geometry and Capture phase. Existing reset is a suspected, not assumed, failure until run.
- late publication/retirement controls: direct B live while A drains; A's range identity/rows cannot mutate during preparation; publication and undo state remain stable after extra worker turns and B rows survive. Persistence is checked only for dirty-marking, not exactly-once admission. The FIFO may release independently of retained source presentation; explicit pending/fault outcomes are reported.

Native drains are capped (20,000 owner turns) and print the last phase/owner state. Stop at a concrete unavailable dependency rather than manufacture B/C sealed state. C's full trace remains unqualified if B cannot seal.

## Independent directions

- ABI/reserve measurement uses scratch-only host/ARM probes against actual headers and allocator/reservation operations; host/target sizes, directory/tail/body ownership and reader retirement are distinct. A parameterized candidate price is not a supported envelope.
- Visual proof suite lives in a separate new path; source miss/work exposure, pairing/edit/window oracle, source supersession, presentation and wakeup are checked where actual harness seams support them. No source observer may return domain success.

## Verification and completion

Exact commands/results, failures versus controls, candidate price and remaining missing primitives will be recorded in HANDOFF.md, CUSTODY.md, VISUAL.md and READINESS.md before task checkoff. Full native regression gate follows stable default controls; intentional RED profiles are separately named and never counted as passing implementation. No shipping build is required solely for native test/doc changes; ARM object-size probes are not firmware build/boot proof. One focused independent review may inspect the non-trivial proof/contract after findings converge.

Status: fixtures and measurements run; see HANDOFF.md/CUSTODY.md/VISUAL.md/READINESS.md. Default controls are not immediate-handoff acceptance, and firmware readiness remains separately classified.
