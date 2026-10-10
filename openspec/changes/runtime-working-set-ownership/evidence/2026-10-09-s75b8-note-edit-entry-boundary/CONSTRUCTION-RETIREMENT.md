# S7.5b.8b — construction-owned bounded retirement

## Frozen G2 — execution packet (before production edits)

Scope: extend `LoopSnapshotConstruction` only, with a caller-owned non-owning scalar release cursor and existing graph release walker. Authorized by CURRENT_WORK Now implementing and the unchecked bounded entry/focus task. No pending-box adoption, source queue rewrite, entry/main dispatch, publication/timing policy, new manager/queue/lease, instrumentation or device operation.

G0 — implementation discovery: constructor, partial/failed/idle note/event workspaces, private membership-copy chains, candidate roots, frozen candidate and parent are traced in the session scratch discovery. Existing immutable release cannot retire mutable workspace aliases. G1 — owner/design: extend the construction owner, reuse `releaseSnapshotReference`, retain synchronous destruction compatibility for unadmitted objects. Decision review: DEC-047 — transactional fragment, DEC-073 — committed-pass coverage and DEC-075 — memory-park/retry remain unchanged. No new owner/representation/policy or reassessment trigger; no new decision entry required.

### Contract and lifecycle

Cursor is noncopyable/nonmovable and single-use. It owns no graph, workspace or vector. It must remain alive until admitted owner cleanup is complete; owner must remain alive for all calls. Admission is scalar-only and irreversible. Normal complete/step/freeze/handoff are blocked from acceptance onward. Candidate/changes borrows are observation-only; callers exclude semantic borrowers, concurrent mutation and weak resurrection while releasing private exclusive graphs. Object destruction/address reuse is not qualified by the receipt.

Repeated begin with the same admitted cursor returns current progress without restarting. Fresh/foreign/overlapping cursor admission is rejected. Wrong-owner begin/step/query must not alter the valid cursor or owner's progress. No cancellation/discard/reuse of an admitted cursor; completed query and zero grant are observational. A drained owner shell may be destroyed safely. Old callers that never accept release retain synchronous destruction.

### Unit oracle

A grant performs at most one existing graph ownership grain, one vector back-row removal after its retained edge has been separately drained, one empty allocation/shell retirement, or one phase/terminal transition. Mutable graph references MUST move to a const view before invoking the walker and restore afterward without an extra surviving shared owner. Workspace aliases/copy chains and vector rows drain before construction/candidate/frozen/parent roots. Exclusive snapshot roots drain span and event separately before their shell. No variable-length traversal or full vector clear in a grant. Allocator latency/failure, portable STL deallocation complexity, allocation-free cleanup and device timing are not qualified.

### Fixture plan (frozen before behavior edits)

Use `test_loop_content_resolution`, linking the real `LoopSnapshot.cpp`, and a new `construction_release.inc`. Build payloads through real constructor/step/freeze/failure-injection paths. Native-only read-only observation access may inspect workspace edges/capacity; it may not inject state. Acceptance/withheld/zero grants retain rows, allocations and weak nodes; publication APIs reject after acceptance. Literal multirow changes distinguish one-row progress. Rebegin/overlap/foreign owner preserve valid progress. Real partial note AND event multipage copies, failed construction, completed idle workspaces, shared frozen candidate/parent preservation, private last-owner graph reclamation, empty/missing parent, completed observational query and safe destruction are required. Fixed fixture drain limits reflect literal topology, not a device budget. Missing APIs are compile/link setup failures, not behavioral RED evidence.

### Verification and controls

Run focused construction and entry suites. Independently mutate eager acceptance, two-unit stepping, and qualification/publication blocking; each must fail explicit assertions. Save production checkpoints and restore byte-identically before the one focused independent review and broad checks. Run full `pio test -e native`, `pio run -e teensy41-capture-serial`, strict OpenSpec validation and `git diff --check`. Recheck the flagged parent withheld-owner RED, then restore ordinary artifacts. No upload. Full task, feedback and hardware gates remain unchecked.

