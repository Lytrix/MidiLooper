# Ordering work reduction — execution packet

Scope: task 3.10 service qualification, DEC-108 window publication/source retention
items 2/3/13. Existing RuntimePlaybackOrderBuilder owns the private order and merge
scratch; no ownership transfer, event buffer, scheduler, capacity or timing change.
Historical review: DEC-108 requires one declared bounded grain and immutable
published storage; merge scratch is already privately reserved by every caller.
Snapshot canonical/event-ID and playback canonical-rank comparators stay unchanged.

Before edits: each level writes N scratch entries then copies N back. At N2048,
11 levels plus fill =47104 entries,982 turns at quantum48, per sort. New design
alternates existing arrays as merge input/output, normalizing to caller order once
only if the final level ends in scratch. Bound N*(1+ceil(log2N)+(ceil(log2N)%2));
N2048 ->26624 entries,555 turns. No visible pointer swap or borrowed-source writes.
One extra boolean uses existing class padding if ABI allows; verify target pins.

Fixed oracle: independent stable comparator parity for reverse/random/tied inputs,
odd/even levels, retained subsets and quanta1/7/48. Final output always in supplied
order; borrowed source arrays unchanged; completion not visible early. Reverse
full inputs require exactly the predicted entry count under quantum1; zero and
already sorted keep fill-only behavior. Cancel/discard resets array parity.
Mutation: disable array-direction toggle or final normalization, check parity
assertions fail; restore byte-identically. Focused prewarm suite, full native,
capture/RAM1 build, strict OpenSpec and whitespace checks. Parent owns these files;
launch work runs independently on Track/runtime/view-set, no concurrent path edits.

Qualification limit: this saves854 selected turns over two worst-case dense sorts,
but copying alone needs32 vs3 available per holder/window at H8/gap64. Do not
replace the declaration with observed maxima or call a launch fixture sustained
qualification. Device timing and continuous refill remain separate open claims.

Completion: pending verification.

## Verified result
Prewarm196/196 includes odd/even levels, deterministic mixed keys, filtered/borrowed
sources, quanta1/7/48, discard/restart and measured reverse2048 sorting. Both array
direction and final normalization mutations fail permutation assertions and restore
byte-identically. Dense snapshot fixture measures2,178 source units,35 fill turns,
1,153 order/rank turns (555+43+555),1 shared retirement turn; independently sorted
canonical/tick output matches. Derived lifecycle including count/freeze/admit is
1,226 selected turns vs2,080 before; worst-topology upper bound falls2,624->1,770.

Rebased expectations, not limits: capture order3 entries11->9 grains; prepared ranked
work6,240->4,960 (provisional ceiling stays6,240); unprepared work still exceeds
prepared work, but no longer exceeds that ceiling. Playback order40/80/160/320 rows
11/25/57/127->6/15/30/74 turns. The rotation batch now takes35 turns and fits its
56-turn placeholder; dense negative timing remains separately pinned. The H8 refusal
fixture moves12->7 bars to keep across-track contention deciding; same63-row work
20->12 grains. No grain quantum, capacity, denied-turn term, gap or margin changed.

Full native2,768 passed/7 skipped and final capture/RAM1 pass (see launch manifest).
At H8/gap64, the lane supplies only3 selected turns/window/holder. Even32 pure
copy turns cannot fit; the measured dense pipeline is much larger. Thus continuous
two-bar qualification remains NOT QUALIFIED. Initial prefill cannot repair the
producer-rate deficit. Next design evidence: bounded work/grain duration and a
justified service guarantee; combined count/fill can reduce work but does not by
itself resolve this lower bound. No observed maximum substituted for a guarantee.
