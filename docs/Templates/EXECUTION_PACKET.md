# Execution packet template

Authority: [IMPLEMENTATION_READINESS.md](../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice).
Store an instance in the active change's existing design/review artifact; one section per implementation
slice is enough. Fields may link to an exact existing contract. Mark inapplicable fields with a reason.
This is a template, not evidence that any task passed.

## Identity and scope

- Packet/task IDs and approved contract/spec/DEC references:
- Status: specified / fixture implemented / native verified / device verified / blocked:
- Owner and production entry → deferred worker → commit/retirement:
- Primary files and existing test harness:
- Preconditions already present; prerequisites included in this slice; external blockers:
- Mutation, postconditions and explicit non-goals:
- Commit boundary (independently buildable; temporary compatibility removal if any):

## Oracle fixed before behavior edits

For each case, list:
- Stable case ID; initial logical/content/resource state:
- Ordered actions and worker/tick steps:
- Expected observation **after each step**, including pending requests and owning identities:
- Completion/supersession/fault result and exactly-once effects:
- Preserved invariants and a counterexample that must fail:

## Controls and coverage

- Existing time/allocator/I/O seams reused; minimal new test-only seam if necessary:
- Production paths exercised; mocks/stubs excluded from the asserted path:
- Busy/stale/failure/recovery/transport-switch/overlap cases relevant to this slice:
- Independent expected content/trace and source of numeric limits:
- Baseline failure or passing reuse evidence; explicitly identify untestable-before-link cases:
- Bounded drain limit and last-state failure output; optional fixed fuzz seed:

## Verification recipe

- Exact focused command(s), required suite/case IDs, expected exit/count criteria:
- Required broad/native/build checks:
- Device-only claim, stimulus/entry point, setup and content/timing oracle:
- If measurement: inputs, units, attribution and subsequent decision consuming the numbers:
- Evidence path in the change; baseline revision and relevant dirty-tree diff:

## Completion record

Record each required check's actual command, revision/context, result/counts and evidence link.
Mark NOT RUN, BLOCKED, or NOT APPLICABLE explicitly; never copy a planned result as a pass.
- Oracle/contract unchanged, or authorized change and supporting evidence:
- Fixture implemented and discriminating assertion demonstrated:
- Native verification:
- Build/RAM1 verification:
- Device verification:
- Required reviews and durable docs:
- Task IDs eligible for checkoff; stage gates still open:
