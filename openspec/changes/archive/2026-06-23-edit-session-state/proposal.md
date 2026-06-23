## Why

Live edit RAM is split across misnamed **`NoteEditSession`**, **`MainEditMode`**, and parallel
state owners. Stored **editPass** rows use **`EditSessionType`** today — the name fits the live
session layer better than the pass row layer.

Rename for two clear layers:

| Layer | Enum | Values |
|-------|------|--------|
| Live **`EditSession`** | **`EditSessionType`** | `Loop`, `Note`, `ControlChange` |
| Stored **`EditPass`** row | **`EditPassType`** | `Note`, `ControlChange`, `Audio` |

Same domain words (`Note`, `ControlChange`) at both layers; **`Loop`** only on live session (no
**editPass** row). **`Audio`** only on pass until an audio edit session ships.

## What Changes

- **`EditSession`** on **EditManager** — single owner: **`sessionType`**, **`store`**, undo, pass
  batch, domain branches.
- Rename pass-row **`EditSessionType`** → **`EditPassType`**; field **`sessionType`** → **`passType`**
  on **EditPass**.
- Remove **`MainEditMode`**, **`NoteEditSession`**, duplicate scope mirrors.
- **`cycleEditSession`** toggles **`EditSession::sessionType`** (`Loop` ↔ `Note` for now).

## Sequencing

- Land before **scoped-edit-pass-persistence** exit-path matrix.
- Coordinate with **scoped-edit-pass-payload** (touches **EditPass** layout).
