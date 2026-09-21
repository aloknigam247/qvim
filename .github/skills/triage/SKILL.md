---
name: triage
description: Use when the user wants to triage a task/bug/idea into a well-formed GitHub issue for a future agent to implement. Discusses the task, investigates what needs doing and where, checks existing issues for duplicates (and warns) and related issues (and links them), classifies it — labelling investigation-only work `spike` — and creates a GitHub issue only after the user accepts. Trigger phrases include "triage", "triage this", "create an issue", "file an issue", "turn this into a task", "raise a ticket".
---

# Triage

Turn a rough task, bug, or idea into a **well-formed GitHub issue** that a *different agent* can implement later with no prior conversation context. The skill discusses the task, investigates the codebase to find what needs doing and where, classifies it, and — **only after the user explicitly accepts** — creates the issue in this project's GitHub repo.

## Principles

- **Discuss first, create last.** Never run `gh issue create` until the user has reviewed the drafted issue and explicitly accepted it.
- **Check for duplicates and related issues first.** Before drafting, search the repo's existing issues (open *and* closed). If the task duplicates an existing issue, say so and steer the user away from filing a redundant one. If related-but-distinct issues exist, link them via GitHub's native issue relationships.
- **Always rubber-duck the draft.** Every triage runs the rubber-duck agent over the drafted issue to catch flawed root causes, invariant violations, and weak tests *before* the user sees it.
- **The issue is for another agent, at another time.** Write it as a self-contained task: enough context, file references, and acceptance criteria that an agent with zero conversation history can pick it up and implement it.
- **Anchor to stable identifiers, not coordinates.** An issue is implemented at an unknown later time against a changed tree. Reference symbols, string literals, config keys, and search patterns — never line numbers, occurrence counts, or verbatim multi-line snapshots that the codebase will invalidate. A frozen inventory goes stale *and* misleads: the implementing agent trusts it as exhaustive and skips rediscovering occurrences added after the issue was filed. The issue points the way; the implementing agent re-discovers the exact scope.
- **Match detail to task type.** For a narrow **bug**, the exact offending lines *are* the deliverable — a tight snippet of the actual defect is worth freezing. For **features / refactors / renames / broad sweeps**, staleness dominates: give a discovery recipe (search patterns + stable anchors) and the compat/invariant rules, not an enumerated file list.
- **Ask for missing details.** Do not guess when scope, expected behavior, or acceptance criteria are ambiguous.

## Input

The user provides a task, bug report, or idea in their request. It may be vague (e.g., "the cursor blink phase resets on every nvim window event") or a feature ask.

## Steps

### 1. Understand and discuss the task

1. Restate the task in your own words to confirm understanding.
2. Determine the **type of work**: is it a bug, a new feature, a refactor, etc.? (This informs the category later.)
3. Ask the user any clarifying questions needed to write a complete task. Use the `ask_user` tool. Typical gaps to probe:
   - Expected vs. actual behavior (for bugs)
   - Which **layer(s)** are affected (msgpack-rpc transport, redraw dispatch in `NvimConnector`, `GridModel`/`HighlightTable`/`ModeInfo` state, the `GridItem`/`CursorItem` paint path, `InputHandler` key encoding, or the QML shell)
   - Scope boundaries (what is explicitly *out of scope*)
   - Acceptance criteria / definition of done
   - Any constraints (per-frame redraw cost, the paint-path purity rule, no `msgpack::object` copies, thread affinity of `QRawFont`-backed objects, backwards-compat with new redraw event variants)
   - What tests should add/validate the fix (triaged issues include their tests as part of the fix)
4. Do **not** proceed to investigation until the task is clear enough to investigate.

### 2. Investigate "what needs to be done, and where" (via subagent)

Launch a fresh **general-purpose** subagent (via the Task tool) with **no prior conversation context** — build the prompt from scratch. This keeps the main conversation clean.

The subagent prompt must include:
- The **task description** as clarified in step 1
- The **task type** (bug/feature/refactor/etc.)
- The affected **layer(s)** if known
- The instruction to read the root `AGENTS.md` for the architecture (QML shell + `QQuickPaintedItem` grid renderer + embedded `nvim --embed` over msgpack-rpc), the "Top priorities" (correctness/performance) rules, the "Layout" map, and any nested `AGENTS.md` (`qml/AGENTS.md`, `src/AGENTS.md`, `tests/AGENTS.md`) for the subtree being touched before touching anything
- The **investigation goals** below

The subagent is responsible for:
1. Reading the root `AGENTS.md` (and the relevant nested `AGENTS.md`) for architecture, conventions, correctness/performance gates, and gotchas.
2. Using grep/glob/view to locate the work. **Scale the output to the task type** (see the "Match detail to task type" principle):
   - **Narrow bug:** capture the exact offending lines as a short verbatim snippet with a `file:line` ref — the defect *is* the deliverable and won't move much.
   - **Feature / refactor / rename / broad sweep:** do **not** enumerate every hit. Identify the **distinct surfaces/categories** the work touches, give **one representative entry point** per surface (as a stable anchor — a symbol, string literal, or config key), and record the **grep/glob patterns** that locate the rest. Prefer "find the remaining refs via `<pattern>`" over a frozen file list or an occurrence count.
3. Identifying the **component(s)** involved (e.g., `MsgpackRpc`, `NvimConnector`, `GridModel`, `HighlightTable`, `ModeInfo`, `GridItem`, `CursorItem`, `InputHandler`, a `*Model`, or a `qml/` component).
4. Confirming the **root cause** (for bugs) by tracing the actual code — not guessing. Remember the redraw event stream is the single source of truth; `ext_hlstate` makes `hl_attr_define` a 4-element event; `paint()` must be a pure function of (`GridModel`, `HighlightTable`, `ModeInfo`, cursor state) and must never reach back into RPC state.
5. Sketching a **proposed approach** consistent with existing patterns (redraw event handlers, the reactive `QObject`-proxy for Repeater delegates, `Q_PROPERTY`/`Q_SIGNAL` wiring, text-run batching by `(hl_id, font_state)`) and **without regressing per-frame redraw cost or breaking paint-path purity**.
6. Noting **testing implications**: which test tier maps to the changed source — Tier 1 `tests/unit/` (headless, `QTEST_GUILESS_MAIN`), Tier 2 `tests/integration/` (real `nvim --embed`, `minimal` QPA + software renderer), or Tier 3 `tests/qml/` (`qmltest`) — which **existing** tests will break and must be updated, and what **new** test cases would add/validate the fix (specific test function/method names and the behavior each asserts). For visual changes, note whether `scripts/screenshot-qvim.ps1` / the `visual-validate-qvim` skill is the authoritative gate.
7. Flagging any ambiguity or missing information the user still needs to resolve.

The subagent must **return a structured report** containing:
- `surfaces` — the distinct categories of change the task touches (for a narrow bug this is usually one); for each, a short reason and its compat/invariant rules if any
- `affected` — representative entry points as `{file, symbol, why}`. For bugs, the specific site(s). For features/refactors/renames, **one canonical example per surface**, not an exhaustive inventory
- `searchStrategy` — the grep/glob patterns and stable anchors (symbol names, string literals like the `qvim_yank` rpcnotify name, config keys) that let the implementing agent **re-discover the full scope** later. This replaces a frozen file enumeration
- `codeSnippet` — for a **narrow bug only**, a short verbatim excerpt of the offending lines with a `file:line` ref and language for fencing. Omit for features/refactors/renames (reference the pattern, not the lines)
- `components` — the component(s) involved
- `rootCause` — for bugs, the confirmed root cause with file:line references (or "n/a")
- `approach` — the proposed implementation approach
- `testing` — test files/impact, the exact `ctest --preset release -R <selector>` (or targeted test binary), a concrete **regression test snippet** whose assertions pin *this specific* fixed behavior (see Testing requirements below), and whether a screenshot/`visual-validate-qvim` gate is required
- `openQuestions` — anything still unclear

