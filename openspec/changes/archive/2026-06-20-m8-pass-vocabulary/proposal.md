# Proposal — pass vocabulary (replaces span)

**Change:** `m8-pass-vocabulary`  
**Status:** Code rename shipped (2026-06-20); HITL sign-off pending Teensy  
**Supersedes:** M8 use of **span** for overdub-bounded edit batches

## Problem

**span** is overloaded (M8 overdub chapter, overlap tick range, compaction window) and reads
like book/chapter domain, not audio/MIDI time.

## Decision

Adopt **pass** as the shared time-domain word:

| Pass | Meaning |
|------|---------|
| **record pass** | One record capture → **Take** commit |
| **overdub pass** | One overdub capture → **Take** commit |
| **edit pass** | Edits committed between boundary events inside **NoteEditSession** |

**edit pass** replaces **span** in docs and identifiers (`editPassIndex`, `closeNoteEditPass`, …).
See [tasks.md](./tasks.md) for HITL sign-off.

Global undo unchanged in behavior: **NoteEditSessionCommitted** = one undo step per closed
**edit pass** (list of **Edit** ids).

## Scope

- Naming rule + OpenSpec deltas
- Doc updates for active changes (`m8-edit`, `note-edit-modification-session`)
- Phased code/SD rename (`editPassIndex`, `closeNoteEditPass`, …)

## Out of scope

- Renaming **Take** / **Capture** (already locked)
- Using **layer** for edit batches (collides with performance layers + two-stack undo)
