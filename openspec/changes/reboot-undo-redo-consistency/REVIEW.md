# Contract adversarial review

2026-10-29; read-only self-review against `7c3f69ff`. This is not the independent implementation review required before slice checkoff.

## TIGHTENING — allocation refusal must not publish an empty frame

**Evidence:** `collectQualifiedCommittedDisplayNotes` and compose filtering may reserve display storage; empty and unavailable already share `false` in the existing collector.

**Failed assumption:** metadata qualification alone implies successful content copying.

**Required change:** adoption must require both current interval qualification and successful admitted compose/filter completion. A refused copy preserves the old coherent frame/key; it is not authoritative emptiness. Use existing admission/refusal observation, not a new allocator owner.

**Validation:** deterministic display reserve refusal then recovery, asserting unchanged frame/key on denial and exact replacement on recovery, plus qualified-empty success.

## COVERED — same-window handoff and stale-root bypass

The captured `hold_gap` with clean smaller cache is the concrete target. The contract requires replacing the held frame only with current covered content; it never adopts an unpublished root or changes MIDI membership. Same-window, scrolled-window, stale and partial cache controls discriminate it.

## COVERED — Record/overdub, overlapping/deferred work and alternative entries

Record→Stop→Record, overdub→Stop→successor, stopped undo→Play and playing undo/redo remain distinct controls. Existing holds remain when a current complete source is missing. Save in flight must not be drained/reset by display, and display cannot mutate pass state. Fixture linkage may not stub the tested global undo or restore route.

## COVERED — identity and lifetime

Track/slot switch, clear/load replacement, root completion and clock/transport transitions must reject prior qualification and recompute the current key. No borrowed source pointers survive the existing compose call. The redo packet records references/readability through staging destruction and ordinary teardown; production release changes remain blocked.

## COVERED — long loops, capacity, memory and MIDI

Reuse current window qualification and existing compose storage. No new role, per-slot resident field, whole-pass gather, full validate on Stop, extra MIDI-edge work or second activation authority is authorized. Full native/build evidence and hardware timing remain separate from logical tests. Existing supported-envelope coverage must remain green; no new capacity claim is made.

## G2 REVIEW

**PASS WITH TIGHTENING** for fixture work and the display contract once the allocation-denial condition above is incorporated. **BLOCKED** for a redo production repair: its loaded-slot resource boundary is unresolved and has no implementation tasks. An independent focused implementation review is still owed.

The allocation-denial condition is incorporated in design and tasks. Planning validation: `openspec validate reboot-undo-redo-consistency --strict` PASS; CLI reports all four planning artifacts complete. This is not fixture, native, firmware-build or device acceptance; those checks were NOT RUN in this planning phase.

## Focused independent implementation review

Independent reviewer session `ses_e77f11e1fffecCxOi3eBCwW2uj` reviewed the three display production files and named fixture/support changes against `7c3f69ff`; no build/test was run by the reviewer. Verdict: **REQUEST CHANGES**, one concrete blocker: the later `appendOverdubPassDisplayNotes` projection can refuse after the initial PassesResolve refusal check, then the final slice can clear dirty bars/stamp currency despite incomplete wrap reconstruction.

The repair observes the refusal counter around wrap completion too, returning before deferred-wrap flag clearing, cache content replacement or dirty-bar clearing. New serialized/restored wrap-held fixture admits the preceding window projection and its copy, denies only the later wrap completion through a native-only allocator countdown, and demonstrates the pre-fix failure (`denied wrap completion must leave its slice retryable`). With the guard it preserves prior frame/key and unqualified dirty slice; after resource recovery and normal publication/cache workers it paints the exact canonical wrapping span once. The recovery expectation uses snapshot DisplayNote geometry (`start > end`), not the compatibility path's already-split head/tail representation.

The reviewer also listed missing interleaving coverage rather than inventing production changes. Apply subsequently added rejected-overlay, undo during a resolved/in-flight frame, cross-track/clear, and clock-stop/restart with queued-save controls. Remaining matrix gaps and the unrelated redo/Record characterizations are left visible in task/evidence status. This is one focused review plus concrete-finding retest, not a broad review loop or a claim that the reviewer independently ran the suite.

## Source-reuse contract red team — before mutation

**TIGHTENING:** `coverageBuildNeedsService` excludes Counting/AwaitAdmission and generic shared builders; the original retirement predicate alone is insufficient for descriptor identity reuse. Reuse must additionally exclude active generic/bridge builds, retained gather, owned coverage-source borrows, and complete current/ready/retained views that name that revision. These are existing observable owners, not new locks. Add negative reader-boundary fixtures. The original physical-release operation keeps its existing predicate and is not newly wired.

**COVERED:** exact ordered pins/chunk IDs, older Retiring role only, capture/session/finalization exclusions, per-slot runtime ownership and no release/re-retain prevent reusing a live or changed source. A stopped/clock-changed request must still pass those facts; no deadline or active-slot authority changes. Materialized merged events remain consumer-owned; admission reuses no event buffers. Persistence/root publication may share the same bodies safely because refs/alias are unchanged, not freed or mutated.

**COVERED:** matching and index work stay in the existing source-admission owner, with no new call site, queue, field or capacity role. No additional timing guarantee; metadata matching is content-dependent and device cost remains open. No full validation or persistence work is added on Stop. Long-loop/maximum-active-role correctness still needs the stated regression/device gates.

**G2 REVIEW: PASS WITH TIGHTENING**, incorporated in the packet before production mutation. This self-check is not the focused independent source-lifecycle implementation review.

## Focused independent source-lifecycle implementation review

Reviewer session `ses_e77a7f7a4ffeCBzhQtwiUkWnFo`: **APPROVE**, no concrete violations in the new source slice. Traced both admission callers, exact inventory matching, reader protections, descriptor handoff/index publication and the separately callable physical retirement operation; checked packet, corrected saved-slot oracles and literal MIDI/NoteId/refcount/alias assertions. No third role, target field, release/re-retain, new source owner or admission-path retirement was found. This review excludes earlier display changes already reviewed separately.

The reviewer did not run builds/tests. Dedicated bridge-build, coverage-source borrow, retained-view, mismatched body/slot identity, same-count reorder and long-loop rolling-source reuse tests were identified as remaining coverage limitations; inspected guards are not claimed as those fixtures. Full native/build results and the later three display/slot/save cases are main-session evidence, not retroactively attributed to the reviewer.
