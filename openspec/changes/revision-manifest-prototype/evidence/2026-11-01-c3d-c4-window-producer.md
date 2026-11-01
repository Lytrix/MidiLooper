# C3d + C4 — the shadow window producer, and per-window parity with the passes view on device

Date: 2026-11-01. Native: `test_revision_manifest_store` (40), `test_realtime_idle_owner_admission` (17). Device:
probe/shadow image, final HEX `b63af14487ecdb16…`, forced fresh boot verified in
`captures/session_20261101_193210.log` (`boot_reset,crash=0`, `BOOT,load,ok`, no crash rows). RAM1: the probe and
default images are both at 96,192 variables; the default image is unchanged with all three registry rows.
Raw rows: [`2026-11-01-c3d-chain-status-raw.txt`](2026-11-01-c3d-chain-status-raw.txt),
[`2026-11-01-c4-chain-windows-raw.txt`](2026-11-01-c4-chain-windows-raw.txt).

## Owner rules applied (DEC-246 amendments 9–13)

- **The oracle** is the authoritative committed-pass effective view (production's passes gather) with `revision()`.
  The snapshot and full-rebuild paths are pinned legacy representations.
- **C4 evidence counts only with a passing stimulus gate.** This run used the owner chain, whose gates pass on this
  image: record 1, overdubs 12, cap 13, slot cleared.
- **Revalidation is metadata-only:** the `ImportSourceKey` contract is documented in code. It never reads a row or
  hashes content.
- **One source identity → at most one successful publication:** counted as `duplicate_publishes`, which was 0.
- **C3c** (restored slots) will go through the same importer source abstraction; it is deferred until after C4.

## C3d — the window producer

- **Store API:** `beginWindow(manifest, expectedIdentity, expectedRevision, from, to, out, capacity)` and
  `stepWindow(maxRows)`.
  - It answers `Ready` (the stamp carries the identity, revision and interval actually read), `IndexOwed`,
    `ImportOwed`, `CapacityDenied` or `SourceMoved`.
  - It never builds an index or an import, and never scans.
  - It pins its manifest, so it finishes honestly as the identity it started on.
  - Native tests: `Ready` equals the `Scan` oracle and the vector oracle over every 2-bar window, in 7-row steps; each
    named answer; a window started on M41 still finishes as M41 (and with M41's rows) after M42 commits.
- **Firmware:** a third registered child, `RevisionShadowWindowGrain` (`revision_shadow_window`, 2 ms / 0.5 ms;
  128 rows per step).
  - It produces consecutive 2-bar windows of the current shadow manifest into its own 2,048-row buffer.
  - It digests each window incrementally (count, sum and XOR of per-row mixes over tick, status and data:
    order-independent and allocation-free).
  - Digests go into a 64-entry PSRAM table, labelled with the identity and revision actually read.
  - **Each window is produced once per manifest identity;** the producer is idle until the identity moves.
- **C4 comparator:** stopped-only `!REVSHADOW_WINDOWS` recomputes production's passes-derived view
  (`copyEffectiveCommittedEventsInRange`) for each stored interval and compares digests. Windows from an older
  identity are counted as stale, not compared.
- **Budget correction:** the earlier `shadow_bytes` (479,940) covered only the store and job, and the shadow actually
  exceeded the 0.5 MiB cap with its diagnostic buffer. Now: pool 320 pages, status window 4,096 rows, producer 2,048
  rows. **Total 490,220 B**, printed as `shadow_total_bytes`.

## Device — owner chain (20-bar Record + 12 overdubs + internal-clock hold)

| Reading | Run A (before the churn fix) | **Run B (final image)** |
|---|---|---|
| Stimulus gate (record / overdubs / cap / cleared) | 1 / 12 / 13 / yes | **1 / 12 / 13 / yes** |
| Shadow publishes / duplicates / `SourceMoved` / refusals | 14 / 0 / 0 / 2 | **13 / 0 / 0 / 1** |
| Import grain max (64 rows) | 366 µs | **256 µs** |
| Window grain calls / `Ready` / max | 11,862 / 5,881 / 927 µs | **261 / 130 / 243 µs** |
| **C4: windows compared / matched** (identity, source current) | 10 / 10 (14, yes) | **10 / 10 (13, yes)** |
| Whole-loop shadow `revision()` vs passes view | 1,052 / 1,052 | **1,042 / 1,042** |
| Production snapshot | not current, `pending_publish=1` | not current, `pending_publish=1` |

## Findings

1. **C4 passes on the device:** every 2-bar window the shadow produced, labelled with the identity it read, equals
   production's passes-derived effective view of that interval. All three grains stay far inside 2 ms.
2. **Churn found and fixed:** run A's producer kept rebuilding the same identity's windows (5,881 `Ready` for 14
   identities). With "each window once per identity", run B's 130 `Ready` is exactly 13 identities × 10 windows.
   Every shadow publication in both runs was for a distinct source identity (0 duplicates).
3. **Production's snapshot stayed un-current** after the chain in both runs (`pending_publish=1`); the passes
   oracle makes Stage C independent of it.

## Not covered (named)

- **C4 with hides:** a gate-valid hide-carrying stimulus is still owed. The occupy-paint preset's own verdict fails on
  this image (C3b). Its parity (54/54) is recorded, but it does not count as C4 evidence under amendment 10.
- **Exact order:** the window comparison is per-interval multiset equality. Production's passes order is undefined
  within an equal-tick class (`std::sort`, B.2a), so exact order is checked host-side against DEC-244 (B.2b, B.3,
  C1).
- **C3c restored slots;** C5 (authority switch) is not authorized by this evidence alone.
