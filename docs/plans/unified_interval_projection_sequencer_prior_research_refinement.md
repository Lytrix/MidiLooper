# Prior research — sequencer interval projection

**Date:** 2026-07-04  
**OpenSpec:** [`unified-interval-projection`](../../openspec/changes/unified-interval-projection/)  
**Status:** Appendix — supports UIP Phases 1–5; **does not expand scope**

Primary authority: [`unified-interval-projection/design.md`](../../openspec/changes/unified-interval-projection/design.md) (engine + Edit/Display/Playback migration).

---

## Purpose

Third-party **prior research** (not “prior art”) validates the **generate equivalent intervals → select for context** split. MidiLooper implements its own engine; this doc collects **reusable math** and **contrast** only.

---

## Reuse in v1

| Source | UIP stage | What to reuse |
|--------|-----------|---------------|
| [Magda-core looped MIDI split](https://github.com/Conceptual-Machines/magda-core/commit/b8df1e1cbacf337c432ef5bc4c698cd66a233a34) | Generate + Edit select | `phase = fmod(x, loopLen)`; bounded k shifts |
| [JUCE MIDI loop forum](https://forum.juce.com/t/how-to-loop-midi-file/33837) | Playback | `fmod(currentTime, loopLength)` |
| [cp3.io sample-accurate loop](https://cp3.io/posts/sample-accurate-midi-timing/) | Playback | Buffer window intersection; schedule wrap-around events |
| [Tracktion LoopingMidiNode](https://tracktion.github.io/tracktion_engine/classtracktion_1_1engine_1_1LoopingMidiNode.html) | Context builders | `editRange`, `loopRange`, `offset` shape |
| [mmckegg/midi-looper](https://github.com/mmckegg/midi-looper) | Generate | `position % length` at transform layer |

**Not UIP:** [Ardour overlap prefs](https://manual.ardour.org/working-with-midi/handle-overlapping-notes/), [Helio Alt+O](https://docs.helio.fm/refactoring.html) — overlap OpenSpec only.

---

## Contrast (do not adopt as authority)

| Pattern | Why not primary for MidiLooper |
|---------|-------------------------------|
| **Ableton clip-local time** ([Clip View manual](https://www.ableton.com/en/manual/clip-view/)) | MidiLooper uses **global `currentTick`** + **`loopStartTick`** (design D12) |
| **Ardour region membership** ([Mantis #7421](https://tracker.ardour.org/view.php?id=7421)) | UIP selects by **interval intersection** with playhead/window |

---

## Test fixtures (Phase 1)

| Fixture | Source |
|---------|--------|
| `900→1080`, `L=960` → `-60→120`, `900→1080`, `1860→2040` | Magda / design D2 |
| Playback buffer crosses loop end mid-block | cp3.io |
| Note on exactly at loop 0 after linear off at loop−1 | Ardour regression guard |
| Display/edit parity | In-repo `test_noteutils_reconstruct`, `test_note_edit_focus` |

---

## Global time hook (D12 — document only in v1)

See design **D12**. Playback **`originTick`** from `getEffectivePlaybackTick`; **`jamTick` retrigger** is future (Phase 3 jam) — context field only, no v1 implementation.

References: [`dual-tick_view_override_architecture_856310b1.plan.md`](dual-tick_view_override_architecture_856310b1.plan.md), `Track::getEffectivePlaybackTick`.
