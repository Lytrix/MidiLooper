# NoteEdit — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § NoteEdit in the decision log.

## Owns
- `NoteEditSession`: a NOTE_EDIT session's notes, selection and gather (display cursor and scratch window).
- `NoteEditWriter`: each edit action (an E: step) as one edit run of HideNote rows and replacement notes, rebuilt and
  published in the background; create, leave (one U: step), abandon.

## Invariants
- An edit run is not a derived occluder (EDIT-1): NOTE_EDIT writes its own overlap results as explicit hides and
  replacements; noteIds are kept (lineage, DEC-245).
- The causing note A is never changed; another note of A's pitch inside A is hidden, one sounding into A ends at A's
  start (any channel). When A is deselected the results become the notes' own state (sticky, DEC-030).
- One selection owner: a selection gesture is refused while a NOTE_EDIT session is open.
- The gather shares the display cursor and scratch window with the tile sweep: one at a time.
- A newer action supersedes a running rebuild (its reservation forgiven, its run reclaimed).

## Traps
- Don't leave the note lookup on a note of length 0 (a NoteOff on its NoteOn's tick): it must move on, or entering
  NOTE_EDIT hangs.

## Decisions
- DEC-030 (sticky end of participation) · DEC-245 (lossless committed source) · DEC-279 (copy / paste)
