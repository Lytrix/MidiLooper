# G2 execution packet — 3.12 / 5.5: the emission selection's **named refusal** at the declared closure capacity

Slice of task **3.12** (admission limit and the bounded legacy compatibility path) carrying the repair of the clause that
fixture **5.5**'s own measurement **falsified** on 2026-10-06, with `tasks.md` § 2.11's restored-over-`L` dimension.
Packet written **before** the edits, per the
[G2 execution packet](../../../../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
requirement; the previous increment (`evidence/2026-10-06-s312e-paged-serving-envelope/`) pinned the loss.

## 1. What the falsification measured, and what the candidates were

The 3.12e drive (production per-track clock entry, retained-source (restored-library) shape, an interval of **2,100 events
of note pairs** inside the two-bar span, one deferred-lane turn and one clock frame per 64-tick frame, two whole passes)
consumed **4,200 ordinals** (the interval twice, exactly) and emitted **3,830**: 370 events (8.8 %) were *consumed and then
skipped as behind their frame*, in 20 short frames, so 5.5's "complete, ordered, exactly-once output with no tick delay"
did **not** hold. The record named two candidate repairs and deliberately did **not** attribute the mechanism: *a producer
turn that publishes more than one page while the ring has room*, or *a named refusal of the emission selection at densities
the declared service cannot carry* (the I3c-b lesson that emitting wrong content is worse than not emitting from pages).

## 2. The mechanism, measured (2026-10-06, this slice's first act)

The measurement in this slice **settles the candidates**. Instrumented over the same drive (per-frame ledger occupancy,
ring published ordinal, consumed ordinal, per-frame due set from the interval's own resolver):

| figure | value |
|---|---|
| frames whose consumed-ordinal delta ≠ their due set | **0** (every frame consumed exactly its own due set) |
| frames whose emissions fell short of that due set | **20**, 370 events |
| the type of every missing row | **`NoteOff`** (150 sampled mismatches, *all* `type=128`; no `NoteOn` was ever missing) |
| the matching `NoteOn` of every missing `NoteOff` | **already sent** (`onSent=1`), at its own tick, in an earlier frame |
| the active-note ledger's open-entry count while this happens | pinned at **128 = `ActiveNoteLedger::kMaxOpenNotes`**, `overflowed == 1`, from frame 3 of pass 1 onward |

So the walk is **not** the loser and the producer's page rate is **not** the constraint (consistent with 3.12e's withdrawn
probe: doubling the lane's turns changed nothing):

* the interval's own content holds **≈ 157 ledger identities open at once** (1,050 note-ons over a 1,336-tick on-span with
  a **200-tick** note span: `0.786 × 200`), which **exceeds the emission service's declared closure capacity** of
  **128** — the same declared capacity the capture/stop admission enforces as
  `RuntimePlaybackViewConfig::kMaxCaptureNoteReservations` and the window admission counts inside `L` as
  `kWindowClosureReserve`;
* the ledger therefore **stops recording** note-ons (its documented, never-evict policy: table-full is a refuse with
  `overflowed_`, the On is still emitted) and every later tagged `NoteOff` for an unrecorded identity is an **orphan**,
  which `applyPlaybackEvent` refuses (`false`, no mutation, no emission);
* the result is exactly what the falsification saw: **a note-on emitted without a closable note-off** — a stuck note —
  i.e. *wrong content*, not a late delivery and not a refusal by name.

**Candidate A is therefore falsified as a repair:** no producer turn, however many pages it publishes, can change a
per-frame ledger occupancy that is fixed by the interval's own musical concurrency. **Candidate B is the repair**, and
this slice is it: the emission selection must refuse **by name** at a density the declared service cannot carry, instead of
half-serving it.

## 3. Scope

**In scope** (one refusal, one place, plus its fixture):

1. `ActiveNoteLedger::kMaxOpenNotes` becomes the **criterion** the paged delivery's establishment consults: the interval's
   own **closure high-water** (the running note-on/note-off balance of its content, in the delivery's own traversal order)
   is compared with the declared capacity.
2. `Track::beginPagedPlaybackForSlot` computes that high-water over the content the retained gather has **already**
   traversed for this interval — no new pass, and **sound in both routes**: the gather-completing route (content over `L`,
   which is 3.12/5.5's own shape) is measured **exactly**; the slice-ceiling route measures a **prefix**, whose high-water
   can only be ≤ the whole's, so a refusal there is never an over-refusal. On a refusal the ring is **never established**
   (no page produced, `pagedActive` stays false) and the demand stays **owed**.
3. The refusal is **named** in three places a reader needs it: `PagedPlaybackOutcome::ClosuresExceedDeclaredCapacity` (the
   lane's own conclusion), `PlaybackViewPendingReason::ClosuresExceedDeclaredCapacity` (why the demand is still owed) and
   `Track::PagedEmissionDeliveryState::ClosuresExceedDeclaredCapacity` (the selection gate's answer, by name).
   `committedPlaybackRetryCanProgress` stops re-attempting it exactly as it does for `WindowExceedsRetainedCapacity`, so a
   refused demand is named rather than re-gathered every turn.
4. Fixture: the 3.12e drive, split into its two halves —
   * the **reproducer** (unchanged content: 1,050 pairs, 200-tick span) now asserts the **refusal** with its own teeth:
     no delivery, the named state, **zero rows** at the MIDI edge, **zero ring pages produced**, and — the decisive fact —
     **`ledger.overflowed() == false`**: the harm is provably absent because the ring never served;
   * the **acceptance** half (same shape, **carried** density: 1,100 pairs = 2,200 events over `L` with a **100-tick**
     note span ⇒ a high-water of ≈ 77 ≤ 128 and a pair still > one page apart in the event order) asserts 5.5's clause
     strictly: **per-frame due sets equal, in order, nothing skipped, nothing short**, over two whole passes.
5. The clause itself is restated where it is owned: `tasks.md` § 3.12/§ 5.5, and the spec's *Legacy dense content is served
   through bounded pages* requirement gains the declared-capacity bound and the **named refusal** (the content is still
   never refused at admission and never truncated; what is refused is the **emission selection** for it).

**Out of scope, named:** raising the declared closure capacity (`kMaxOpenNotes`; DEC-108 §14 item 12(c) leaves its size to
measurement, and the sweeps that could move it are **6.3**); the *observed* backstop for content whose note-offs do not
close its own note-ons (the balance model counts every Off as closing, so an orphan-closing content can still overflow the
ledger mid-pass — a named residual, not faked here); content whose pairs cross the interval's own boundary; the multi-list
(overdub-pass) traversal order; the device leg (6.4/6.5); any `ExecutionBounds.md` row reclassification; and the fallback
removal (3.7b — see § 5).

## 4. Transition oracle (pinned before the edits)

| # | Transition | Oracle |
|---|---|---|
| O1 | The over-`L` demand whose content exceeds the declared closure capacity | the switch to pages is **refused by name**: `pagedActive == false`, `pagedPagesProduced == 0`, `pagedOutcome == ClosuresExceedDeclaredCapacity`, the selection gate answers `ClosuresExceedDeclaredCapacity`, the pending reason is `ClosuresExceedDeclaredCapacity`, and the demand is still owed (never dropped) |
| O2 | The same demand's **harm** | the walk over two whole passes emits **zero** rows (no window, no root, no merged fallback, no pages) and `ledger.overflowed() == false` — the stuck-note class cannot arise, because nothing was served |
| O3 | The over-`L` demand whose content **fits** the declared closure capacity | the switch happens as before and the drive is **per-frame exact**: for every frame, the rows the MIDI edge received == the interval's due set for that frame, in order; `skipped == 0`, `shortFrames == 0`, `!ledger.overflowed()` |
| O4 | The criterion's own boundary | the high-water is compared against `ActiveNoteLedger::kMaxOpenNotes` and nothing else: a content at the capacity is **served**, one over it is **refused** (the fixture's two shapes sit either side of the declared value by construction) |
| O5 | The refusal's stability | the refused demand is not re-gathered every turn (`committedPlaybackRetryCanProgress` is false for its reason), and a re-demand for the same interval (pages already held) still answers `true` on the existing idempotent path |
| O6 | Nothing else moves | the ring's storage behaviour, the emission adapter, the boundary straddle judgement, the ledger's own contract and every other lane are unchanged; the criteria that held in 3.12e (no early emission, order, storage bound ≤ 3 pages, no inline rebuild, wraps straddled) still hold for the served shape |

## 5. Fixtures and red-proofs

* Fixtures: `test_3_12_a_restored_over_l_interval_beyond_the_declared_closure_capacity_is_refused_by_name` (the
  reproducer) and `test_3_12_the_paged_path_serves_a_restored_over_l_interval_at_each_due_tick` (the acceptance half,
  which keeps 3.12e's name and oracle), sharing the content builder
  `prepareDensePairedInterval(notes, loopBars, noteSpanTicks)`.
* Red-proofs (each restored byte-identically, sha256-checked, anchor occurrence asserted):
  * **RP-D** the closure preflight removed from `beginPagedPlaybackForSlot` → the reproducer's fixture must fail (the ring
    is established, the drive emits, and `ledger.overflowed()` is true — the measured 3.12e loss returns);
  * **RP-E** the comparison inverted (`>` → `>=`) → the carried-density fixture must fail (a content *at* the declared
    capacity would be refused, which O4 says is wrong) **or**, if the two shapes do not sit adjacently enough to catch it,
    the mutation is recorded as a **non-red-proof with its mechanism named** rather than claimed.
* **Consequence named, not smoothed over:** for this content the refusal means the slot is **not served from pages** and has
  no other lane (rootless demoted, empty merged store), so it is silent **by decision** — the I3c-b lesson applied. That is
  the honest trade against emitting note-ons the service cannot close, and it is the reason 3.7b (fallback removal) cannot
  be closed for such content without raising the declared capacity or giving the closure a different owner.

## 6. Exit criteria

1. focused `test_runtime_working_set_ownership` green (both halves); ring suite `test_window_paged_legacy` green;
2. full `pio test -e native` green, no suite ERRORED/FAILED;
3. `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 guard passing (the refusal lives in the runtime's own
   external state and in flash: no RAM1 static, no ITCM growth claimed without the build's own line);
4. every red-proof measured, with byte-identical restores;
5. `openspec validate windowed-playback-generation --strict` and `git diff --check` clean;
6. `tasks.md` § 3.12/§ 5.5 restated with what is now repaired and what stays owed, the spec's requirement amended, and
   `CURRENT_WORK.md`/`PROJECT_STATE.md` + the evidence record written.
## 7. Completion record (filled after the run, 2026-10-06)

* **The mechanism was measured before the repair**, and it settles the candidates: the walk consumed its due set every
  frame (candidate A cannot be the mechanism) and the active-note ledger sat pinned at its declared 128 with
  `overflowed == 1`, so every missing row was a `NoteOff` whose own `NoteOn` had already been sent. Evidence § 1.
* **The criterion gained one term during implementation, forced by the existing suite:** the peak alone over-refuses
  note-on-only content (which loses nothing), so the criterion is `owesClosures && peak > kMaxOpenNotes`. Dropping the
  term is red-proved (**RP-E**: ten note-on-only fixtures fail); disabling the preflight is red-proved (**RP-D**: the
  refusal fixture fails). § 3's item 1 is refined accordingly.
* **The carried fixture's span is 132, not 100**, for a reason outside this slice: the capture store's hot-stop
  minimum-pair removal (48 ticks, newest-open-on pairing) mis-pairs a dense same-pitch schedule at short spans and drops
  both events of each mis-paired pair (measured: 1,100 pairs at span 100 leave an effective **1,364** events, and the
  demand then became a *window*). A model of that pairing reproduced the figure exactly and located span 132 (2,120 events
  > `L`, high-water 109, a pair still 176 positions apart).
* **Verified:** focused 164/164 (baseline 163), ring 3/3, full native 2,842 / 7 skipped / 2,835 succeeded / 0 failed
  (baseline 2,841/2,834), capture build SUCCESS with the RAM1 line byte-identical (`variables 96,192`;
  `code 392,248 + padding 968`), `git diff --check` and `openspec validate --strict` clean. Both red-proofs restored
  byte-identically (`sha256=2150eedd273c1a93edb239d426338a795f8307dd2f2655ba11d3bcc2104bfede`).
* **Not claimed** (unchanged from § 3's out-of-scope list, plus what the run added): the balance is not a ledger
  (orphan-off content that mis-pairs while staying inside the capacity is not detected here — the service's own refusal
  report is the backstop owed); over-refusal of unpaired-excess (wrap-crossing) content is deliberate and named; channel
  remapping is not modelled; the refused interval is not served at all on this shape (silence by decision, and the reason
  3.7b stays open for it); no duration, no device leg, no row reclassification, no fallback removal; `kMaxOpenNotes`
  itself unchanged.
