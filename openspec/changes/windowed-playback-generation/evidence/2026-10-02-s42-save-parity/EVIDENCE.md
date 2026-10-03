# Slice 4.2 — save parity is judged on the serialized root (F-6)

**Change:** `windowed-playback-generation` · **Task:** 4.2 (**landed; the task stays open on a named repair**) ·
**Date:** 2026-10-02 · **Branch:** `feature/windowed-playback-generation`
**Packet:** [`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) § Increment 4.2
**Contract:** the spec's *Checkpoint parity for undo and save* — "The published root SHALL remain the immutable
checkpoint authority for undo and save. A checkpoint's parity SHALL be judged against the loop's **current**
revision … a persisted body SHALL be equivalent to the root it was serialized from, and neither SHALL consult the
window's lag"; scenario *Save parity is judged on the serialized root* — "**WHEN** a body is persisted and later
restored **THEN** the body is equivalent to the root it was serialized from **AND** the restore's coverage outcome is
either current or named as stale". Design **D7** names this slice's target explicitly: "today's
`storePublishedSnapshot(…, kSnapshotCoverageCurrent)` at the restore edge is the case `F-6` has to falsify".

---

## What landed

| File | Change |
|---|---|
| `test/test_storage_loop_io/test_storage_loop_io.cpp` | `test_lsv1_save_parity_is_judged_on_the_serialized_root` — the spec's scenario, driven through the **production writer** (`writeLoopPersisted`) and the **production restore edge** (`stepLoopSnapshotBodyFormatV1LoadParse` → `applyLoadedLoopSlotBodyCommit`), asserting equivalence and the **named** coverage outcome. |
| `src/StorageLoopIo.cpp` | **Comment only, no behaviour change:** the restore edge's over-claiming `// DEC-073: the restored passes and the installed root are the same committed state, …` is replaced by the boundary statement — the claim rests on the **writer's** admission (RC2), not on anything the reader can re-derive. |

**Why the fixture is new rather than extended:** the gap was verified by reading the code, not assumed. The LSV1 root
section persists `loopId, loopLengthTicks, loopStartTick, nextNoteId, nextEventId` + content + memberships and **no
provenance figure of any kind**; the reader validates section presence (`sawLoopMetadata`, `sawLoopSnapshot`) and each
section's *internal* structure (unique ids, `nextNoteId > maxNoteId`, membership refs resolving, bar indices) and
**never the root's correspondence to the persisted passes**; and the existing restore-edge fixture
(`test_lsv1_load_commit_rebuilds_undo_and_preserves_root`) asserts only presence plus `publishedSnapshotIsCurrent()`.
So "the body is equivalent to the root it was serialized from" was **not** asserted anywhere at the restore edge, and
nothing judged the restore's coverage at all.

## The oracle, case by case

| Case | Observed |
|---|---|
| the source state | a real record commit, drained to a **settled** root: `loopUsesLoopSnapshotBodyFormatV1ForSave` true and `checkpointParity() == Current` before the write |
| the body | `writeLoopPersisted` emits LSV1 (`probeLoopSnapshotBodyFormat == SupportedV1`) |
| **(A) equivalence at the restore edge** | the installed root is equivalent, content for content, to the root that was serialized (`assertSnapshotsEquivalent` — ids, length, tick, note/event counters, resolved display notes, resolved window events). Equivalence is asserted as **content, not address**: the round trip rebuilds the root, and the first version of this fixture failed on a pointer-identity assertion of mine (`Expected 0x…0300 Was 0x…1C20`) before that was corrected |
| **(B) the committed state arrived** | the restored loop carries the body's `loopId`, `loopLengthTicks`, a record pass, and the note-id figure the root itself was enumerated from (`peekNextNoteId() == publishedSnapshot()->nextNoteId()`) |
| **(C) the outcome is named** | `checkpointParity() == CheckpointParity::Current`, `publishedSnapshotIsCurrent()`, `publishedSnapshotIdentity() != nullptr` — 4.1's name, applied to the save consumer |
| the admission's evidence | the save-side guarantee (only a *settled* root is embedded) is **cited**: `test_write_loop_persisted_requires_a_settled_published_snapshot` in the same suite owns it, and is not restated here |

## The falsification — measured, not asserted

D7's target is that the restore edge claims currency **without reconciling**. A temporary probe (removed before
commit — it would pin wrong behaviour) built a body that the reader accepts but whose two sections cannot describe one
committed state: the metadata section carries `nextNoteId = root.nextNoteId() + 7` while the root section carries its
own. The probe's operative assertions, **all of which passed**:

```
  applyLoadedLoopSlotBodyCommit(loop, track, 0, true, oracle, std::move(loadedSnapshot));
  TEST_ASSERT_EQUAL_UINT32(snapshot->nextNoteId() + 7, loop.peekNextNoteId());              // the restored state
  TEST_ASSERT_EQUAL_UINT32(snapshot->nextNoteId(), loop.publishedSnapshot()->nextNoteId());  // … the installed root
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());                                       // presented as CURRENT
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CheckpointParity::Current),
                        static_cast<int>(loop.checkpointParity()));                          // … and NAMED current
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshotIdentity());                                    // … as the current identity
```

