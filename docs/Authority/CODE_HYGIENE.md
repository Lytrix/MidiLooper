# Code hygiene

How the firmware's code is kept readable for a normal reader: what comments carry, how large a file or function may
grow before it is reviewed, and the design principles a change follows. Applies to `include/` and `src/`. Names and
vocabulary are [NAMING.md](NAMING.md)'s; ownership and execution bounds are
[ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md)'s. Owner 2026-11-11 ("comments are now overloaded with DEC numbers, for
a normal reader this works confusing"); sources: the Google C++ Style Guide (comments, short functions), the C++ Core
Guidelines NL.1 / NL.2, and common ADR practice for references in code.

Check a change with `scripts/code_hygiene_report.py` (sizes and history references per file, folder `AGENTS.md` caps).

## 1. Comments say what the code cannot

- **Intent, invariants, units, budgets, and why** a non-obvious choice was made. Not a restatement of the code
  (Core Guidelines NL.1: "don't say in comments what can be clearly stated in code"; NL.2: state intent).
- **Timeless, present tense.** No history: no "was", "now", "after the fix", "before X", dates, owner quotes, device
  images or measured runs. That belongs in [DECISION_LOG.md](../DECISION_LOG.md), the commit message and
  [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md), where it is dated and stays true.
- **Decision references are sparse.** At most one per non-obvious rule, only where a reader would otherwise take a
  deliberate choice for a mistake, in one form: `// DEC-254 (run-once store): …`, the ID with its short name
  ([NAMING.md § Short names beside IDs](NAMING.md#short-names-beside-ids-in-chat)). Stage and task IDs (`E4.6d`,
  `slice 3`), plan sections and owner quotes do not appear in code. Specification requirement IDs (`OVL-1`,
  `Q-LEN-1`, `OUT-POLY`) may name the rule a line implements: they are normative and timeless.
- **Measured figures** (µs, bytes) stay only when the code depends on them (a budget, a threshold), with what they bound;
  where they were measured goes to the decision log.
- **File comment:** what the file owns and what does not belong in it — not its history.
- **TODO** names what is open and where it is tracked: `// TODO(E9.8b-3a): …` is the one place a task ID is allowed.

## 2. Sizes are review thresholds, not hard limits

| | Review when over | Then |
|---|---|---|
| A file | **1,250 lines** | Does it hold more than one responsibility? Split by responsibility into files in the same area folder |
| A function | **60 lines** | Can a named step be extracted without hiding the flow? (Google: ~40 lines, "no hard limit") |

A long file or function that is one coherent responsibility may stay; the review is the rule, the split is the usual
outcome. Splits are moves: they keep the compiled image byte-identical, or differ only in the linker's veneer order.

How a split is done:
- **A class:** the class keeps every declaration with its doc comment (the class reads as its interface); the bodies of
  one responsibility move to `<Class><Part>.h` beside it as `inline` out-of-class definitions with a trailing return
  type (`inline auto Class<T>::f(…) -> R`), and the class's header includes its parts after the class. `inline` keeps
  the compiler's inlining as it was; attributes (`CE_COLD`, section placement) stay on the declaration.
- **A separate type** inside a large header moves to its own header (`PressDetector.h`).
- **`main.cpp`:** a section moves to `src/<Part>.h`, included where the section stood (inside the anonymous
  namespace); the part says so in its first comment and is not a standalone header.

## 3. Design principles

**DRY — don't repeat yourself.** One implementation of a shared sequence, as a function or helper beside its users;
a second copy is the moment to extract, not the third. Duplicated test fixtures are allowed when they keep a test
readable on its own.

**SOLID**, as it applies to a fixed-memory, header-only engine on a microcontroller:

- **Single responsibility:** a class has one reason to change. The area folders (NAMING.md § Engine source layout)
  are the coarse split; within them, one owner per concern (persistence does not draw, the surface does not compose).
- **Open / closed:** extend through the existing extension points — a new job class through the `WorkerScheduler`, a
  new card through `CardIo`, a new window through the surface's window states — rather than by editing every caller.
- **Liskov substitution:** an implementation of an interface keeps its contract: a `CardIo` fake in a test and
  `SdCardIo` on the device must agree on every promise the interface documents (never waits, sector addressing).
- **Interface segregation:** a consumer depends on the narrow part it uses — pass the pool, the runs or the history a
  function needs, not `EngineRoot` whole, unless it really coordinates them.
- **Dependency inversion:** depend on abstractions. In this engine that is mostly **template parameters** (`Pool`,
  `Runs`, `Card`, compile-time, no indirection on the tick path) and a few **virtual interfaces** at hardware
  boundaries (`CardIo`, `ColdSink`), so the engine builds and is tested natively without the Teensy.

These sit under the project's own constraints, which win where they conflict: fixed memory (no allocation after
boot), bounded grains on the playing path, RAM1 placement, and the execution bounds in ARCHITECTURE_RULES.

