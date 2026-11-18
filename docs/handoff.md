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
- Qt test runner: 18 passed, including setup/cleanup and real QML interaction.
- Python CLI integration suite: 7 tests passed.
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

## Windows Claude hooks

Claude hooks now use exec form (`command` plus an argument array) on every
platform. Native Windows paths are passed verbatim without Bash or PowerShell.
Reconnect migrates the previous shell form and updates marked Lore hooks when
the executable moves, while preserving unrelated settings and callbacks.

Ancestor traversal terminates at drive/UNC roots. Canonical project containment
uses Windows path separators and case-insensitive comparisons on Windows.
Tests cover drive roots, UNC paths, project boundaries, Unicode and special
characters in executable and project paths, and original-record deduplication.

PowerShell scripts build, test, and package a Windows executable with Qt runtime
dependencies. `.github/workflows/windows.yml` runs the native Qt and CLI suites,
then tests a packaged executable with Qt SDK paths removed. The provider fixtures
are native executables and the MCP pipe test is portable across both platforms.
Windows builds enable console output so headless hooks can return JSON.

Live authentication and actual Windows Claude/Codex CLI distributions still
require validation on a Windows machine. In particular, learning must account
for `.cmd`/`.bat` shims if the provider is installed through npm; QProcess cannot
launch those as native executables. Windows archives inherit directory ACLs;
Unix owner-only permission bits do not configure Windows ACLs.

WSL2 is a possible interim route, but has not been tested. Lore and the coding
agents should share the same Linux environment for that workflow.

## Other current limits

Search is keyword-based. Semantic embeddings, fuzzy search, remote sync,
encryption, and email digests are not implemented. Only data exposed in saved
JSONL logs can be captured. Saved transcript formats may change across CLI
versions. Generated claims still require checking against current code.