Reference stable anchors (symbols, string literals, config keys), **never line numbers or occurrence counts** in the enumerated scope — the issue is implemented later against a moved tree.

If the subagent returns `openQuestions`, resolve them with the user (via `ask_user`) before drafting the issue.

### 3. Check for duplicate and related issues

Before classifying and drafting, search the repo's existing issues so the new issue isn't redundant and gets linked to anything relevant. Search **both open and closed** issues.

1. Build a few search queries from the task's key terms — the affected component (e.g. `GridItem`, `InputHandler`, `CursorItem`), the symptom, and any distinctive keywords. Run them with `gh`:

   ```pwsh
   gh issue list --state all --search "<keywords>" --limit 20 --json number,title,state,url
   # broader full-text search across the repo:
   gh search issues --repo <owner>/<repo> "<keywords>" --state all --limit 20 --json number,title,state,url
   ```

2. Read the candidate issues (`gh issue view <n>`) closely enough to judge each one. Classify every genuine match as either:
   - **Duplicate** — same root cause / same requested change. The new issue would be redundant.
   - **Related** — distinct task, but overlapping area, dependency, blocker, or follow-up. Worth linking, not merging.

3. **Handle duplicates.** If one or more open duplicates exist, **stop and inform the user** (via `ask_user`) with the issue number(s), title(s), and URL(s), and recommend *not* filing a new one — instead point them at the existing issue (add a comment/upvote there if they want). Only continue to draft a new issue if the user explicitly confirms it is meaningfully different. A closed issue covering the same thing is worth surfacing too (it may need reopening rather than a fresh issue).

4. **Record related issues** for linking. Keep a list of `{number, title, url, relationship}` where `relationship` is a short reason (e.g. "blocked by", "follow-up to", "same subsystem"). These get linked from the drafted issue in step 5 using GitHub's native cross-reference relationships (`Related to #N`, `Blocked by #N`, `Part of #N`, `Follow-up to #N`), which surface in both issues' timelines.

### 4. Classify the task

Assign **one or more categories** from the fixed set below (these map 1:1 to GitHub labels):

| Category      | Use case                                        |
|---------------|-------------------------------------------------|
| `bug`         | Something is broken or behaves incorrectly      |
| `feature`     | New capability                                  |
| `refactor`    | Restructuring without behavior change           |
| `test`        | Adding or updating tests                        |
| `docs`        | Documentation changes                           |
| `chore`       | Maintenance, dependencies, cleanup              |
| `performance` | Speed/memory/efficiency improvements            |
| `tech-debt`   | Paying down accumulated shortcuts               |
| `spike`       | Investigation/exploration only — no code or docs changes (research, feasibility, root-cause analysis) |

Pick the categories that genuinely apply (usually one primary, occasionally a secondary such as `bug` + `tech-debt`).

**Spike issues.** Apply the `spike` label when the task is purely investigation, exploration, or research whose deliverable is *findings* (a decision, a root-cause analysis, a feasibility answer, a proposed design) and that **does not itself change any source code or documentation**. A spike is `spike` *alone* — if the task is expected to also land a code/doc change, it is not a spike, use the matching category instead. Because a spike ships no code, the **Testing requirements** section of its issue is replaced by **Investigation deliverables** (the concrete questions to answer and the artifact/output the spike must produce, e.g. a written findings summary or a follow-up issue), and the **Acceptance criteria** are about the findings being produced, not about tests passing.

### 5. Draft the issue and present it for acceptance