## 4. Where the rest goes

Rationale leaves the code, it is not deleted. Three places, each read at a different moment:

| Place | Holds | Read |
|---|---|---|
| The code comment | the timeless rule: intent, invariant, unit, budget; at most one `DEC-### (short name)` | always, with the code |
| The area folder's `AGENTS.md` (e.g. `include/ContentEngine/Composition/AGENTS.md`) | what an agent must know before editing that folder: its invariants, its traps, and the approaches already tried and rejected there — one line each, with the `DEC-### (short name)` that holds the evidence | before editing a file in that folder |
| [DECISION_LOG.md](../DECISION_LOG.md) | the history: dates, owner quotes, measurements, device images, and the full **Tried and rejected** block (§ 5) | searched by topic or ID, never read whole |

A folder `AGENTS.md` is capped at **120 lines** (`scripts/code_hygiene_report.py --strict` fails above it). It holds
what is true now: a line is **replaced** when its rule changes and removed when the code it guards is gone, never
appended as history. Its sections, in order: *Owns*, *Invariants*, *Traps* (including *Don't* lines for rejected
approaches), *Decisions* (the IDs that matter for the folder, one short name each). Measurements stay in the decision
log; a line points there.

## 5. Tried and rejected

An approach that was built, measured and dropped is a result, not a dead end: it is usually the obvious one, so the
next change tries it first. It is recorded in three depths:

1. **The decision log, in full.** The DEC entry the work belongs to carries a **Tried and rejected** table (template:
   [SESSION_CLOSEOUT.md](../Templates/SESSION_CLOSEOUT.md)):

   | Tried | Measured | Rejected because | Revisit if |
   |---|---|---|---|

   *Measured* names the evidence (figures, device image, test, capture). *Revisit if* names the assumption that made
   it fail, so a rejection does not become dogma: when that assumption changes, the approach may be right.
2. **The folder `AGENTS.md`, one line** — only for an approach an agent editing that folder is likely to retry:
   `- Don't <approach>: <one-clause result>. DEC-### (short name).`
3. **A guard test, where the failure is measurable.** A test that would fail if the rejected approach came back (a
   feasibility gate, a grain-count or byte-identity check) names the lesson in its comment, so a failure explains
   itself: `// Guards DEC-### (short name): <the rejected approach> <what it broke>.`

In the code itself only a one-line warning at the exact spot where the obvious change goes wrong, e.g.
`// Stays in RAM1: from flash the status line costs about 4x. DEC-297 (deep-overdub timing).`

## 6. How a cleanup is done

- One area folder or one file per commit; each commit is checked with the native suite and the image hash
  (comment-only and move-only commits must be byte-identical).
- Rationale removed from code is first checked against the decision log; anything only the comment held is moved
  there, or to the folder's `AGENTS.md` (§ 4). Every "tried / reverted / did not work" comment becomes a Tried and
  rejected row (§ 5).
- No behaviour change rides a hygiene commit.
