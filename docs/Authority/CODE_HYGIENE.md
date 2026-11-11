# Code hygiene

How the firmware's code is kept readable for a normal reader: what comments carry, how large a file or function may
grow before it is reviewed, and the design principles a change follows. Applies to `include/` and `src/`. Names and
vocabulary are [NAMING.md](NAMING.md)'s; ownership and execution bounds are
[ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md)'s. Owner 2026-11-11 ("comments are now overloaded with DEC numbers, for
a normal reader this works confusing"); sources: the Google C++ Style Guide (comments, short functions), the C++ Core
Guidelines NL.1 / NL.2, and common ADR practice for references in code.

Check a change with `scripts/code_hygiene_report.py` (sizes and history references per file).

## 1. Comments say what the code cannot

- **Intent, invariants, units, budgets, and why** a non-obvious choice was made. Not a restatement of the code
  (Core Guidelines NL.1: "don't say in comments what can be clearly stated in code"; NL.2: state intent).
- **Timeless, present tense.** No history: no "was", "now", "after the fix", "before X", dates, owner quotes, device
  images or measured runs. That belongs in [DECISION_LOG.md](../DECISION_LOG.md), the commit message and
  [PROJECT_STATE.md](../Runtime/PROJECT_STATE.md), where it is dated and stays true.
- **Decision references are sparse.** At most one per non-obvious rule, only where a reader would otherwise take a
  deliberate choice for a mistake, in one form: `// DEC-254 (run-once store): …`, the ID with its short name
  ([NAMING.md § Short names beside IDs](NAMING.md#short-names-beside-ids-in-chat)). Stage and task IDs (`E4.6d`,
  `slice 3`) do not appear in code.
- **Measured figures** (µs, bytes) stay only when the code depends on them (a budget, a threshold), with what they bound;
  where they were measured goes to the decision log.
- **File comment:** what the file owns and what does not belong in it — not its history.
- **TODO** names what is open and where it is tracked: `// TODO(E9.8b-3a): …` is the one place a task ID is allowed.

## 2. Sizes are review thresholds, not hard limits

| | Review when over | Then |
|---|---|---|
| A file | **1,000 lines** | Does it hold more than one responsibility? Split by responsibility into files in the same area folder |
| A function | **60 lines** | Can a named step be extracted without hiding the flow? (Google: ~40 lines, "no hard limit") |

A long file or function that is one coherent responsibility may stay; the review is the rule, the split is the usual
outcome. Splits are moves: they keep the compiled image byte-identical, or differ only in the linker's veneer order.

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

## 4. How a cleanup is done

- One area folder or one file per commit; each commit is checked with the native suite and the image hash
  (comment-only and move-only commits must be byte-identical).
- Rationale removed from code is first checked against the decision log; anything only the comment held is moved there.
- No behaviour change rides a hygiene commit.
