# Legacy API retirement after translation-unit extraction

**Kind:** architectural refinement  
**Applies to:** All translation-unit extraction plans (`Loop`, `EditManager`, `NoteEditFocus`, `StorageManager`, future mechanical splits)  
**Naming authority:** [NAMING.md](../00-authority/NAMING.md)  
**Workflow authority:** [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc)

---

## One-line goal

Separate **mechanical ownership extraction** from **API cleanup** by retiring legacy functions only after the architectural split has fully stabilized.

---

## Motivation

A translation-unit extraction has two independent objectives:

1. **Move ownership** into the correct architectural module.
2. **Simplify the API** by removing obsolete wrappers, aliases, and compatibility functions.

These have different review characteristics and should not be combined.

A PR that both moves code and deletes legacy entry points becomes harder to review, harder to bisect, and harder to verify.

Instead:

- extraction phases move code only;
- a dedicated final phase removes obsolete APIs.

---

## Principle

> **Move first. Simplify later.**

During extraction, preserve compatibility whenever practical.

Only remove legacy functions after:

- architectural ownership is stable,
- all call sites have migrated,
- tests no longer depend on the legacy API,
- no remaining extraction phase references it.

---

## Recommended lifecycle

```text
Original implementation
        │
        ▼
Extract ownership
(mechanical only)
        │
        ▼
Compatibility period
(aliases / wrappers allowed)
        │
        ▼
All callers migrated
        │
        ▼
Legacy Retirement phase
(delete wrappers)
        │
        ▼
Architecture stable
```

---

## During extraction phases

Translation-unit extraction phases should prefer moving implementation bodies without changing public or internal entry points.

Examples that **remain** until Legacy Retirement:

- compatibility wrappers
- alias functions
- deprecated helper names
- forwarding methods
- temporary adapters

Extraction PRs answer only one question:

> **Did ownership move correctly?**

Not:

> **Was the API simplified correctly?**

**Exception:** remove a symbol only when required for compilation after a move (e.g. duplicate definition). Prefer a thin forwarder instead.

---

## Legacy markers

Temporary compatibility functions should clearly indicate planned removal.

```cpp
// Legacy compatibility wrapper.
// TODO(refactor): Remove during Legacy Retirement after all callers
// use projectNoteEditDisplayNotes().
```

For public APIs, where appropriate:

```cpp
[[deprecated("Use projectNoteEditDisplayNotes")]]
```

Intent must be obvious: temporary, deliberate, scheduled for removal.

---

## Retirement criteria

A legacy function may be removed only when **all** of the following are true:

- Every internal caller has migrated.
- Native and HITL tests no longer reference it.
- No remaining extraction phase depends on it.
- Removal does not require behavioural review.
- The replacement API is considered stable.

If any condition is not satisfied, the wrapper remains.

---

## Dedicated Legacy Retirement phase

Every large translation-unit extraction may end with a final cleanup phase (e.g. **Phase LR** or **Phase 11** after structural Phase 10).

Typical responsibilities:

- remove compatibility wrappers
- remove alias functions
- inline obsolete forwarding methods
- remove deprecated helper names
- delete compatibility comments
- simplify the call graph

**No** ownership changes. **No** behavioural changes. **No** architectural movement. **API cleanup only.**

| Gate | Answer |
|------|--------|
| Ownership change? | NO |
| Transition change? | NO |
| Risk | low (delete-only after criteria met) |

**PR title pattern:** `refactor({module}): Legacy Retirement — remove obsolete wrappers`

---

## Benefits

### Cleaner reviews

Extraction: *Phase 7 only moved code.*

Legacy Retirement: *only removes obsolete wrappers.*

Each PR has a single responsibility.

### Easier bisecting

Behaviour regressions after extraction → ownership movement.

Regressions after Legacy Retirement → caller migration or delete mistake (narrower blast radius).

### Consistent workflow

1. Extract ownership.
2. Stabilize architecture.
3. Migrate callers (optional explicit sub-phase).
4. Retire legacy APIs.
5. Optional naming simplifications ([NAMING.md](../00-authority/NAMING.md)).

---

## Cross-references

- [translation_unit_extraction_plan_template.md](../templates/translation_unit_extraction_plan_template.md) — Legacy Retirement section
- [noteditfocus_translation_unit_extraction_refinement.md](noteditfocus_translation_unit_extraction_refinement.md) — Phase LR candidates
- [loop_translation_unit_extraction_refinement.md](loop_translation_unit_extraction_refinement.md) — optional follow-up LR for shipped aliases