## Results

Implemented and verified; primitive remains unused by live pending-box/entry/main paths. Eight fixtures were written before production behavior edits. Missing APIs were setup failures, not behavioral RED claims.

APIs: `LoopSnapshotConstruction::beginRelease`, `stepRelease`, `releaseResult`; noncopyable/nonmovable single-use `ReleaseCursor`, explicit Idle/Progress/Complete/Rejected. Existing constructor/destructor compatibility retained; admission gates complete/step/freeze/handoff. Fixed phases drain note/event workspaces, change rows/buffers, mutable roots, candidate roots, frozen span/event/shell, parent span/event/shell. No new detached owner or per-Loop cursor. Construction adds one transient cursor-identity pointer, not payload ownership.

Real-path fixtures prove literal row removals and two private copied membership pages (32 scalar edges) with at most one edge removal/node expiry per grant. Observed totals: empty/missing parent14; three note/event rows20; previous-edge rows18; failed partial note225; partial event/last parent owner391; shared idle workspaces52; exclusive unchanged frozen242; private frozen last-owner408. These are fixture work counts, not device budgets or timing.

Verification:
- Focused construction97/97, final construction+entry166/166. Logs `construction-focused.log`, `construction-grains.log`, `construction-restored.log`, `construction-final-focused.log`.
- Independent controls: eager acceptance3 assertion failures, doubled quantum8, omitted handoff gate1; exit1 each, also SIGQUIT/SIGFPE/SIGHUP and harness ERRORED. See `construction-control-acceptance.log`, `construction-control-quantum.log`, `construction-control-block.log`. Restored byte-identically before review/broad checks (`construction-restoration.log`).
- One focused static independent review APPROVE, no concrete reachable defects; reviewer did not run shell checks. [Review record](CONSTRUCTION-RETIREMENT-REVIEW.md).
- Full native2,990 total/2,983 passed/7 skipped/0 failed,112 PASSED suites (`construction-full-native.log`).
- Default capture SUCCESS (`construction-capture.log`): RAM1 variables96,192/code391,064/padding2,152, floor840 (headroom1,312), locals34,880; RAM2 163,200; EXTRAM9,312. All unchanged; no budget rebase/upload. Cold flash code is not a RAM1 code claim.
- Parent oracle69 pass/1 withheld-owner assertion failure, plus SIGHUP/harness ERRORED/exit1 (`construction-parent-oracle.log`). Ordinary artifacts restored166/166/exit0. No full-task checkoff.

Restored checkpoint SHA-256: header `0de8d8d2d1ed5d76a25c2f8615f1a3dd8c199e702a7c6f038b9770bec4bfa05c`; source `9dc19640548bac850248b1cd560d5fe95bf34583b0540f514711c0d66edd7692`; fixture `45280e1dec3ce1aa718b40287b12dad0482ba78ff1bea0bf659854b6924f1c3f`; harness `babea3b4545f76cf01fa0a43814f3fddbc5585858a71c0586ff376f20fb06f40`.

## Closeout and remaining scope

Runtime/current work, deliverable, active design/tasks, storage guide and execution bounds updated. This implements existing-owner bounded retirement under DEC-047 — transactional fragment / DEC-073 — committed-pass coverage / DEC-075 — memory-park/retry; no new architecture owner/policy/exclusion, so no new decision entry. No overlap/ledger/playback-order logic changed. Strict OpenSpec and whitespace results are saved in `construction-closeout-validation.log`.

Pending-box cancellation/adoption and lifecycle across Loop/Track destruction, inventory adoption, qualified caller continuations and atomic session/FSM/main dispatch remain open. Whole synchronous queue/reset/destruction helpers have not become bounded by this primitive. Full bounded-entry task, feedback and hardware gates stay unchecked. No commit/push/PR, upload/device/SD/stimulus/marker/instrumentation action; unrelated dirty changes preserved.