**Observed:** today's path installs a root that does not come from the restored state, and reports it as
`CheckpointParity::Current` with a non-null `publishedSnapshotIdentity()` — the "silently current" outcome the
requirement forbids. The only thing that excludes such a body in production is the **writer's** admission, and the
body records no evidence of it, so the reader cannot tell the two apart.

**A side-finding, recorded because it changed the measurement:** the first probe attempt put the divergence on the
*oracle* and measured nothing (`Expected 17 Was 10`) — the reader **merges the metadata section into the oracle**
(`mergeLsv1LoopMetadataIntoOracle`), repairing oracle-side scalars before the restore edge sees them. The incoherence
has to be between the metadata and the **root** to survive.

## The repairs considered — one refuted, two owed

| # | Repair | Status |
|---|---|---|
| (i) | **Persist the root's coverage in the body** (the figure the restore edge actually needs) | **Owed — a format decision.** LSV1 v1 is pinned (`kLoopSnapshotBodyFormatVersion = 1`), the single header flag is strictly validated (`flags & ~0x0001 → Failed`, so a new bit is a version break), and the root payload has no provenance field to extend in place. |
| (ii) | **Cross-check the root's own figures against the committed state's** (cheap, no format change) | **Refuted by reading the code.** `Loop::nextNoteId_` can advance while the committed state — and so a settled root — is unchanged: `EditStates/EditSelectNoteState.cpp:188` calls `Loop::allocateNoteId()` (`src/Loop.cpp:28`, `return nextNoteId_++`). A divergence guard would therefore name a **legitimate** body stale. |
| (iii) | **Reconcile the root against the body's own passes at load** | **Owed.** Needs a comparison across two representations (root memberships vs passes: ordering, overlaps, disabled passes) — a design task, not a silent slice change. |

## Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_storage_loop_io` | **52/52** (was 51; +1 fixture) |
| `pio test -e native -f test_runtime_working_set_ownership` | **65/65** (unchanged) |
| `pio test -e native -f test_playback_prewarm` (cited pin) | **190/190** (unchanged) |
| `pio test -e native -f test_midi_service_drain` | **38/38** (unchanged) |
| `pio test -e native` (full) | **2697 cases: 7 skipped, 2690 succeeded, 0 failed** (baseline 2696 → +1) |
| `pio run -e teensy41-capture-serial` (clean rebuild, 71.5 s) | **SUCCESS** — `variables 96,192` **unchanged** / `code 392,328 + padding 888` / locals 34,880, floor 840, **48 B headroom**, byte-identical to the seven previous slices (a comment plus a test file: no ITCM growth, no pin moved) |

## Red-proofs

Four mutations, each applied to a **named** file at an anchor whose **occurrence count** was asserted before the run
(the 4.1 lesson: an anchor in the wrong file reports "green" without being evidence), and each restored
**byte-identically** (`shasum -a 256 -c` over all three touched files):

| # | File | Mutation | Observed failure |
|---|---|---|---|
| M52 | `src/StorageLoopIo.cpp` | the restore edge claims `kSnapshotCoverageUnset` instead of the sentinel | the new fixture: `Expected 1 Was 3` — the named outcome degrades from `Current` to `CoverageUnrecorded` (and the existing restore-edge fixture fails too) |
| M53 | `src/StorageLoopIo.cpp` | the install is skipped (`if (!loadedLoopSnapshot)` → `if (true)`) | `Expected Non-NULL` — no root is installed at all |
| M56 | `src/StorageLoopIo.cpp` | the oracle is not applied (`applySnapshotToLoop` dropped) | `Expected 7 Was 4294967295` — the restored loop never receives the body's committed state |
| M57 | `src/StorageLoopIoBodyFormatV1.cpp` | the writer omits the root section | `Expected 1 Was 2` (the body is no longer LSV1) plus six writer/reader fixtures — the fixture is anchored on the **production save**, not on a helper |

**The bound of these red-proofs, stated rather than glossed:** all of this fixture's outcome assertions are the
*positive* case (`Current` where the body is coherent), so it cannot falsify an **over-claiming** parity — a mutation
that makes the restore claim currency more eagerly would leave it green. That is exactly why the over-claiming case had
to be a **measurement** (above) rather than a red-proof, and why the repair is owed.

## Not claimed

- **No behaviour change.** The slice's only production edit is a comment; nothing about what is installed, or when, moved.
- **The hole is not closed and is not narrowed.** The measured case still installs a stale root as current on a body
  that violates the writer's admission; the repairs (i) and (iii) are owed, and (ii) is refuted with its reason.
- **Task 4.2 stays open** on that repair. Its verification half — "the body is equivalent to the root it was
  serialized from" and "the restore's coverage outcome is named" — is landed, with the falsifying case recorded so the
  open item has a reproducer rather than a description.
- No deadline/timing claim, no device evidence. The 48 B of RAM1 headroom above the floor is unchanged, and no size
  pin moved.
