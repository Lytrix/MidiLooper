# ContentEngine — folder notes

Read before editing a file in this folder. The area folders below it have their own `AGENTS.md`.
Rules: [CODE_HYGIENE.md § 4–5](../../docs/Authority/CODE_HYGIENE.md). History behind these lines: DEC-298 (comment
history, moved) § Engine root in the decision log. Layout and names: NAMING.md § Engine source layout (DEC-296).

## Owns
- `EngineRoot`: the engine at production capacity — placement of its PSRAM (`EnginePsram`, EXTMEM) and RAM2
  (`EngineRam2`, DMAMEM) regions, `init`, the main-loop entry points (`submitJobs`, `runGrain`, `clockTick`) and the
  library / load / groove operations. `EngineCore`: the storage core and its budget pins. `EngineConfig`: every fixed
  capacity. `WorkerScheduler`: grains by class (Hard, transition, display, background, persistence) and deadlines.

## Invariants
- Nothing is sized at run time; no allocation after boot. Neither region is zeroed at reset: `init` writes every
  field the engine reads, pool first. `EngineRoot::init` runs from flash (`CE_COLD`, RAM1 headroom).
- The budgets (`kPsramBudget`, `kRam2Budget`, the persistence and composer bounds) are measured values; a change is a
  budget change and its commit names what grew and by how much. PSRAM is asserted on the host's 64-bit layout, RAM2
  and the persistence bound on ARM.
- Build gates (D1): DTCM ≤ 96,224 B; ITCM padding ≥ 840 B; RAM1 locals ≥ 34,848 B; RAM2 statics ≤ 226,593 B.
- A grain stays under g = 810 µs (`kGrainUs`), and the main loop's non-engine stretch under B_max = 2,000 µs.
- A late join is at most a 32nd and 25 ms after its boundary.

## Traps
- Don't trust a budget change measured on one layout only: PSRAM grows on the host and ARM differently (the ARM
  persistence bound has its own assert); read the compiler's "evaluates to" for the real size.
- Off-hot-path code (`CE_COLD`) goes to flash; code a grain or the Clock path runs stays in RAM1.

## Decisions
- DEC-252 (bounded content engine) · DEC-254 (run-once store, per-slot undo) · DEC-296 (source layout and names) ·
  DEC-297 (deep-overdub timing)
