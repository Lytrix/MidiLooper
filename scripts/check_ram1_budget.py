"""Post-link RAM1 budget check (runtime-working-set-ownership S3.0).

S3.0's placement gate is a **post-link** property. The structure-size `static_assert`s
pin the data layout, but they cannot see extra instructions, linker veneers,
alignment, or unrelated RAM1 growth -- so they do not close the gate on their own.

This script reads the linked firmware's own size report and fails the build when the
approved budget changes.

Layout it protects (RAM1 = 512 KB):

    variables (DTCM data) + code + padding (ITCM) + free for local variables = 524,288

ITCM is held at 12 banks (393,216 B). Crossing into a 13th bank costs 32,768 B and
drops the local/stack allowance to ~2,176 B, which is the documented silent boot
hang. `padding` is the headroom between the end of ITCM code and that boundary, so
the padding floor is the early-warning line.

The baseline lives in `scripts/ram1_budget.json` and is edited **deliberately, by a
human**, with the reason recorded in the owning change's review artifact. It is never
auto-updated: a reduction below the padding floor, a growth in RAM1 variables, or a
lost local allowance must fail the build until someone explains and re-bases it.

History of the padding floor: 872 was the recorded line before
`runtime-working-set-ownership` began. Its first four slices added 32 B of ITCM
(two call sites to a FLASH-resident predicate in the ITCM manager entries, plus the
latch drive), so the floor was re-based to **840** as a reviewed 32-byte allowance on
2026-09-23 -- not silently reset. ITCM banks stayed at 12 the whole time.
"""

import json
import os
import re
import subprocess
import sys

from SCons.Script import Exit

_RAM1_RE = re.compile(
    r"RAM1:\s+variables:(\d+),\s+code:(\d+),\s*padding:(\d+)\s+free for local variables:(\d+)"
)


def _read_baseline(env):
    # SCons executes extra scripts without `__file__`, so resolve from the project dir.
    path = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "ram1_budget.json")
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _size_report(env):
    """Run the platform's own size tool on the linked ELF and parse the RAM1 line."""
    elf = os.path.join(env.subst("$BUILD_DIR"), "firmware.elf")
    if not os.path.isfile(elf):
        raise RuntimeError(f"RAM1 budget check: linked ELF not found at {elf}")

    tool_dir = env.PioPlatform().get_package_dir("tool-teensy")
    tool = os.path.join(tool_dir, "teensy_size")
    if not os.path.isfile(tool):
        raise RuntimeError(f"RAM1 budget check: size tool not found at {tool}")

    completed = subprocess.run([tool, elf], capture_output=True, text=True, check=False)
    report = (completed.stdout or "") + (completed.stderr or "")
    match = _RAM1_RE.search(report)
    if match is None:
        raise RuntimeError(
            f"RAM1 budget check: could not read the RAM1 line from `{tool}` "
            f"(exit {completed.returncode}):\n{report}"
        )
    variables, code, padding, locals_free = (int(group) for group in match.groups())
    return {
        "variables": variables,
        "code": code,
        "padding": padding,
        "locals_free": locals_free,
        "itcm": code + padding,
    }


def _check(actual, baseline):
    """Return a list of failure strings; empty means the budget is unchanged."""
    failures = []

    if actual["itcm"] != baseline["itcm_bytes"]:
        failures.append(
            f"ITCM region changed: {actual['itcm']:,} B (code {actual['code']:,} + "
            f"padding {actual['padding']:,}) vs approved {baseline['itcm_bytes']:,} B. "
            f"{baseline['itcm_banks']} banks = {baseline['itcm_bytes']:,} B; a 13th bank "
            f"costs 32,768 B of the local allowance and is the documented silent boot hang."
        )
    if actual["variables"] != baseline["ram1_variables"]:
        delta = actual["variables"] - baseline["ram1_variables"]
        failures.append(
            f"RAM1 variables changed: {actual['variables']:,} B vs approved "
            f"{baseline['ram1_variables']:,} B ({delta:+,} B). DTCM growth comes out of the "
            f"local allowance."
        )
    if actual["locals_free"] != baseline["locals_free"]:
        delta = actual["locals_free"] - baseline["locals_free"]
        failures.append(
            f"local allowance changed: {actual['locals_free']:,} B vs approved "
            f"{baseline['locals_free']:,} B ({delta:+,} B)."
        )
    if actual["padding"] < baseline["padding_floor"]:
        failures.append(
            f"padding below the approved floor: {actual['padding']:,} B < "
            f"{baseline['padding_floor']:,} B. That floor is a reviewed allowance, not an "
            f"automatic reset -- explain the change and re-base `padding_floor` deliberately, "
            f"or move code out of ITCM."
        )
    return failures


def _run(target, source, env):
    """SCons post-action on the linked firmware ELF (signature is SCons's)."""
    del target, source
    try:
        actual = _size_report(env)
    except (RuntimeError, OSError) as error:
        print(f"\n[RAM1 budget] ERROR: {error}\n")
        Exit(1)

    baseline = _read_baseline(env)
    failures = _check(actual, baseline)

    print(
        f"[RAM1 budget] variables {actual['variables']:,} / ITCM {actual['itcm']:,} "
        f"(code {actual['code']:,} + padding {actual['padding']:,}) / locals "
        f"{actual['locals_free']:,} | approved floor: padding >= {baseline['padding_floor']:,}"
    )

    if failures:
        print("\n[RAM1 budget] FAILED -- the approved RAM1 budget changed:\n")
        for failure in failures:
            print(f"  - {failure}")
        print(
            "\n  If this change is intended, re-base scripts/ram1_budget.json in the same\n"
            "  commit and record the reason in the owning change's review artifact.\n"
        )
        Exit(1)

    if actual["padding"] > baseline["padding_floor"]:
        print(
            f"[RAM1 budget] {actual['padding'] - baseline['padding_floor']:,} B of headroom "
            f"above the floor (informational; the floor is only re-based deliberately)."
        )


# Registered as a post-action on the linked ELF, so this reads the real link result rather
# than the pre-link script-load state.
Import("env")
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _run)
