# Packet fixtures — discriminating assertions demonstrated

Records the G2 field **"fixture implemented and discriminating assertion demonstrated"** for
[P-A](../packets/P-A-CUSTODY.md), [P-B](../packets/P-B-SOURCE.md) and [P-D](../packets/P-D-VISUAL.md).
It does not record an implementation, a GREEN case, a build figure or a timing claim.

Scratch logs stay in session scratch; nothing here is a production behaviour change except the
test-harness defect fixed in §Harness defect.

## Receipt

Both suites are registered PlatformIO suites; their **default** run is the characterisation controls,
so a full `pio test -e native` stays green. The desired cases are opt-in through a selector, which is
why they can remain RED without breaking the regression.

| Packet / case | Command | Exit | Exact message |
|---|---|---:|---|
| P-A `P-A-1` | `IMMEDIATE_HANDOFF_CASE=stop "$bin"` | 1 | `FAIL: B remains live/appends after its requested Stop while A finalizes` |
| P-B `P-B-1` | `IMMEDIATE_HANDOFF_CASE=source "$bin"` | 1 | `FAIL: private predecessor A is absent from B's same-pitch hold source` |
| (P-C prerequisite) | `IMMEDIATE_HANDOFF_CASE=empty "$bin"` | 1 | `FAIL: Expected 768 Was 0. empty A cleanup reset geometry still needed by accepted B` |
| (existing, not P-A) | `IMMEDIATE_HANDOFF_CASE=start "$bin"` | 1 | `FAIL: B ingress waits for private Record publication` |
| P-D `P-D-1` | `IMMEDIATE_VISUAL_CASE=bound pio test -e native -f test_immediate_visual_cache_probe` | 1 | `EXPECTED RED: one-bar budget=1 still selects whole-pass resolveDisplayNotes; bounded producer absent` |

Control runs (must stay green, and did):

```text
pio test -e native -f test_immediate_capture_handoff        PASSED 4 cases
pio test -e native -f test_immediate_visual_cache_probe     PASSED 4 cases
```

## Why each failure is causal, not incidental

The diagnostics the fixtures print name the same state the contract clause names, so a future GREEN is
attributable to the clause it implements rather than to a changed fixture.

| Case | Diagnostic rows (abridged) | Clause it falsifies |
|---|---|---|
| `stop` | `B_stop_requested … producer=2` then post-stop input → `producer=3`, `interval=2`, `raw=64/rawgen=1` for A | C1.3 — Stop must cut input at its command boundary |
| `source` | `source_expected,id=1,start=12,end=96,pitch=60,participants=0` | C3.1 — the private extent is a source but its rows are not admitted |
| `empty` | `empty_A_B_live … length=768` → `A_worker_drain … length=0`, B still live | C4.1 — predecessor-local cleanup |
| `start` | `desired_start_before_publication state=5,interval=1,producer=0,committed=0` | C1.4 — per-track admission gate (not P-A's slice) |

## Harness defect found and fixed

`test/test_immediate_capture_handoff/test_immediate_capture_handoff.cpp` rejected an unknown selector
with `TEST_FAIL_MESSAGE(...)` **outside** any `RUN_TEST`. Unity's `UnityFail` `longjmp`s to
`Unity.AbortFrame`, which only `RUN_TEST`/`TEST_PROTECT` establishes, so the read ran off an
uninitialised frame:

| Selector | Before | After |
|---|---|---|
| `IMMEDIATE_HANDOFF_CASE=bogus` | **segfault, exit 139** | exit **2**, `unknown IMMEDIATE_HANDOFF_CASE=bogus (start\|stop\|source\|empty)` |

This mattered beyond tidiness: a mistyped selector in this tree would have looked like a firmware
crash rather than a rejected run, and the previous evidence's "unknown selector exits 2" claim was
not true for this suite. The fix is test-only, three lines, and mirrors the pattern the visual probe
already used. The four named profiles still exit 1 and the default still exits 0.

## Regression after the harness fix

`pio test -e native` — **3,454 cases / 7 skipped / 3,447 passed / 0 failed**, 147.2 s. Unchanged from
the pre-fix count, so the harness edit did not disturb any suite.

## Operational note

`$bin` is `~/Library/Caches/pio-build-250513-215524-teensy41/native/program` and holds **whichever
suite was built last**. Rebuild the named suite immediately before running a selector, or a selector
silently targets the wrong executable. This bit one run while collecting the table above (the `start`
and `empty` messages were initially empty because the visual probe had overwritten the binary).

## What this does not establish

- No packet implementation, no GREEN case, no acceptance-profile change: all five desired profiles are
  still RED on purpose.
- No shipping build, RAM1 figure, device run or timing claim.
- P-C's fixture inputs are unchanged and unproven; `empty` is listed only as a P-C prerequisite that
  already exists.
