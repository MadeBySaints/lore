# Development handoff

## Goal

Lore gives a specific software project a persistent memory across Claude Code
and Codex conversations. Capture the saved original records, build a searchable
index and cited wiki, and keep a small `BRAIN.md` that future sessions read first
to discover context and next steps.

The app is implemented in C++ and Qt Quick. See `references.md` for the
official integration documentation.

## Implemented as of November 18, 2026

- Native Linux desktop UI and headless CLI in a single Qt executable.
- Per-project SQLite database with preserved JSONL records, FTS5 search,
  duplicate detection, project filtering, and unchanged-file scan caching.
- Codex `session_meta` / `response_item` and Claude Code message import.
- Local Claude lifecycle hooks plus polling capture for either provider.
- Managed entry pointers in AGENTS.md and CLAUDE.md, preserving existing text
  and unrelated Claude hooks.
- Model-assisted learning through an installed, authenticated Codex or Claude
  CLI, with validated citations and linked supersession of outdated entries.
- Optional automatic learning after an idle period; disabled by default.
- Read-only MCP stdio tools: `brain_context`, `brain_search`, `brain_wiki`,
  `brain_sessions`, and paginated `brain_session`.
- English/Danish UI and live Omarchy accent, rounding, and language integration.
- Arch package definition and a user-local installer that does not need sudo.

## Validation completed

- Linux build succeeded with Qt 6.12, qmake6, and GCC 16.
- Qt test runner: 14 passed, including setup/cleanup and real QML interaction.
- Python CLI integration suite: 6 tests passed.
- Live learning passes through codex-cli 0.170.0 and Claude Code 2.1.326 both
  created cited memory using fictional project text.
- Arch package built; desktop entry validated; installed GUI opened on Omarchy.
- English/Danish previews were inspected. The README screenshot contains only
  fictional project data.

An early installed `lore --version` crashed because QGuiApplication attempted
display initialization before argument handling. The current startup switches
help/version and other CLI commands to offscreen mode before constructing Qt.
Installed help and version were verified with display variables unset and an
invalid display driver configured; the regression is in `tests/integration.py`.

## Continuing on another machine

Clone the repository and read this file plus `AGENTS.md`. Install the Qt 6 base,
declarative, Quick Controls, SQLite driver, and platform dependencies for that
machine, then follow the build and validation commands in the README.

The repository contains the app source and a fictional preview, not any user's
project database. Existing personal project memory lives in that project's
private `.lore/` directory. See the README for backup instructions if that memory
also needs to move. This repository has no model credentials; authenticate
Codex or Claude separately on the new machine.

## Windows support discussed, not implemented

The current binary, installers, and tests target Linux. Qt makes a native port
possible, but the following work remains:

1. Add a Windows build/deployment path that bundles the necessary Qt modules,
   QML plugins, SQLite driver, and Windows platform plugin.
2. Audit filesystem logic. In particular, `Store::write` stops its ancestor
   walk at `/`; Windows drive roots need a platform-independent termination
   condition. Review path comparisons, case sensitivity, UNC paths, permission
   handling, and symlink/reparse-point behavior.
3. Adapt project hook command quoting and shell selection. Current hook
   commands use POSIX single-quote escaping.
4. Verify executable discovery and launching for native Claude/Codex installs,
   including `.exe` and command-wrapper variants, MCP stdio, and headless Qt.
5. Port shell-based helpers and Unix-specific test fixtures, add Windows CI,
   then exercise the full capture/learn/resume cycle on Windows.

WSL2 is a possible interim route, but has not been tested. Lore and the coding
agents should share the same Linux environment for that workflow.

## Other current limits

Search is keyword-based. Semantic embeddings, fuzzy search, remote sync,
encryption, and email digests are not implemented. Only data exposed in saved
JSONL logs can be captured. Saved transcript formats may change across CLI
versions. Generated claims still require checking against current code.
