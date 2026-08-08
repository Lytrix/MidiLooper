# Branching and firmware versions

**Default branch:** `dev` (v3 integration). GitHub PRs and clones should target `dev`.

---

## Three firmware lines

| Line | Branch | Hardware / control | PSRAM | Active development? |
|------|--------|-------------------|-------|---------------------|
| **v1** | [`main`](https://github.com/Lytrix/MidiLooper/tree/main) | 16×2 LCD, 2 buttons + encoder | Optional | No — frozen reference |
| **v2** | [`midi-faders`](https://github.com/Lytrix/MidiLooper/tree/midi-faders) | DROID / iPad MIDI, basic SD save | Not required | No — cherry-pick critical fixes only |
| **v3** | [`dev`](https://github.com/Lytrix/MidiLooper/tree/dev) | DROID 8×8 + 4 faders, Sets/revisions, multi-slot | Strongly recommended | **Yes** |

**Release tags:** `v3.x.y` on `dev` when a milestone ships. Do **not** fast-forward `main` — v1 stays on `main` forever.

**Archive tags (GitHub):**

| Tag | Points at |
|-----|-----------|
| `archive/v1-basic-looper` | `main` tip |
| `archive/v2-midi-faders` | `midi-faders` tip |
| `archive/pre-dev-cleanup` | Last `continuous-saving-stable` tip before `dev` cutover |

---

## Feature branch workflow

```text
main          (v1 — frozen)
midi-faders   (v2 — frozen)
dev           (v3 — integration, default)
  └── feature/<scope>   (short-lived → merge back to dev)
```

**Planned sequence (2026-07-14):**

1. `feature/persistence-work-queue` — B1–B5 from [`Plans/current_set_persist_work_item_queue_enhancement.md`](Plans/current_set_persist_work_item_queue_enhancement.md)
2. `feature/loop-owned-undo-phase2` — DEC-024 Phase 2 (after queue merges)
3. `feature/set-revision-loop-picker` — overlay 4.8–4.10 (after queue + undo stable)

**Rules:**

- Merge feature branches to `dev` when native + HITL gates pass.
- Never merge `dev` → `main` or `midi-faders` without an explicit release decision.
- v2 critical fixes: cherry-pick from `dev` onto `midi-faders`, tag `v2.x.y`.
- **Default for tracked Issues (including refinements/Tasks):** short-lived branch + PR into `dev`. A PR always requires a branch ≠ `dev`. Direct commits on `dev` only by explicit exception — see [Authority/GITHUB_WORK_TRACKING.md](Authority/GITHUB_WORK_TRACKING.md) § Branches and pull requests.

---

## Local archive branches (remote removed)

These refs were removed from **origin** during the 2026-07-14 cleanup but **kept locally** for `git log`, `git diff`, and `git cherry-pick`. All are strict ancestors of `dev` (no unique commits vs current tip).

| Local branch | Former role |
|--------------|---------------|
| `continuous-saving` | DEC-020 WIP (superseded) |
| `continuous-saving-stable` | Pre-`dev` integration tip |
| `dec-023-recovery` | DEC-023 recovery stack |
| `load-save-sets-loops` | Set/revision + note-edit stack |
| `derived-note-overlap-logic` | Edit-session-action-geometry baseline |
| `feature/track-multi-looping` | Multi-loop prototype |
| `archive/timeline-data-model` | Timeline refactor (was remote-only) |

**Cherry-pick examples:**

```bash
git log load-save-sets-loops..dev
git diff continuous-saving..dev -- path/to/file
git cherry-pick <sha>
```

Historical handoffs under [`Plans/`](Plans/README.md) may cite these branch names; firmware code lives on `dev`.

---

## Which docs apply to which version

| Docs | v1 (`main`) | v2 (`midi-faders`) | v3 (`dev`) |
|------|-------------|-------------------|------------|
| Root README on each branch | Yes | Yes | Yes (v3 cheat sheet) |
| [`Guides/control-surface/`](Guides/control-surface/) | No | Partial (older DROID) | **Yes** — canonical |
| [`Guides/MIDI_CONFIG_GUIDE.md`](Guides/MIDI_CONFIG_GUIDE.md) | No | Reference | **Yes** — DROID default mapping |
