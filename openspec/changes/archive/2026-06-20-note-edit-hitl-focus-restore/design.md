# Design — note edit HITL focus / restore

**Change:** `note-edit-hitl-focus-restore`  
**Status:** **Shipped**  
**Evidence:** [BUG.md](./BUG.md) sign-off captures `013630`, `013835`, `014601`

---

## B1 — Delete target contract (shipped: D1+D2)

### Problem (serial-proven)

Delete-B HITL sent fader-1 select B, then NOTELEN double. Select ignored after fader-4 pitch; delete used stale `selectedNoteIdx`.

### Decision

- [x] **D1:** Last fader-1 **NoteRef** on successful select; delete uses that ref
- [x] **D2:** Pitchbend guard — allow external select when echo delta > threshold
- [ ] **D3:** NOTELEN delete bypasses length-mode toggle commit — **not needed**

---

## B2 — Overlap scratch lifetime (shipped: D4+D7)

### Problem (serial-proven)

Length commit cleared `focus.overlapNotes`; pitch restore could not find hidden P0.

### Decision

- [x] **D4:** Retain **Hidden** scratch after commit; `preCommitEmitted` skips duplicate **DeleteNote**
- [ ] **D5:** Restore from **Edits[]** replay — **not needed**
- [ ] **D6:** Align length hide with move hide — **not needed**
- [x] **D7:** Pitch lane clear: `keepOverlapTrackingForPitchRestore=false`

---

## Verification contract (met)

| Track | Minimal HITL | Full gate | Result |
|-------|--------------|-----------|--------|
| B1 | AC3 + AC5 | Three consecutive baselines | Pass |
| B2 | `inner_p0_ok` + restore log | Same captures | Pass |
| Verifier | AC1 + AC5 tolerance | Script-only patch | Pass |

Parent `edit.ok` sub-verifiers remain deferred per C16.

---

## Native tests

No new suites. Sign-off: `pio test -e native` 110/110 before firmware upload.
