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
- Qt test runner: 28 passed, including setup/cleanup and real QML interaction.
- Python CLI integration suite: 9 tests passed.
- Live learning passes through codex-cli 0.170.0 and Claude Code 2.1.326 both
  created cited memory using fictional project text.
- Arch package built; desktop entry validated; installed GUI opened on Omarchy.
- Live Ask project through Claude returned a cited answer for fictional code.
- Offline retrieval evaluation: 5/5 questions found expected evidence in the top
  three results; the unknown keyword query returned no evidence.
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
Windows builds enable console output so headless hooks can return JSON. Packaging
explicitly includes qoffscreen.dll, which headless commands need in addition to
the GUI platform plugin selected automatically by windeployqt.

Live authentication and actual Windows Claude/Codex CLI distributions still
require validation on a Windows machine. Standard npm wrappers now resolve to
their known JavaScript entry points and are launched with Node directly; they
are never passed to a shell. A Windows regression test covers this resolution.
Windows archives inherit directory ACLs; Unix owner-only permission bits do not
configure Windows ACLs.

## Guided desktop setup and installation

Version 0.2.0 adds a per-user Windows installer, Start menu shortcut, and a small
native lore-desktop.exe launcher that starts the core app without a console.
The package includes Qt, the offscreen plugin, and app-local Microsoft runtime
DLLs. Inno Setup builds the installer; Windows CI installs it into a path with
spaces/apostrophes, renders the installed welcome screen, checks the shortcut,
and verifies that uninstall leaves project memory intact.

The main interface uses Choose folder, tool detection/sign-in actions, and one
Connect project button. Capture starts on connection, runs in a worker thread,
and the app remembers recent projects and per-project capture/learning choices.
Learning remains opt-in and its quota use is explained in the interface. A
live local sign-in check is required before the GUI enables wiki generation.
Start Claude Code / Start Codex buttons open a terminal in the project folder.

Windows folder selection uses IFileDialog; Linux uses the desktop portal. Tests
inject FilePicker and temporary preferences/scan roots to exercise the real QML
button flow without touching user settings or opening modal file dialogs.
Credentials remain owned by the provider CLIs, not Lore.

WSL2 is a possible interim route, but has not been tested. Lore and the coding
agents should share the same Linux environment for that workflow.

## Other current limits

Version 0.3.0 adds Ask project, a local file/symbol index, reviewed/corrected/
outdated/forgotten memory states, possible-conflict flags, code freshness checks,
task briefings, and a hook context audit. The SQLite migration is additive.
Ask validates citation IDs, inline references, evidence freshness, and file
hashes before saving an answer; these checks do not prove entailment or truth.
The latest valid answer is restored when its project is reopened.

Optional semantic retrieval uses Ollama's loopback `/api/embed` endpoint with
EmbeddingGemma. Connections supplies runtime download and model/index buttons;
the model is separate, opt-in, and about 622 MB. Cached vectors are matched to
document hashes, merged with lexical ranking, and excluded when stale or
forgotten. Cancelled indexing can resume. Keyword fallback is explicit.
After an initial file index, capture checks changes each minute while open.
Symbol extraction is a lightweight declaration scan, not a full language server.

Notification banners expire after 12 seconds or clear on success. A persisted
toggle hides banners; Recent issues retains app-session diagnostics. Contended
Markdown exports defer safely and retry using generation counters. Existing
databases skip repeated schema creation, reducing writer contention.

The retrieval evaluation and HTTP embedding fixture use fictional data. Real
embedding quality must be measured separately with `--semantic`; no embedding
model is silently downloaded during tests. Remote sync, encryption, and email
digests are not implemented. Only saved transcript records can be captured.
Generated claims still require checking against current code.
