# Phase 3: Multi-loop slots + jam recording — requirements

**Status (updated Aug 2026):** Partially shipped. The **multi-loop slot infrastructure** (Loop struct, 8 slots per track, per-slot record/overdub/clear/undo, quantized switching, storage v4) is implemented on **`dev`**. **Jam capture (D13–D15) and Scenes are not implemented.** Depends on **Phase 1** (jam state) and **Phase 2** (`jamTick` / `getEffectivePlaybackTick`) — both done. Current overview: [../DELIVERABLE_TRACKING.md](../DELIVERABLE_TRACKING.md).  
**Related:** [dual-tick_view_override_architecture_856310b1.plan.md](dual-tick_view_override_architecture_856310b1.plan.md) (historical architecture).  
**Implementation:** Combined deliverables and refinements → [multi-loop_leds_and_droid_lfo_3a62f325.plan.md](multi-loop_leds_and_droid_lfo_3a62f325.plan.md) §0.

This document specs **what to decide and build** before / during implementation. Items marked **TBD** need product/hardware answers.

### Scope 1 (first implementation pass — locked)

- **Loop slots per track:** **`MAX_LOOPS_PER_TRACK = 8`** — matches the **8 loop buttons** and keeps the test matrix small.
- **Still anticipate larger storage in design** (not in Scope 1 UX): use a **named constant** everywhere (no magic `8`), **versioned save/load** that can carry **many slots per track** and **large per-slot MIDI payloads** as caps grow — see **§2.2** (data layout + SD) and **§10** (slot count + UI). Scope 1 does **not** ship bank/fader slot UI unless you explicitly widen scope.

---

## 1. Goals

- Each **track** can hold up to **N independent loops** (slots), where **N** is a **compile-time (or config) cap** — not one flat `midiEvents` + loop params only. **Scope 1:** **N = 8** (`MAX_LOOPS_PER_TRACK`). See **§10** for raising **N** later and **§2.2** for large per-loop storage.
- **Loop** = stored data (events, length, start, caches). **Jam** = performance mode (existing `jamStartTick` / `jamLength` / `jamTick` / `jamPlaybackActive`).
- User can **record**, **switch**, **overdub**, and **delete** per slot, with UX aligned to today’s track-level REC lifecycle but **per slot**.
- Optional: **scenes** — snapshot of active loop index per track for arrangements.

---

## 2. Data model requirements

### 2.1 `Loop` struct (per slot)

Hold everything that today lives on `Track` for one sequence:

- MIDI event list (or equivalent storage).
- `loopLengthTicks`, `loopStartTick`, `startLoopTick` (or renamed consistently — match current `Track` semantics).
- Note cache / `playbackOrder` / dirty flags if still used per loop.
- Slot **state** for UX: empty | recording | playing | overdubbing | (muted?) — exact enum **TBD**.

### 2.2 `Track` layout

- **`Loop` storage:** fixed array `loops[MAX_LOOPS_PER_TRACK]` *or* `std::vector<Loop>` with **hard cap** `MAX_LOOPS_PER_TRACK` (Teensy RAM — not truly “unlimited”; cap follows memory budget).
- **Large loop storage (anticipate now, Scope 1 may still fit all 8 in RAM):** Do not bake in assumptions that a loop is “small” forever. Prefer **per-slot blobs in a versioned SD/project format** (extend `StorageManager` — see `src/StorageManager.*`) with room for **long event streams**; if RAM becomes tight at higher caps or denser MIDI, **load working set for active slot** and **stream or lazy-load** other slots — **TBD** when profiling says it is needed. Slot count and per-slot size should be **policy** (named limits + schema fields), not implicit fixed struct sizes that block growth.
- `uint32_t` / `uint16_t` `activeLoopIndex` if `MAX_LOOPS_PER_TRACK` can exceed 255 — **TBD** by cap choice.
- Derive “used” slots from metadata or count non-empty loops — **TBD**.
- **Migration:** load old save format → **slot 0 only**, other slots empty. **SD / StorageManager version bump** required; document format.

### 2.3 Jam vs recording clock — **must be specified**

- **Requirement R1:** New MIDI captured while recording is timestamped on **`currentTick`** (global transport), as in the dual-tick plan.
- **Requirement R2:** When **jam playback** is active on a **source** loop, audible output is driven by **`jamTick`** mapping; recording must define whether captured events are:
  - **A)** literal merged MIDI stream at `currentTick`, or
  - **B)** structural references to source events — **pick one** (A is simpler for playback; B is smaller / editable).

### 2.4 Slot 0 = original

- First / imported recording lives in **slot 0**; user-facing “loop 1” = index 0 unless UI is 1-based — **TBD** labeling.

### 2.5 MIDI channel: store on **`Track`** vs **`Loop`**

