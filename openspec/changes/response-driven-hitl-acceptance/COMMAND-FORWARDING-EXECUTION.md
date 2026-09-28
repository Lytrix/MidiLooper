# Response-driven HITL task 2.3 — allowlisted command forwarding

Status: host-verified and device-confirmed at the transport boundary, 2026-09-28.
Baseline `bed90c04` plus the preserved unrelated dirty tree.
Authority: design decisions 2, 5 and 7; DEC-092 — response-driven HITL;
[runtime shared-finalization contract](../runtime-working-set-ownership/SHARED-CAPTURE-FINALIZATION.md).

## G0 discovery / existing-owner decision

VERIFIED from the current code:

- `capture_session.py` is the single serial owner and already writes two firmware
  commands itself (`!TRACE_FENCE <run_token>` at boot settle and `!SNAPSHOT_YIELD` when
  armed). It had **no** external command channel, so a second writer would have had to open
  the port — which is exactly what the change forbids.
- `scripts/hitl/serial/protocol.py` is the existing single decode owner (`#CAP` micros and
  track transitions). Framing and response decoding belong there, not in the driver.
- The firmware accepts `!TRACE_FENCE <token>` **only when the token is exactly 32
  characters** and the transport is stopped (`StorageManagerHitlSerial`), and the
  on-demand observation family is self-delimiting as `LIBOBS,B` … `LIBOBS,E`.

G0 PASS: one forwarding channel on the existing stdin of the existing owner, one framing and
decode seam in the existing protocol module, no second serial reader and no new owner.

## G1 contract / transition oracle fixed before the edit

- `capture_session.py` gains `--command-stdin` and forwards a request only when it is
  `<start> <end> <!COMMAND>` with two distinct 32-char lowercase-hex tokens and the command
  is on `ALLOWED_HITL_COMMANDS`; anything else is refused **by name** on stdout. An accepted
  request writes `!TRACE_FENCE <start>`, the command, then `!TRACE_FENCE <end>`.
- `serial/protocol.py` owns `format_command_request`, `parse_command_receipt`,
  `fence_token` (exact `#CAP,<micros>,TRACE_FENCE,<token>` shape only), `fenced_rows`,
  `fenced_window` and `collect_library_observation` (a family is settled only by its
  `LIBOBS,E` row).
- `ManagedCaptureSession` gains `stdin=PIPE`, `send_command(command, nonce, end_nonce)`
  with a **one outstanding request** rule, `complete_command`, `fenced_response` and
  `library_observation`. It refuses when no capture session owns the port, so a request can
  never be written by the wrong owner.

## G2 adversarial review

- COVERED: allowlist refusal by name, and a bare `!` or an embedded-space command is
  malformed rather than forwarded.
- COVERED: one outstanding request; a *different* nonce cannot release the slot.
- COVERED: nonce mismatch and stale windows return `None`; a delayed or repeated start
  fence restarts rather than extends the window.
- COVERED: a coalesced line (dropped newline) no longer completes a window, because the
  fence row must match exactly — an earlier revision accepted the merged line and would have
  reported a partial observation as complete.
- COVERED: a missing terminal `LIBOBS,E` returns an unclosed window, never an empty census.
- TIGHTENING (device-learned): the fence token must be exactly 32 characters; the first
  implementation used an 8-character nonce with an `-end` suffix and the device silently
  ignored both fences. The tokens are now two distinct uuid4 hex values and the requirement
  is pinned by a unit test.

## Verification

- `.venv/bin/python scripts/test_hitl_command_forwarding.py`: **23 passed**, covering request
  parsing and the 32-char rule, the read-only allowlist, accept/refuse receipts, exact fence
  shapes, complete/missing/duplicate/coalesced/delayed/stale windows, the observation family
  with and without its terminal row, one-outstanding-request, a closed pipe, a dead session
  and a timed-out response.
- Device (port `/dev/cu.usbmodem154944801`, read-only, no SD mutation): a managed capture
  session forwarded `!LIB_OBSERVE` and the log contains the request's acknowledgement and
  both fences —
  `#CAP,1706442955,TRACE_FENCE,565ac6640eb248bbaf6a68d781f14091`, then
  `[StorageManager] library observation requested`, then
  `#CAP,1706443002,TRACE_FENCE,b872a25b6cae4655bd8bfd0120a88d58`. The fence window is
  therefore complete end to end.
- The `LIBOBS` family itself did not settle within 60 s on that idle, long-running boot, so
  `library_observation` correctly returned loss rather than an empty census. The observation
  **serving** path (which settles only when the SD session is free and no track is busy) is
  task 2.1/2.2 scope and remains open; this slice proves the transport that will carry it and
  the loss detection around it.

## Completion record

- Host suite **23/23**; existing HITL host suites re-run and green (accepted sequence 42,
  corpus 3, retention plan 23, device shutdown 15).
- Firmware sources are untouched by this slice. Capture-serial build and full
  `pio test -e native` re-run to confirm the tree is unchanged and green.
- Task 2.3 — allowlisted forwarding and single decode — is complete. Task 2.1/2.2 (firmware
  observation request/reply and its coherent identity snapshot) remain open, followed by
  2.4 (measure the instrumentation) and the acceptance stages 3–6.
