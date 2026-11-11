# Composition — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Composition in the decision log.

## Owns
- Effective content, derived and never stored: `SpanWindowComposer` merges a state's runs into span windows
  (overlap rules OVL-1..8, output admission OUT-POLY / Q-OUT-2, held-loop re-strike Q-LEN-1); `SpanWindow` and
  `WindowPlayer`; restart checkpoints (`CheckpointTable`); the shared edit-hide index (`EditIndexStore`); a resized
  loop's fit run (`FitRun`); groove templates and the groove pass (`Groove`, `GroovePass`).

## Invariants
- Merge order is DEC-244 (tick, NoteOff < NoteOn < other, pass order, append order) everywhere, including checkpoints.
- Selection never changes whether an instance is effective or when it ends, so a loop repeats identically from the
  first steady cycle. A checkpoint is the composer's whole state: resuming equals composing through.
- A grain is bounded: `composeRows` counts merged rows plus look-ahead reads; a bind runs `kRunsPerStep` runs a
  grain (`bindStep`, one call a grain); a resume seeks in the same steps; the loop wrap (`nextCycle`, O(runs)) ends its
  grain and composing resumes on the next.
- The composers live in PSRAM and are not constructed: `init` sets every field, and each job keeps its own
  "bind started" flag (`bindStarted_`), never a flag on the shared composer.
- `dirty_` marks the pitches whose occluder row was raised since the last bind; a bind clears and `capture` scans
  only those, in ascending pitch order (a checkpoint is unchanged by the mask).
- A fit run names content runs by their **position** in the state's run list, never by RunId (DEC-285): RunIds are
  reassigned on load, copy and get a loop.
- The runs before a groove pass are composed empty, in place, so a fit lookup's position still names the same run.
- A manifest identity is never reused, so an edit-hide index slot can never answer for another revision.

## Traps
- Don't speed up stream selection with a tournament tree: no change in the worst grain; selection is not the cost.
  DEC-297 (deep-overdub timing).
- Don't count stream selection (head keys) as grain work: grains fewer rows, the E5 feasibility gate test then
  misses its deadlines. DEC-297.
- Don't add per-pitch onset masks for the look-ahead (`laterPassStartsWhileSounding`): the average row falls, the
  worst grain does not, and bind grows. DEC-297.
- Don't judge a timing change by the per-row average: attribute the worst grain per job class (temporary DWT probes).
- Don't start a stepped bind in a grain of its own: the cold start then plays a grain later (an `engine_root` test
  catches it). Guard tests: `test_a_source_binds_in_steps_of_runs`, `test_the_loop_wrap_is_a_grain_of_its_own`.
- Don't key a fit lookup by RunId: after a load a note past the end comes back.

## Decisions
- DEC-244 (equal-tick order) · DEC-246 (one read path) · DEC-257 (loop length is a play window) ·
  DEC-283 (loop start in the stored loop) · DEC-284 (HOME and variation) · DEC-285 (fit run by position) ·
  DEC-286 (open-instance limit, rebuild view identity) · DEC-287 (groove pass) · DEC-288 (background groove build) ·
  DEC-297 (deep-overdub timing)
