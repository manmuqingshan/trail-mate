<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **trail-mate** (43886 symbols, 82034 relationships, 300 execution flows). Use the GitNexus MCP tools to understand code, assess impact, and navigate safely.

> Index stale? Run `node .gitnexus/run.cjs analyze` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? `npx gitnexus analyze` (npm 11 crash → `npm i -g gitnexus`; #1939).

## Always Do

- **MUST run impact analysis before editing any symbol.** Before modifying a function, class, or method, run `impact({target: "symbolName", direction: "upstream"})` and report the blast radius (direct callers, affected processes, risk level) to the user.
- **MUST run `detect_changes()` before committing** to verify your changes only affect expected symbols and execution flows. For regression review, compare against the default branch: `detect_changes({scope: "compare", base_ref: "main"})`.
- **MUST warn the user** if impact analysis returns HIGH or CRITICAL risk before proceeding with edits.
- When exploring unfamiliar code, use `query({query: "concept"})` to find execution flows instead of grepping. It returns process-grouped results ranked by relevance.
- When you need full context on a specific symbol — callers, callees, which execution flows it participates in — use `context({name: "symbolName"})`.

## Never Do

- NEVER edit a function, class, or method without first running `impact` on it.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit changes without running `detect_changes()` to check affected scope.

## Resources

| Resource | Use for |
|----------|---------|
| `gitnexus://repo/trail-mate/context` | Codebase overview, check index freshness |
| `gitnexus://repo/trail-mate/clusters` | All functional areas |
| `gitnexus://repo/trail-mate/processes` | All execution flows |
| `gitnexus://repo/trail-mate/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
|------|---------------------|
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->

# Trail Mate Agent Rules

## PlatformIO Builds, Uploads, And Monitors

- When a PlatformIO build/upload is started, let it run until it explicitly completes with success or failure. Do not impose an arbitrary time limit such as 120s or 300s.
- Never treat a tool-call timeout as the build result. The only valid build result is the PlatformIO process exit code and its final log output.
- Never use a foreground `shell_command` call as the control mechanism for long PlatformIO builds. The shell tool timeout must not become a build timeout.
- For any long build, start it as a hidden background process, redirect stdout/stderr to a log file, record the PID and log path, and poll with short commands until the process exits.
- Run PlatformIO build, upload, or monitor when it is part of the requested verification, and make sure each started process reaches a definite terminal state.
- Before starting a PlatformIO build/upload/monitor, check for existing processes for the same repository and environment. Do not start a duplicate build.
- If a tool call times out, is interrupted, or ends unexpectedly while a build/upload is running, immediately check the recorded PID and any orphaned `pio`, compiler, linker, and `.pio/build/<env>` processes before doing anything else.
- Stop only clearly matching stale processes for this repository/environment. Never use broad process kills.
- Keep serial monitoring short and bounded unless the user explicitly asks for a longer capture.
- Prefer CI/release-time artifacts for routine build outputs. Do not rebuild locally on every small change.

## PowerShell Command Discipline

- Keep PowerShell commands short and single-purpose. Avoid dense one-liners with complex quoting, nested shells, or long chained pipelines.
- Prefer native PowerShell cmdlets end to end. Do not mix PowerShell enumeration with `cmd /c` for file or process operations.
- Use `rg`/`rg --files` for search, `git` for git state, and `apply_patch` for manual file edits.
- For repeated or complex local operations, write down the intended steps first and run them as small observable commands instead of one large opaque command.
- After any failed or timed-out command, inspect the process state before retrying.
