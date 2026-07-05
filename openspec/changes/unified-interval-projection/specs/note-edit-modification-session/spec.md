## MODIFIED Requirements

### Requirement: NOTE_EDIT transaction consistency

Within a single NOTE_EDIT geometry update, the system SHALL:

1. Read **transaction baseline** (**`baselineMap`**, **`commitBaseline`**) as immutable input for the current **edit driver** (D19)
2. Read **edited geometry** — **`EditorSelection`** + linear causing spans (`focus.last` for **`primaryNote`**; see **`edit-session-action-geometry`** spec § Edited geometry)
3. **Orchestrator** — determine changed causing notes and eligible pairs (D17)
4. Run **Edit projection** — **`projectEditIntervalsForAnalysis`** with **`ProjectionContext`** (`ProjectionType::Edit`) before analysis; replaces **`normalizeWrapToLinear`**
5. Run **`analyzeEditSessionInteractions`** (pure; supplied causing inputs and post-projection spans only)
6. Run **`groupEditSessionInteractionsByTarget`** (ephemeral; grouped per target)
7. Run **`resolveConstrainedGeometry`** (pure)
8. Run **`buildEditSessionActions`** (edit session action builder; inputs: constrained geometry, edited geometry, transaction baseline, live store; does not mutate live store)
9. Run **`applyEditSessionActions`** (**edit session action apply** — sole live store writer; includes boundary split)
10. Run **`normalizeWindow`** on edit closure at micro boundary only

The system MUST NOT interleave storage mutation with analysis. The restore-first staged pipeline is **retired** in favor of rebuild + **RestoreNote** actions.

Edit inventory, motor sync, and selectable display lists SHALL read display geometry through **Display projection** only — not through independent wrap linearization or display-tick remap of storage authority.

#### Scenario: Edit inventory uses projection not storage remap

- **WHEN** `filterSelectableDisplayNotes` or edit motor inventory is built
- **THEN** window filtering uses Display projection / inclusion helpers
- **AND** storage ticks are not remapped for mutation authority

#### Scenario: No normalizeWrapToLinear in geometry path

- **WHEN** move/length/pitch/add geometry runs after UIP migration
- **THEN** **`normalizeWrapToLinear`** is not called
- **AND** Edit projection supplies analyze inputs