**Product intent (aligned with this project):** **`Loop`** holds the **MIDI data** (events, loop geometry, caches). **`Track`** holds **routing** such as **output MIDI channel** so you can **remap the whole strip** in one place; all active slots on that track follow unless you add a deliberate override model.

**Existing code today:** [`include/Track.h`](include/Track.h) defines **`uint8_t midiChannel`** on `Track`, and stored MIDI events also carry a **`channel`** field per event — Phase 3 / playback must define **precedence** (e.g. always emit on `Track::midiChannel` vs respect bytes in the loop) — **TBD** and must be consistent with record path.

| | **Channel on `Track` (recommended default)** | **Channel on `Loop` (per slot)** |
|---|-----------------------------------------------|----------------------------------|
| **Pros** | One value to change → **all loops on that track** remap together (matches your reuse/remap goal). Less redundant state in saves. Matches common “track = channel strip” mental model. | Each slot can target a **different channel** without adding another track row. Self-contained **clip** if you export a single loop blob with its own routing. |
| **Cons** | Cannot have two slots on the **same** track outputting two channels without **splitting into two tracks** or adding an override field anyway. | **Remap** requires updating **every** loop or defining **track default + per-loop override** (more rules and UI). Duplicated channel in each saved loop; easy to get **out of sync** with what the user thinks “the track” is. |

**Recommendation for Phase 3:** Keep **authoritative output channel on `Track`** (`midiChannel`); use **`Loop` only for sequence data**. If you ever need per-loop channel, add an **optional override** on `Loop` (nil = use track) rather than moving the only copy onto the loop.

### 2.6 Future UX (not Scope 1): fader over MIDI **kind**

- **Goal:** Step or scrub **MIDI category** the way you already select **notes / loop start** — e.g. cycle **note vs CC value vs pitch bend** (and similar) inside an edit or jam context. Orthogonal to **loop slot count**; depends on editor mode and display.
- **Requirement when implemented:** Document which mode owns the fader map and how it interacts with [`FADER_STATE_SYSTEM.md`](../Guides/FADER_STATE_SYSTEM.md) patterns.

---

## 3. Input / hardware requirements

### 3.1 Loop buttons (direct access)

- **Up to 8 physical buttons** map to **slot index** (or global with **selected track**) when `MAX_LOOPS_PER_TRACK` and UI bank align — **TBD** DROID layout.
- When **`MAX_LOOPS_PER_TRACK > 8`**, direct buttons address **slot = bankBase + buttonIndex** (see §10).
- Must not collide with **bar (17–24)** and **16th (0–15)** on ch 16 without a clear mode layer.
- **Requirement:** Document MIDI note(s)/channel(s) in `droid/midilooper_v1.ini` (or equivalent) when implemented.

### 3.2 Coexistence with existing REC button

- **Requirement:** Define whether track-level REC is **deprecated**, **aliases slot 0**, or **stays** alongside slot buttons — **TBD**.

---

## 4. UX requirements (per slot)

Mirror current track REC lifecycle **per loop button** where possible:

| Situation | Action |
|-----------|--------|
| Empty slot + press | Start recording into that slot (events @ `currentTick`). |
| Recording same slot + press | Stop, finalize (quantization **TBD** — bar boundary per original plan), set **active** to that slot, start looping. |
| Filled slot + short press | Switch **activeLoopIndex** to that slot (playback + display). |
| Filled slot + double/long | Start **overdub** into that slot — **TBD** match existing long-press semantics. |
| Overdubbing + press same | Stop overdub. |
| Triple press (or agreed gesture) | Clear slot → empty. |

**LED feedback:** per-button state (empty / rec / play / overdub / mute) — **TBD** feasibility with current LED outputs.

---

## 5. “Browse another loop while overdubbing” (from dual-tick plan)

When overdubbing **loop A** and user presses **loop B**:

- **Display + jam window** follow **B** (use Phase 1 jam state to show B’s content).
- **Record target** remains **A**.
- **Bar/16th** navigate **B** for performance; **B**’s output at `jamTick` / effective tick is mixed into what gets recorded into **A** at `currentTick`.
- Press **A** again → view returns to **A**.

**Requirement:** Formalize merge rule (see §2.3) and edge cases (B empty, B same as A, rapid switching).

---

## 6. Creating a new loop (three modes)

1. **Fresh record:** `jamPlayback` off or `jamTick` aligned with global feel — standard record into empty slot.
2. **Rearrangement record:** Jam mode, seek/slide bars, capture performance into new slot (timestamps on `currentTick`).
3. **Live switch record:** While recording new slot, switch **source** loop via loop buttons; composite captured into one new loop.

**Requirement:** Each mode has a **test checklist** in `docs/` when implemented.

---

## 7. Scenes / arrangements

- A **scene** stores `activeLoopIndex` (or equivalent) for **each** of `NUM_TRACKS` tracks (see `Config` / global headers).
- **Trigger:** **TBD** (button, CC, program change).
- **Persistence:** **TBD** — in-memory only for v1 vs save with project on SD.

