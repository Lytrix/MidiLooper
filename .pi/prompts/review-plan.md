---
description: Paste newest docs/Plans plan into ChatGPT project, fold review into plan
argument-hint: "[plan-path-or-name]"
---

# /review-plan

Execute the **ChatGPT plan-review flow** end-to-end. Do **not** implement firmware.

Plan target: `${1:-newest file under docs/Plans/ by mtime}`

Extra user notes (optional): `${@:2}`

## Goal

1. Resolve the plan file
2. Paste it into the ChatGPT project composer and submit
3. Wait for the review response
4. Update the plan with the findings
5. Return only the **main changes** made to the plan

## Defaults

- Plans directory: `docs/Plans/`
- ChatGPT project URL:
  `https://chatgpt.com/g/g-p-6a3255595c3c8191aaaf750b521f9e8d/project`
- Prefer `browser_execute` (CDP). If Chrome has no remote debugging, launch a
  debug Chrome profile or connect via `BU_CDP_WS` / `session.connect({ wsUrl })`.
- Do **not** edit production firmware / `.cpp` / `.h` as part of this command.

## Step 1 — Resolve the plan

If `$1` is provided:

- Treat it as a path, basename, or change/plan name.
- Resolve under `docs/Plans/`, `.cursor/plans/`, or an OpenSpec change if clearly
  intended. Prefer an exact path match.

If `$1` is empty:

- Choose the **most recently modified** `*.md` under `docs/Plans/`
  (exclude `README.md` and `archive/` unless the user asked for those).
- Confirm briefly in chat: which plan was selected and its mtime.

Read the full plan before proceeding.

## Step 2 — Open the ChatGPT project

Using `browser_execute`:

1. `await session.connect()` (or `{ wsUrl }` if needed).
2. Find/create a page target for the ChatGPT project URL above.
3. `await session.use(targetId)`.
4. Confirm the composer is available:
   - contenteditable `#prompt-textarea` (ProseMirror), or
   - fallback `textarea[name="prompt-textarea"]`
5. Screenshot once if login / interstitial / wrong project is shown.
   If not logged in or wrong project, stop and tell the user what to fix.

## Step 3 — Paste the plan and submit

1. Focus `#prompt-textarea`.
2. Clear any leftover draft (select-all + backspace / empty ProseMirror).
3. Insert the **full plan markdown** with `Input.insertText` in chunks if needed.
4. Submit via the real send control:
   - Prefer clicking `button[data-testid="send-button"]` / `aria-label="Send prompt"`.
   - Do **not** click Expand / Collapse / Share / dictation.
   - Enter is acceptable only if the send button is unavailable.
5. Confirm submission:
   - composer clears / new chat URL appears / Stop-generating control appears.
6. If submit fails, recover and retry once; then stop with a screenshot.

## Step 4 — Wait and capture the review

Poll until generation finishes (Stop button gone and assistant text stable):

- Extract the **last assistant message** text.
- Keep a local copy of that review text for the plan update.
- Screenshot the finished review once.

If the response is truncated, empty, or clearly a refusal / wrong-context answer,
stop and report that. Do not invent findings.

## Step 5 — Fold findings into the plan

Update the selected plan markdown in-repo. Prefer surgical edits:

1. Update **Status** to note ChatGPT project review date + verdict
   (APPROVED / APPROVED WITH TIGHTENING / REVISE).
2. Apply concrete tightenings the review requires (preconditions, commit
   boundaries, validation criteria, ownership pins, non-goals, etc.).
3. Append a **Pre-implementation review (ChatGPT project, YYYY-MM-DD)** section
   with:
   - Source/chat identity if available
   - Verdict
   - Keep / change / do-not-bundle points
   - Proceed? YES/NO
4. Preserve the plan’s existing architecture and RC content unless the review
   contradicts it with a clear required change.
5. Do **not** start Stage implementation in this command.

Also do a quick local consistency pass while editing:

- Named owners/functions still make sense against the codebase (`rg` / read).
- Do not weaken freshness≠completeness, ownership pins, or explicit non-goals
  unless the review gives a hard blocker and the user must decide.

If the review says **NO / revise before coding**, update the plan with blockers
and stop without claiming approval.

## Step 6 — Return summary (required)

After editing, reply with a short summary only:

```markdown
## /review-plan result

**Plan:** <path>
**ChatGPT verdict:** <APPROVED | APPROVED WITH TIGHTENING | REVISE>
**Proceed?** <YES | NO>

### Main changes made to the plan
1. …
2. …
3. …

### Open before coding
- … (or “none”)
```

Keep the bullet list to the real deltas — not a restatement of the whole plan.

## Failure / recovery rules

- Chrome CDP unavailable → explain how to enable remote debugging / reuse the
  existing debug profile; do not pretend the review happened.
- Wrong ChatGPT project or login wall → stop with screenshot.
- Ambiguous plan selection → ask which plan; do not guess among several recent files.
- Review incomplete → do not update the plan with guessed findings.

## Arguments from user

$@
