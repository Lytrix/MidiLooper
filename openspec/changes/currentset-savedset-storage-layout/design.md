## Context

Current runtime persistence already writes `MidiLooper/current/workspace.bin` and per-slot
`loop_TT_SS.bin` files via deferred, chunk-bounded slices in `StorageManager`.
This is the correct shape for mutable live edits because only dirty slots need
payload rewrites. The open issue is layout policy split:

- Live **CurrentSet** should optimize write amplification and stop-path latency.
- Immutable **SavedSet** should optimize file count and browser/catalog reads.

Related shipped and in-flight references:

- `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`
- `docs/DELIVERABLE_TRACKING.md`
- `openspec/changes/workspace-session-persistence/`

Hot-path constraints:

- No synchronous full flatten/write on record/overdub stop.
- No heap walks or large transient allocations in capture/stop paths.
- Continue chunk-bounded payload persistence (`StorageLoopIo`) and deferred FSM.

## Goals / Non-Goals

**Goals:**

- Keep CurrentSet mutable writes incremental at slot scope.
- Add slot-summary metadata in `workspace.bin` for fast slot-list rendering/search.
- Define immutable SavedSet packaging with low file count (`set.bin` + `loops.bin`).
- Keep boot/recovery resilience via atomic temp→verify→rename and completion magic.
- Keep read compatibility for existing CurrentSet v6 trees.

**Non-Goals:**

- Replacing deferred-save scheduling FSM in this change.
- Introducing new jam/session persistence fields (M10 scope).
- Reworking note-edit pass model or undo semantics.
- Changing capture pass chunk wire format in `StorageLoopIo` unless required by
  SavedSet pack index compatibility.

## Decisions

### Decision 1: Split by mutability boundary

- **CurrentSet:** `workspace.bin` + per-slot loop files stays the live format.
- **SavedSet:** `set.bin` + packed `loops.bin` becomes snapshot format.

Rationale:

- Live edits need low write amplification (slot-local rewrites).
- Snapshots are immutable and benefit from fewer files.

Alternatives considered:

- Single mutable monolith: rejected (high rewrite cost and wear for small edits).
- Two-file mutable CurrentSet (`workspace.bin` + packed loops): rejected for same
  rewrite amplification problem unless a log-structured segment GC layer is added.

### Decision 2: `workspace.bin` is the slot browser index

CurrentSet and SavedSet metadata will include slot-summary rows sufficient for
slot-list UI and catalog filters without opening loop payload files.

Rationale:

- Fast metadata search is I/O bound on SD directory/file opens; index-in-meta
  avoids per-slot file reads during browse.

Alternatives considered:

- Compute slot summaries by opening each slot file on browse: rejected (slow).
- Sidecar summary file per slot: rejected (more files and update complexity).

### Decision 3: Keep atomic per-file commit semantics

CurrentSet slot writes and SavedSet final file commits remain staged:

- write temp
- verify completion marker / expected size/index
- rename to final

Rationale:

- Existing resilience model isolates corruption and supports recovery-point chain.

Alternatives considered:

- In-place overwrite: rejected (power-loss hazard).

### Decision 4: Transport stop does not imply full dirty by default

Default stop path should not mark all 64 slots dirty unless a specific
maintenance/migration mode requests full rewrite.

Rationale:

- Full-slot rewrite at stop negates incremental write gains and increases wear.

Alternatives considered:

- Always full dirty on stop: rejected for wear/latency.

## Risks / Trade-offs

- **Risk:** SavedSet packed format diverges from CurrentSet file model.  
  **Mitigation:** explicit per-set `containerVersion` and format discriminator in
  SavedSet `set.bin`; loader dispatches by format.

- **Risk:** Slot summaries in meta can drift from payloads if update hooks miss
  mutation paths.  
  **Mitigation:** update summaries only from same dirty-mark call paths and add
  native tests covering record, overdub, edit commit, clear, import.

- **Risk:** Migration complexity from existing SavedSet trees once implemented.  
  **Mitigation:** phased rollout: write-new/read-both, then optional repack tool.

- **Risk:** Larger `workspace.bin` from summary/index rows.  
  **Mitigation:** fixed-width compact rows and bounded fields; no dynamic strings
  in hot-path writer.

## Migration Plan

1. **Phase A — CurrentSet policy hardening**
   - Keep existing CurrentSet file layout.
   - Remove default full-dirty on transport stop.
   - Add slot-summary rows in CurrentSet `workspace.bin`.

2. **Phase B — SavedSet packed format introduce**
   - Implement SavedSet writer for `set.bin` + `loops.bin`.
   - Implement loader for packed SavedSet into CurrentSet.
   - Keep CurrentSet load path unchanged.

3. **Phase C — Compatibility**
   - Read both legacy SavedSet-per-slot (if any) and packed SavedSet.
   - Prefer writing packed SavedSet for new snapshots.

4. **Rollback**
   - CurrentSet path unaffected; disable packed SavedSet writer behind feature flag
     and continue CurrentSet-only runtime persistence.

## Open Questions

- Should SavedSet keep optional per-slot diagnostics files for offline tooling?
- Which exact summary fields are mandatory for first slot-list release:
  `hasData`, `bars`, `noteOnCount`, `lastEditedUnix`, `crc32`?
- Should recovery-point checkpoints remain per-slot files only, or gain packed
  export for storage compaction?