1. Compose the issue **title** in Conventional Commit style: `<type>(scope): <short description>` (e.g., `fix(GridItem): cursor blink phase resets on nvim window event`). Use the component as the scope where it helps. For a spike, use `spike(scope): <question to answer>`.
2. Compose the issue **body** with these sections (omit a section only if truly not applicable):
   - **Summary** — one or two sentences.
   - **Context / Background** — why this matters; the reported symptom or motivation; the affected layer(s). **For a narrow bug**, embed the offending code as a fenced snippet (from the subagent's `codeSnippet`) with a `file:line` caption so the future agent sees exactly what code the task refers to. **For a feature/refactor/rename**, describe the *pattern* and the surfaces involved — do not paste a frozen multi-line snapshot or an occurrence count that the tree will invalidate.
   - **Related issues** — if step 3 found related-but-distinct issues, list each one using a GitHub cross-reference keyword so the relationship is recorded natively in both issues' timelines: `Related to #N`, `Blocked by #N`, `Part of #N`, or `Follow-up to #N` (one per line, with the short reason). Omit this section if there are none. Do **not** list duplicates here — duplicates are resolved with the user in step 3, not linked from a new redundant issue.
   - **Affected surfaces & components** — for a narrow bug, the specific `file` -> `symbol` -> reason sites. For a feature/refactor/rename, the **surfaces/categories** (from the subagent's `surfaces`) with **one representative entry point each** plus the **discovery command** that finds the rest (from `searchStrategy`). Prefix this section with a standing note to the implementing agent: *"This is a starting map, not an exhaustive inventory — re-run the discovery searches below and treat their output as authoritative; occurrences may have been added since this issue was filed."* Reference the "Layout" / "Where to extend" guidance in `AGENTS.md` where relevant.
   - **Proposed approach** — the subagent's `approach`, plus root cause for bugs. Call out any invariant that must **not** break (paint-path purity, per-frame cost, no `msgpack::object` copies, render-thread affinity, well-bracketed nvim keycodes).
   - **Acceptance criteria** — a checklist of concrete, verifiable outcomes. For a spike, these are about the findings being produced (questions answered, decision recorded, follow-up filed), not tests passing.
   - **Testing requirements** — **required for any issue that changes code or docs.** Spell out the concrete test changes needed to add and/or validate the fix, so a future agent (or the user) can verify the change is done. Triaged issues are expected to include their tests as part of the fix — do **not** add a "tests only when explicitly requested" caveat here. **For a `spike` issue, replace this section with an _Investigation deliverables_ section** — a spike changes no code or docs, so it has no tests; instead list the concrete questions to answer and the artifact the spike must produce (a written findings summary, a decision, a follow-up implementation issue). For non-spike issues:
     - The exact test tier/target that maps to the changed source (Tier 1 `tests/unit/`, Tier 2 `tests/integration/`, or Tier 3 `tests/qml/`), and the test binary name where known (e.g., `test_input_handler`, `test_qml`).
     - **Existing tests that will break** and must be updated — name the test target and function, and say how. Reference test functions by name, not line number.
     - **New test cases** that add/validate the fix — proposed test function/method names and the specific behavior/assertion each covers. Include a concrete **regression test code snippet** (fenced) that the future agent can drop in. Use `QSignalSpy` for redraw assertions and `dumpAscii()` for grid snapshots per repo convention.
     - **Pin the fix, not incidental state.** Each new test's assertions must fail today (before the fix) and pass only once *this specific* change is made, and must target the fixed behavior narrowly — assert the exact grid/highlight/cursor/keycode value that changes, not a broad snapshot or unrelated surrounding state — via a **hard `QVERIFY`/`QCOMPARE`**, never a `qWarning` or soft pixel-band heuristic (which respond to kerning/sub-pixel changes and produce false positives).
     - The exact command to run for validation, e.g. `ctest --preset release -R 'attach_and_render|resize|insert_and_quit'` for paint-path/redraw changes, `ctest --preset release -R test_input_handler` for `InputHandler`, `ctest --preset release -R test_qml` for QML. Note that the build must pass under `/W4 /permissive-` with warnings treated as bugs.
     - **Visual gate.** For paint-path / font / cursor / selection / highlight changes, require capturing the rendered window via `scripts/screenshot-qvim.ps1` (or the `visual-validate-qvim` skill) and inspecting it — a green ctest run alone is not proof.
   - **Out of scope** — what this task must not touch.
   - Add a footer line: `Categories: <comma-separated categories>`.
3. Write the draft to `tmp/triage-issue.md` (git-ignored) so the user can edit it directly.
4. **Validate the draft with the rubber-duck agent — always, no exceptions.** Before showing the draft to the user, launch the **rubber-duck** agent (via the Task tool) and have it review `tmp/triage-issue.md`. This step is mandatory for every triage. The rubber-duck prompt must include:
   - The full drafted issue (or instruct it to read `tmp/triage-issue.md`).
   - The subagent's investigation report (`surfaces`, `affected`, `searchStrategy`, `codeSnippet`, `rootCause`, `approach`, `testing`) so it can check the draft against the actual findings.
   - The instruction to catch: incorrect or unverified root cause, `file:line` references that don't match reality, **a frozen file/line inventory or occurrence count where a discovery recipe belongs** (features/refactors/renames must anchor to patterns and stable identifiers, not coordinates that will go stale), an approach that violates a qvim invariant (paint-path purity, per-frame redraw cost, no `msgpack::object` copies, render-thread affinity, well-bracketed nvim keycodes, 4-element `hl_attr_define`), acceptance criteria that aren't concretely verifiable, and testing requirements that use soft assertions (`qWarning`/pixel-band) instead of hard `QVERIFY`/`QCOMPARE`, target the wrong test tier, or fail to pin *this specific* fix.
   - The instruction to report only substantive issues (bugs, logic/design flaws, missing coverage) — not style/wording nits.

   Act on the rubber-duck's findings: revise `tmp/triage-issue.md` (and re-run the investigation subagent if it surfaced a factual gap) until the substantive findings are resolved. Only then proceed to present the draft.
5. Show a summary in chat including the proposed **title**, **categories/labels**, and any **related issues** being linked, and note that the rubber-duck review passed.
6. **Always open the rendered draft with mdview before asking the user to accept.** Launch the viewer on the draft so the user reviews the formatted markdown, not the raw file:

   ```pwsh
   D:\mdview\target\release\mdview.exe tmp/triage-issue.md
   ```

7. **Ask the user to accept, edit, or reject** using the `ask_user` tool (accept / edit / reject). If they choose "edit", let them edit `tmp/triage-issue.md` (and/or adjust categories) and wait for confirmation, then re-read the file **and reopen it with mdview** (same command as above) so the user reviews the modified draft before being asked again. Repeat the reopen-on-modification loop until the user accepts or rejects.

### 6. Create the GitHub issue — only if accepted

**Only run this step if the user accepted in step 5.** If rejected, delete `tmp/triage-issue.md` and stop.

Use the bundled helper script — it derives the repo root, ensures each label exists (creating any that are missing), extracts the title from the first `# ` heading, writes the body to a git-ignored temp file, creates the issue assigned to `@me`, and prints the title and URL. GitHub auto-links any `Related to #N` / `Blocked by #N` cross-references in the body at creation time, so no extra linking call is needed:

```ps1
pwsh -NoProfile -ExecutionPolicy Bypass `
  -File .github/skills/triage/scripts/new-issue.ps1 `
  -Draft tmp/triage-issue.md -Label <cat1> -Label <cat2>
```

Pass one `-Label` per chosen category (including `spike` when applicable). Then report the created issue URL to the user, and confirm the related-issue cross-references resolved.

### 7. Cleanup

1. Delete `tmp/triage-issue.md` and any temp body file.
2. Show a short summary: issue URL, title, assigned categories/labels, and any linked related issues.
