# P6.3 — `.clang-format` dry-run evidence (task 7.3)

**Verdict: ship `.editorconfig` only. No `.clang-format` is added.** Design § D9 accepts a formatter
config only if `clang-format --dry-run` over the added files reports no drift beyond the copyright
header (7.1) and trailing whitespace (7.2) already fixed by this phase. No candidate derived from the
incumbent style comes close, and a config that cannot reproduce the style would land a
four-figure-line reformat inside a "no behavior change" refactor.

## Command

`clang-format` is only reachable through the Xcode shim on this host:

```
$ xcrun clang-format --version
Apple clang-format version 17.0.0 (clang-1700.6.4.2)
```

Each candidate was run over the 61 files the stacked branch adds (the 56 runtime files named in
`proposal.md` § Why plus `include/Utils/PlaybackEventOrder.h` and the four files P5 split out):

```
for f in <61 files>; do
  xcrun clang-format --style=file:<candidate> "$f" | diff -u "$f" -
done
```

## Candidates and result

| Candidate | Style | ColumnLimit | Files changed | Changed lines |
|---|---|---|---|---|
| A | `BasedOnStyle: Google`, 2-space, pointers left, `SortIncludes: Never`, `ReflowComments: false` | 100 | 51 / 61 | 4,870 |
| B | As A, but no column limit (the incumbent code has 84 code lines over 120 columns and a 261-column maximum, so a limit is not what the style enforces) | 0 | 45 / 61 | 2,274 |
| C | `BasedOnStyle: LLVM` with the same overrides | 100 | 59 / 61 | 6,989 |

The best candidate (B) still rewrites 2,274 lines across 45 of 61 files. The drift is not whitespace:
it is the incumbent's deliberate line packing, which clang-format splits unconditionally.

## Representative drift (candidate B, `src/Track/RuntimePlaybackView.cpp`)

```diff
@@ -205,7 +205,8 @@
   ++storageGeneration;
   rawCoveragePrepared = false;
   firstRecordReturnWindow = false;
-  passOriginsReady = canonicalRanksReady = false; passOriginSource = {};
+  passOriginsReady = canonicalRanksReady = false;
+  passOriginSource = {};
   storage.clear();
@@ -281,8 +282,10 @@
-  releaseExternalMemoryFirst(insertionRanks); releaseExternalMemoryFirst(baseRanks);
-  releaseExternalMemoryFirst(rankedBaseOrder); releaseExternalMemoryFirst(baseOrderScratch);
+  releaseExternalMemoryFirst(insertionRanks);
+  releaseExternalMemoryFirst(baseRanks);
+  releaseExternalMemoryFirst(rankedBaseOrder);
+  releaseExternalMemoryFirst(baseOrderScratch);
@@ -308,9 +311,11 @@
-  canonicalRankCount = 0; canonicalMasksReady = false;
-  affectedPitches.fill(0); affectedIdentities.fill(0);
-  heldClosureCount = 0; // Retire metadata without clearing the fixed payload array.
+  canonicalRankCount = 0;
+  canonicalMasksReady = false;
+  affectedPitches.fill(0);
+  affectedIdentities.fill(0);
+  heldClosureCount = 0;  // Retire metadata without clearing the fixed payload array.
@@ -343,14 +348,17 @@
     return (entry.note < 128 && (affectedPitches[entry.note / 32] & (uint32_t(1) << (entry.note % 32)))) ||
-        (entry.noteId != kInvalidNoteId && (affectedIdentities[key / 32] & (uint32_t(1) << (key % 32))));
+           (entry.noteId != kInvalidNoteId && (affectedIdentities[key / 32] & (uint32_t(1) << (key % 32))));
@@ -425,14 +433,19 @@
-    if (index == ledgerBoundary.count) { same = false; return; }
+    if (index == ledgerBoundary.count) {
+      same = false;
+      return;
+    }
```

## What ships instead

`.editorconfig` at the repo root, which fixes the part of the style that is mechanical and
non-controversial — UTF-8, LF, final newline, no trailing blanks, 2-space C/C++ indent, 4-space
Python indent — and deliberately leaves `*.md` trailing whitespace alone because 120 of the branch's
added Markdown files use two trailing spaces as intentional hard line breaks.

`clang-format` therefore stays an authoring aid, not a repo gate. If the project later wants an
enforced formatter, the acceptance path is a dedicated reformat change with its own review, not this
refactor.
