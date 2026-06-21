## DEFERRED Requirements

### Requirement: editPasses cardinality cap

**Deferred to `pool-budget`.** Fixed row-count cap is superseded by heap admission and
**reclaimUnreferencedDisabledPasses**. Do not implement **`MAX_EDIT_PASSES_PER_LOOP`** in
**loop-ownership-hardening**.

See **`openspec/changes/pool-budget/specs/timeline-passes/spec.md`**.