---

## 8. Implementation phases (suggested)

Superseded by [multi-loop_leds_and_droid_lfo_3a62f325.plan.md](multi-loop_leds_and_droid_lfo_3a62f325.plan.md) §0.1 (D1–D15 deliverables). Rough mapping:

- **3a** → D2, D10
- **3b** → D4, D5, D7
- **3c** → D11, D12
- **3d** → D13, D14
- **3e** → D15

---

## 9. Open decisions checklist (before coding)

Tracked as **R1–R8** in [multi-loop_leds_and_droid_lfo_3a62f325.plan.md](multi-loop_leds_and_droid_lfo_3a62f325.plan.md) §0.2. Key items:

- [ ] **Scope 1:** `MAX_LOOPS_PER_TRACK == 8` (locked); document max events / SD blob layout (§2.2)
- [ ] **R2 Playback channel:** `Track::midiChannel` vs per-event `channel` (see §2.5)
- [ ] **R3 Track-level REC:** deprecate, alias slot 0, or keep
- [ ] **R1 Merge model:** §2.3 A vs B (blocks arrangement playback)
- [ ] **R4 Recording stop quantization:** bar vs 16th
- [ ] **R5/R8** Gesture map + slot state enum
- [ ] **R7 Scene** trigger + persistence (if in scope)

---

## 10. Capacity: 8 vs 64+, tracks vs “lanes”, UI scaling

### 10.1 Recommendation for **v1 / Scope 1**

- Set **`MAX_LOOPS_PER_TRACK = 8`** in `Config` (or next to `NUM_TRACKS`), **one named constant**, and avoid magic `8` across the codebase. This is **Scope 1** (see document header).
- **Why:** Matches your **current button budget**, smallest test matrix, and matches the original dual-tick Phase 3 sketch. `NUM_TRACKS` can already follow **1–16** (or more) in global/config without forcing **64 loops per track** on day one.

### 10.2 Anticipate larger **N** and **heavier loops** without building it all in Scope 1

- **Data layer:** Even with a fixed array for RAM predictability, size it with **`MAX_LOOPS_PER_TRACK`** so raising the cap (e.g. 16, 32, 64) is a **single constant + save format review**, not a rewrite.
- **Per-loop payload:** Save format and in-memory boundaries should allow **large event lists** per slot (same theme as §2.2 — versioned blobs, explicit length/count fields, no implicit tiny caps in on-disk layout).
- **“Unlimited”:** On Teensy, loops are **bounded by RAM + SD**; express that as a **documented maximum** in config, not unbounded `vector` growth at runtime without checks.
- **UI when N > direct buttons (e.g. 64 slots, 8 buttons):**
  - **Loop bank / page:** `bankBase = k * 8`, button `i` selects slot `bankBase + i` (same mental model as pad banks).
  - **Fader or encoder** to scrub **active slot index** — **same interaction family** as **loop length / loop start** in LOOP_EDIT (continuous control + display shows index).
  - **Two buttons:** “loop slot − / +” or “page prev/next” — cheap extension once `activeLoopIndex` is not assumed &lt; 8 in logic.

Implement **direct 8** first; add **bank or fader** when you raise `MAX_LOOPS_PER_TRACK`.

### 10.3 `NUM_TRACKS` / MIDI channels vs “CC track + Note track” on **one channel**

- That is **orthogonal** to **loop slots per track**: it is about **how many independent sequencers / lanes** you expose (e.g. two tracks both outputting channel 5 — one CC-only, one note-only — for separate mute/jam/record).
- **Do not** fold that into Phase 3 loop count: treat as **future track topology** (extra `Track` rows, or sub-lanes inside a track) once routing/UI is specified.
- Phase 3 stays: **multiple stored loops per existing `Track` abstraction**.

### 10.4 Summary

| Question | Suggested answer |
|----------|------------------|
| Scope 1 loop count | **8** — **`MAX_LOOPS_PER_TRACK = 8`**, one named constant. |
| Anticipate 64+ slots? | **Yes in design** (named cap, bank/fader UX spec); **defer implementation** until you need it. |
| Anticipate large loop storage? | **Yes in design** — versioned SD blobs, explicit sizes, optional lazy-load later (§2.2). |
| Same channel, CC vs Note? | **Separate feature** from slot count; `NUM_TRACKS` can grow independently. |

---

## 11. References (code)

- `include/Track.h`, `src/Track.cpp` — current single-loop + jam fields; `Track::midiChannel` + per-event `channel` (§2.5)
- `src/BarStepButtonHandler.cpp` — bar/16th jam UX
- `src/TrackManager.cpp`, `src/ClockManager.cpp` — `advanceJamTicks`, `getEffectivePlaybackTick`
- `src/StorageManager.*` — extend for multi-slot blobs
