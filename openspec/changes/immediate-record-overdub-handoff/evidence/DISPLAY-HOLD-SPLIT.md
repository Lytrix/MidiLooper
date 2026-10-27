# Display hold split — live viewport, retained content

## Symptom and attribution
Owner-reported after P-D: multiple overdubs hold, but the piano-roll cursor stops following the
window. Capture `captures/session_20261026_221420.log`, 1,015 `DIAG,disp_turn` rows:
`st=fresh` 859 (`why=-`), `st=ret` 156 (155 `why=hold_gap`, 1 `handoff_seed`), and
**154 turns retained while the requested interval had already moved**.

The visual slice was excluded first: a native moving-viewport case over 33 bars/17 passes/1,584 rows
measured `turns=801 requested_advances=801 published_turns=1 retained_turns=800 rekeys=0`. The bounded
continuation key contains no viewport, so a moved request cannot supersede it — the deferral is plain
completion, not churn. The freeze therefore lived in the display hold.

## Change
`syncDetailedPaintWindow` publishes the **viewport** fact from the requested interval every turn, while
the hold continues to substitute the retained `liveDrawSourceKey_` interval for **content** only.
`drawPianoRoll` maps the roll (grid, note bars, bracket, playhead) in the live viewport. Content
selection, the content key and every hold guard are unchanged: retained rows are never re-gathered,
mixed, or claimed as current.

Contract amendment (owner-approved as part of this fix, no guard removed): the existing control
`test_moving_window_gap_never_mixes_old_prefix_with_new_viewport_overlay` asserted the old policy that a
held frame must also keep its viewport. Its content clauses are kept verbatim (content key frozen at
`8*bar-16`, 2 paint notes, `b`/`z` exact, live overlay `c` excluded); only the viewport clause flipped,
with three added asserts (drawn viewport == requested, start 0, length 16 bars).

## Oracle
`test_hold_keeps_retained_content_but_draws_the_requested_viewport` (registered under
`IMMEDIATE_VISUAL_CASE=bound`; body lives in the probe because it owns the real paint fixture):

```
HOLD_SPLIT turns=3 view_starts=0,2000,4000 req_start=4000 retained_key=6128,12288 drawn_rows=1,2,3 content_notes=4
```

(a) viewport == request and strictly advancing; (b) content 4 notes, exact and unchanged; (c) content key
frozen at 6128/12288 on all three turns; (d) drawn rows are the retained rows inside the live viewport
(1,2,3 vs 4,4,4 in retained coordinates). A note inside the viewport but outside the retained interval is
correctly absent — the hold's honest hole, asserted by row count.

Red-proof: with the draw site reverted to the retained interval (`M-SPLIT1`), both this case and the
amended display control fail with "drawn rows must be the retained rows inside the requested viewport";
`PianoRollDraw.cpp` restored byte-identically (`9b887eec…`).

## Verification (parent-verified)
- Visual probe controls pass; `IMMEDIATE_VISUAL_CASE=bound` **6/6**; display suite 22/22.
- Handoff default 20/20; `source` 10/10, `stop` 4/4, `empty` 3/3.
- Full native **3,491 total / 7 skipped / 3,484 passed / 0 failed**.
- Build SUCCESS; RAM1 unchanged from baseline (code 389,864 + padding 3,352 / locals 34,880; floor 840).

## Not closed
- The split is verified natively; the on-glass behaviour needs one more capture with `disp_turn`
  (`req==pub`, `st=ret`, `why=hold_gap`, no diverged-request rows).
- Under a hold, bars the retained frame never qualified render empty rather than stale — bounded by the
  hold's lifetime. Whether that is preferable to a frozen cursor in every fixture is a follow-up decision.
- No upload/SD/commit in the implementing slice.
