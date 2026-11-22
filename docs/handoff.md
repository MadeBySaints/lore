# Development handoff

## Goal

Lore gives a specific software project a persistent memory across Claude Code
and Codex conversations. Capture the saved original records, build a searchable
index and cited wiki, and keep a small `BRAIN.md` that future sessions read first
to discover context and next steps.

The app is implemented in C++ and Qt Quick. See `references.md` for the
official integration documentation.

## Current development direction

Lore focuses on the local coding memory loop. The application keeps project
capture, plain-text import, cited wiki learning, keyword recall, and managed
agent pointers. Claude Code starts a cleared session with a bounded project
handoff while learning proceeds outside the hook. New capture skips tool-only
records and preserves user and assistant prose. The upgrade rebuilds keyword
search from conversation text while keeping original archived records and IDs.

BoardHydra networking, credentials, board commands, roadmap and card work were
removed from the application. Legacy database records remain available for
archive integrity. The remaining sections describe earlier releases.

## Resume and archive improvements

- Resume uses the same cited session handoff as the CLI and read-only MCP.
  Users can select a conversation and save task, change, check, blocker, and
  next-action notes. Assistant reports and user-prepared notes keep their
  provenance.
- Claude clear recovery can also run on the next prompt. Explicit clear
  checkpoints are consumed once, and multiple pending checkpoints in the
  same transcript directory are treated as ambiguous. Reconnect to register
  the new PostCompact hook.
- The CLI has `doctor`, a verified SQLite `backup`, and `restore` into a
  separate archive. Migration keeps the original when reconnect fails.
- The desktop opens exact citations and supports an explicit All projects
  search. Conversations can import the active branch from ChatGPT exports.

## Historical implementation as of November 18, 2026

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
- Qt test runner: 29 passed, including setup/cleanup and real QML interaction.
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

Notification toasts overlay the top of the window. They expire after 12 seconds
or clear on success. A persisted toggle hides them, and Recent issues retains
app-session diagnostics. Contended
Markdown exports defer safely and retry using generation counters. Existing
databases skip repeated schema creation, reducing writer contention.

The retrieval evaluation and HTTP embedding fixture use fictional data. Real
embedding quality must be measured separately with `--semantic`; no embedding
model is silently downloaded during tests. Remote sync, encryption, and email
digests are not implemented. Only saved transcript records can be captured.
Generated claims still require checking against current code.

## Concurrent capture (unreleased)

Transcripts are read incrementally. `ingest_offsets` (schema v5, keyed by path
and provider) stores the byte offset, line count, project ownership, and a hash
of the first line. A changed first line or a shorter file triggers a full,
deduplicated re-read; only newline-terminated lines advance the offset. Files
are processed in 1,000-line batches, each committing its offset. Transcripts
from other projects are skipped after the first look. `sync` reads the scan
cache once and writes it in one transaction. `UserPromptSubmit` hooks no longer
render Markdown; other hooks render only when `exportsStale()`.

In WAL mode a connection holding an open read cannot start writing after
another connection commits; SQLite returns "locked" without waiting. Finish
every SELECT before writing on the same connection. The concurrency test runs
six sessions plus a background poller against one project.

Settings → Migrate and `lore migrate` move an in-project archive to a separate
working directory using `Store::copyTo` and `Store::removeArchive`.

## Verify and separate working directories (unreleased)

Each wiki card has a Verify button. `Store::verifyPrompt` ranks indexed file
excerpts only (linked files first) and `saveVerification` reuses Ask's
citation checks (`checkedCitations`), storing the verdict in the additive
`memory_verification` table (schema v4). Verdicts never change review state.

`Store::root()` is Lore's working directory; `Store::source()` is the project,
recorded in `project_settings` as `source` when they differ. Indexing,
transcript ownership, citation file checks, hooks, and AGENTS.md/CLAUDE.md
pointers use the source; everything else uses the working directory. The GUI
defaults to `Documents/Lore-<project>` (`LORE_WORKSPACE_ROOT` overrides the
parent, which the tests use). `lore init --project W --source S` does the same
from the CLI.

## Wiki responsiveness (unreleased)

Memory review (Confirm, Correct, Outdated, Forget) runs in the worker thread
and queues clicks in order. `Store::memories()` loads with a fixed handful of
set-based queries instead of several per memory and citation. `render()`
keeps export stamps in the additive `rendered_outputs` table (schema v3) and
skips rebuilding unchanged conversations and file snapshots; pages whose bytes
are unchanged are not rewritten. Backend list properties notify separately
(`sessionsChanged`, `memoriesChanged`, `filesChanged`) and only when their
contents differ, so capture ticks and preference saves no longer rebuild the
wiki list. Memory card dialogs load on demand. `MemoryModel` updates wiki rows
in place so a review keeps scroll position.

## Obsidian graph (unreleased)

`Store::graph` writes `Lore/` beside `.lore/` while `Store::obsidianVault`
finds a `.obsidian` folder in the working directory or a parent. It holds a hub
note, one note per non-excluded wiki entry (named by title; older duplicates get
` (id)`), one per file linked from those entries or their code checks, and one
per conversation. Content hashes live in `rendered_outputs` under `graph:` keys;
notes Lore stops producing are deleted only if their hash still matches.
`removeGraph` runs from `removeArchive` and removes only notes whose frontmatter
starts with `lore-generated: true`. Covered by
`obsidianGraphLinksEntriesFilesAndConversations`.

## Local model provider (unreleased)

Provider `local` sends prompts over loopback HTTP instead of a CLI process.
`Toolbox` probes `/api/tags` (Ollama) then `/v1/models`, hides embedding models,
and keeps `local.url` / `local.model` in the tool overrides preference
(`LORE_LLM_URL` / `LORE_LLM_MODEL` are fallbacks; `Toolbox::loopback` rejects
non-loopback hosts). `Backend::startLocalModel` uses Ollama `/api/chat` with
`num_ctx` sized to the prompt and the memory JSON schema as `format`, otherwise
`/v1/chat/completions` with `response_format: json_schema`. Both finish in
`Backend::finishModel`, which strips `<think>` blocks, fences, and prose around
the JSON. `learningBatch(true)` uses small budgets; local Build wiki chains rounds
until nothing is pending. Ask & Verify use `answerProvider` (per project,
defaulting to the old single provider). Covered by
`localModelBuildsWikiWithoutAPlan` and `connectionsChooseALocalModelForBuildingTheWiki`.
A real run on gpt-oss:20b took 16 to 56 seconds per round. Lore's own SessionStart
hook text is captured as a user message and wastes local rounds; filtering it is
a possible follow-up.

## Project browser (unreleased)

Page 10 (`ProjectsPage.qml`, Ctrl+O, **All projects…**) lists
`Backend::knownProjects`: the per-project preference records keyed by working
directory, which now also store `source`, `name`, `lastOpened`, and counts.
`scanProjects` runs in a worker: it opens each `Store::workspaceRoot()/Lore-*`
memory and reads `cwd` from the newest Claude transcript per project folder and
the newest 300 Codex rollouts. Folders without a memory become suggestions.
`forgetProject` hides an entry (`hiddenProjects`) without touching files; opening
the project again unhides it. Covered by
`projectBrowserRemembersFoldersAndFindsProjects` and
`projectsPageOpensARememberedProject`.

`/clear` investigation (November 18, 2026): Claude desktop-app sessions
(`entrypoint: claude-desktop-3p`) in a connected project fire SessionStart and
UserPromptSubmit hooks and were fully captured. A cleared conversation was in a
project that had never been connected; its transcript was still on disk.
`test_clear_captures_the_conversation_before_it_resets` and
`test_connecting_later_recovers_earlier_sessions` cover both paths. Unverified:
sessions the desktop app runs in `.claude/worktrees/<name>` may not load the main
folder's gitignored `.claude/settings.local.json`; the GUI watcher still
captures them because their cwd is inside the source folder.

On `SessionEnd` with reason `clear`, Lore saves the old transcript and records
the capture in recent activity. Export work waits for the next session or sync.
`SessionStart` with source `clear` also retries saved transcripts alongside the
new transcript before building the briefing. This covers a final record that
finishes writing after the ending hook and works before the new transcript
exists. It uses the same project filtering and deduplication as normal sync.
The CLI regression invokes the configured hook executable with both payloads.

## Automatic wiki builds (unreleased)

Per-project options `autoProvider`, `autoAfter` (messages waiting, 0 = off),
`autoEvery` (hours, 0 = off), and `lastAutoBuild` sit beside `autoLearning`.
Projects saved before these existed migrate to `autoAfter: 1`, the old "any new
message" behaviour. `Backend::automationDue` evaluates the triggers;
`runAutomation` runs from `m_auto` (quiet delay after capture,
`LORE_AUTO_QUIET_MS`) and `m_schedule` (`LORE_AUTO_CHECK_MS`, default one
minute). `m_autoRun` keeps a triggered build draining until nothing is pending,
and `lastAutoBuild` records when it started. Covered by
`automationBuildsTheWikiOnItsOwnTriggersAndModel` and
`connectionsConfigureAutomaticBuilds`. Builds only run inside the GUI process.

`project_settings.obsidian` (`on`/`off`, unset = follow vault detection) backs
the Obsidian switch in Settings (now **Write notes beside BRAIN.md**, shown when no vault is attached). `Store::setObsidianOutput(false)`
runs `removeGraph` and drops the `graph:` stamps. Covered by
`obsidianOutputSwitchWritesAndRemovesNotes`.

## Vault, board, and roadmap (unreleased)

`Store::obsidianVaults` reads Obsidian's `obsidian.json` (`LORE_OBSIDIAN_CONFIG`
overrides it) and `vaultInfo` reports the BoardHydra plugin (`.obsidian/plugins/
boardhydra`, enabled in `community-plugins.json`) and its boards: `root` +
`folders` from the plugin's `data.json` (never its key) plus a shallow scan for
`Lists` folders. `project_settings.vault` attaches a vault; `graphFolder()` is
then `<vault>/Lore` (or `<vault>/Lore <project>` when another project's marker, which names its `project:` hash, owns `Lore`) instead of `<root>/Lore`; `render` moves notes earlier builds nested in `Lore/<project>`. `setVault` removes
the old copy and the `graph:` stamps. `removeGraph` now takes the graph folder.

The per-memory card writer is replaced by a roadmap (schema v6:
`roadmap_cards`, `roadmap_items`). `roadmapPrompt` sends open next/question
memories, recent decisions/facts, board lists/labels, and current cards;
`saveRoadmap` validates (lists, labels, memory ids, caps; uncovered open work
gets `m<id>` cards), keeps keys, retires dropped cards, stores label/list
suggestions, and renders twice (the second pass, `m_noNewCards`, links memory
notes to cards without adding cards). `boardCards` writes Lore-owned notes
whole; for notes with `bh_id` it only appends unseen items, ticks items whose
memories ended, adds labels once, and sets done once, tracked in
`roadmap_items`. `Backend::planRoadmap` runs it with the roadmap model (`roadmapProvider`);
`planRoadmapIfDue` after builds and board choice; `lore roadmap` from the CLI.
BRAIN.md gains a board section. Plugin facts (boardhydra-obsidian 0.3.9, read
November 19, 2026): new notes without `bh_id` become cards; unknown lists and
labels are dropped with a warning (`noteToPatch`), and `pushIndex` never creates
lists; checklists take one level of sub-items; the guide refreshes every 6
hours, graph colour groups on every label change. A real Claude plan of this
project's 57 open items produced 9 goal cards with sub-steps. Covered by
`attachedVaultReceivesNotesAndListsBoards`, `roadmapCardsFollowThePluginFormat`,
and `settingsAttachVaultAndChooseBoard`. Known race: Lore refreshes a waiting
note only if it still has no `bh_id` when re-read, but the plugin could add one
between that read and the write.

## Builds, progress, and concurrency (unreleased)

`Backend::learn` starts a manual build; `startRound` runs one round;
`continueBuild` waits for `m_work` and the model, then starts the next round
with the build's model (`m_manualProvider`, or `m_autoProvider` while
`m_autoRun`). A failed round is retried once (`m_retried`). `buildProgress`
exposes done/pending/rounds for the bar in Main.qml. Gating is split:
`modelBusy` (one model call at a time; `startModel` refuses a second) and
`workBusy` (worker); capture, reviews, and worker tasks no longer wait for a
model call. Cancel sets `m_autoSnooze` (30 minutes) instead of disabling
automation. Covered by `buildShowsProgressAndLeavesTheRestUsable`.

## Learning from what was said (unreleased)

`spoken()` reduces a record to the user's words and the assistant's prose:
Claude `text` blocks or string content, Codex `message` items. Tool calls and
output, reasoning, `isMeta`/compact summaries, and wrapped hook/command text
(`<system-reminder>`, `<local-command-caveat>`, `<command-name>` …) are dropped.
`learningBatch` (now non-const) marks records with nothing said as learned
without sending them, in order, and sends spoken text only. On this project
2,106 of 2,510 learned records were tool-only. Conversation titles use the first
spoken user text, and `sync` renames sources titled after a banner. The graph
leaves out superseded entries. Covered by
`learningSendsWhatWasSaidNotToolOutput`.

## The board is the graph (unreleased)

With a valid `boardFolder`, `graph()` writes no memory notes and removes the
ones it wrote (`removeGraph`, `graph:` stamps dropped), so the vault graph is the
plugin's tree: board > list > card > checklist > item (see the plugin's own
board/list/checklist/checkitem notes). Card notes write `list:` as a link to the
list's note, as the plugin does, drop the `lore:` memory links, and a finished
card moves to a Done/Complete/Shipped list once. `saveRoadmap` renders once.
The planning prompt describes the tree. The user chose this over memory leaves
on cards (November 19, 2026).

The roadmap has its own model (`roadmapProvider` per project, default the Ask & Verify choice; selector beside Plan roadmap; `lore roadmap --provider`). Local roadmap calls use a JSON schema for the card tree and a 4-hour timeout.


## Verify all (unreleased)

`verifyPrompt` also offers newer memories as evidence: current facts/decisions recorded after the memory (`time` later), at least two shared terms, up to six, confirmed/corrected boosted. `saveVerification` accepts `memory:` citations. `Store::reviewQueue` lists unreviewed current memories without a current check, oldest first. `Backend::verifyAll` queues them; `continueVerify` runs one at a time with `verifyProvider` (per project, defaults to Ask's model), counts failures and skips them, and Cancel/Stop clears the queue. `reviewWaiting` and `verifyProgress` drive the wiki-page row. Ollama calls set `num_predict: -1` and give roadmap answers 24k tokens of room (a cut-off answer was the first local failure). A gpt-oss:20b roadmap of 57 open items took about 2 minutes. Covered by `verifyChecksAgainstNewerFactsAndVerifyAllWorksThroughTheQueue`.


Idle verification: `autoVerify`, `autoVerifyProvider`, `autoVerifyAfter` (entries awaiting review), `autoVerifyEvery` (hours), `lastAutoVerify` per project. `runAutomation` (shared `m_schedule`, see `updateSchedule`) starts `verifyAll` with `m_verifyAuto` when `automationDue()` is false and `autoVerifyDue()`; `continueVerify` drops an automatic run's remaining queue when a build becomes due (`automationDue`/manual build) so the build starts; Stop sets `m_verifySnooze` (30 minutes). `verifyModel()` picks the automatic or Verify model. Covered by `idleVerificationRunsOnItsOwnTriggersAndModel` and `connectionsConfigureAutomaticBuilds`.


## Confidence and the user's reviews (unreleased)

Builds and Verify return `confidence` (0-100), stored in `memory_confidence` (schema v7, source `build`/`check`). `project_settings.autoReview` (0 = off) is the threshold: `settleByModel` records `accepted` (new review state) or `outdated` with a note starting `Model:` (`byModel`), only over no review or an earlier model decision; `setAutoReview` re-applies stored confidences; unclear verdicts never settle. `reopen` deletes a review (not forgotten/corrected). `stats.calibration` compares build/check confidence with the user's own decisions (confirmed agrees; outdated/forgotten/user correction disagrees) at 50..100%. `learningBatch` adds the user's kept, rejected (user-outdated), and corrected entries as lessons; model decisions and forgotten entries are left out. Covered by `modelSettlesEntriesAboveTheConfidenceThreshold`.


Model output (page 11): `Backend::live`/`liveLines` keep a capped log (300k characters). Claude runs with `--output-format stream-json --verbose --include-partial-messages`; the answer is the last line carrying `result`. Codex logs completed agent messages and reasoning. Ollama is called with `stream: true`; tokens (and `thinking`) are logged and collected into the answer, and a reply sent whole is still read. Other local servers log the final answer.


Idle countdown: `runAutomation` no longer starts work directly; `startCountdown(build|verify)` waits `autoBuildDelay`/`autoVerifyDelay` seconds (per project, 30/60, 0 = at once) on `m_countdown` (1 s), holding at the full delay while `busy()` or capturing and restarting when a capture adds records; a due build replaces a verify countdown. `startAutomatic` does the old start. `countdown` exposes kind/seconds. Tests set the delay to 0 (builds) or 2 s (checks).


Instructions: templates in `src/prompt-templates.json` (ids `t-`/`v-`, purpose build/verify); the user's prompts (`c-`) in preferences `prompts`; per-project `buildPrompt`/`verifyPrompt` ids in project options, with the texts copied into `project_settings` `buildInstructions`/`verifyInstructions` (`applyPrompts`) so workers and the CLI read them. `learningBatch` and `verifyPrompt` append them, labelled as the user's, after the rules and before the data. `InstructionsBox.qml` is the dropdown and editor. Covered by `instructionsTravelWithBuildsAndChecks`.


Token use: `recordUsage` reads the finished call's own report (Claude `result.usage` + `modelUsage.contextWindow`, Codex `turn.completed.usage`, Ollama `prompt_eval_count`/`eval_count` with our `num_ctx` as the window, OpenAI `usage`), keeps session and app-wide totals per agent (preferences `usage`), and logs a tokens line. Quick commands: `src/quick-commands.json` plus the user's (`u-`, preferences `quickCommands`); `runQuickCommand` calls `Toolbox::startSessionWith` (agent CLI with the prompt as its first argument, in a terminal in the source folder).


Semantic search: the EmbeddingGemma download streams `/api/pull` (progress in bytes, two-minute no-data timeout instead of a 15-minute cap); indexing counts what needs embedding first and reports done/total, and turns search on after the first saved batch. `Store::Progress` (atomic stage/done/total) is shared with the worker and polled every 500 ms into `semanticProgress` and the status bar. The Model output page appends `liveAppended` batches (100 ms) to a TextEdit in a Flickable and follows only when scrolled to the bottom. Version 0.4.0 (main.cpp, windows.iss, PKGBUILD, README, installer test) is shown on the help page.


Unattended local runs: Ollama options `num_predict` (4k checks, 8k builds, 20k roadmap), `repeat_penalty` 1.15, `think: low` for gpt-oss; OpenAI-style `max_tokens` and `frequency_penalty`. `liveLines` keeps the last 8k characters and stops the reply when a 150-character tail appears four times; the call restarts with the same prompt plus a start-fresh note, temperature +0.25 per attempt, up to 3 times (`m_loopRetries`). In a local build a round failing twice halves the batch (`learningBatch(small, limit)`) and a single failing message is skipped (`Store::skipMessages`, logged). Covered by `localModelStuckInALoopIsStopped` and `buildShowsProgressAndLeavesTheRestUsable`.

## Lore 0.5.0: navigation, Automations, setup guide, board read-back, speed

Navigation. `Main.qml` is a shell: a grouped sidebar (`NavItem`, objectName `nav_<id>`) and pages addressed by id (`win.page`: home, wiki, ask, sources, board, automations, output, diagnostics, settings, projects, setup). Older numbers (`--page 4`, `setProperty("page", 9)`) map through `win.legacy`. Pages are `Loader`s that load on first visit and stay (`win.visited`). Merged pages: `AskSearchPage` (search as you type, Ask, Preview context; replaces AskPage, search, ContextPage's preview), `SourcesPage` (conversations + `ProjectPage`), `DiagnosticsPage` (hook deliveries, recent issues, notifications), `BoardPage` (one `BoardSetup`, plus the board feed). Settings has tabs (Folders, AI & models with the `ToolRow`s, Semantic search, About). Shared components: `Card`, `Caption`, `Body`, `Line`, `PrimaryButton`, `Stat`, `ToolRow`; the palette is on `Theme` (`theme.ink`, `theme.card`, ...). Icons are SVGs in `src/icons`, tinted with `IconImage` (needs the Qt SVG image plugin; PKGBUILD depends on qt6-svg).

Automations page. Schedule rows (capture, builds, checks, model reviews, roadmap, board changes, After Action) with Pause all (`pauseAutomations`/`resumeAutomations`, `m_pauseAll`, session only), then the controls that were in Connections, the review threshold and calibration (moved from the wiki page), `autoRoadmap` (prefs, default on; gates `planRoadmapIfDue` except right after choosing a board), `boardLearning` (project_settings `boardLearning=off`), and `AfterActionPage` as a card.

Setup guide. `SetupWizard.qml` covers the window while no project is connected (`win.setup`; the page becomes "setup" so connecting moves on instead of closing it) and when opened from Settings or Help. Steps: welcome, project (`Backend::folderFacts`), files, ai, connect (`Backend::connectionPreview` lists every file Connect creates or changes), optional obsidian and board (`BoardSetup part: vault|board`, plus a checklist from `vaultState.vault`: pluginVersion, signedIn, syncFolder), an automation preset, first build, summary. `resume()` picks the first step not done. Covered by `sidebarOpensEveryPageAndTheGuideWalksThrough`.

Board read-back. `Store::readBoard` runs in `sync()` before the export renders (so a step unticked on the board is not ticked back). A signature of card, `_archive/`, and `Notes/` note names, sizes, and times skips unchanged boards. Schema v10: `board_cards` (bh_id, list, done, archived, items JSON, lore_id, and `lore_bh`/`lore_hash`/`lore_pending` for Lore's own edits) and `board_comments`. The first look is a baseline. Changes become events of a `board` source (role user, raw shaped like a Claude user message so `spoken()` reads it); ticked or done mapped items settle their next-step memories (`memory_review` outdated, note `Board: ...`; unticked reopens; user reviews untouched); `boardChanges` in the sync result makes `planRoadmapIfDue` run. `boardCards` records each write to an on-board card with `rememberCard` (new baseline, pending), and `cardEditLost` re-applies when the note has the same `bh_updated` and exactly the text Lore's edit replaced (the plugin restored the server copy on start). `vaultInfo` hides folders the plugin no longer syncs (`data.json` `boards` vs `folders`) and reports `pluginVersion`, `signedIn` (whether `key` is set; never its value), `syncFolder`, `staleBoards`. Covered by `boardChangesBecomeMemory` and `staleBoardsAreHiddenAndPluginStateIsReported`.

Performance (measured on a copy of this project's archive: 9,600 records, 991 entries, 20,600 embeddings).
- Prompt hook (`briefing`): 3.7 to 8.2 s down to 0.4 s with the same evidence. Conversation excerpts are built only for full-text hits (`events_fts`, prefix terms, top 400) and the closest semantic matches, not the whole archive; exact hits map to excerpts through a hash. Embeddings are normalized float32 blobs (`embeddings.data`, schema v9); older JSON vectors still rank and are converted by the next semantic update. `evidenceHashes` rebuilds only cited excerpts for citation checks and the restored answer.
- Minute reindex inside capture: 630 ms down to 95 ms. Files with unchanged size and time (ms precision) are not read; only changed rows are written; links are backfilled only for new paths; render only on change.
- Capture ticks: the worker compares `Store::fingerprint()` and returns `Store::snapshot()` only when something changed; worker jobs return their snapshot in the same job. The GUI thread no longer reloads the project every 5 s.
- Signals: `changed` now covers only the project's identity; `statsChanged`, `brainChanged`, `activityChanged`, `answerChanged`, `countdownChanged`, `progressChanged`, `messageChanged`, `usageChanged`, `vaultStateChanged` fire only on a difference. The countdown and progress poll use `quietMessage`. `nextSteps` is computed in C++.
- `render()`: wiki pages skipped by stamp, sessions and board info read once, no quadratic loops in `graph()`, managed blocks written only when changed. Steady state about 120 ms on this archive (first render 1.8 s, was 5.3 s). Opening a project renders only when exports are stale.
- Indexes `events(source,seq)`, partial `events(seq) WHERE learned=0 AND role IN ('user','assistant')`, `memory_conflicts(b)`; `synchronous=NORMAL`.
- Vault and board detection runs in a worker (`checkVaults`, `vaultState`) instead of in QML bindings.
- Not done: model results (learn, verification, roadmap) are still saved on the GUI thread after the call (about 150 to 250 ms on this archive).

## Lore 0.6.0: After Action without a schedule, and card work

No interval. `minutes`/`next` are gone from the After Action config (old values are ignored and dropped on save) and so is `advanceAfterActionSchedule`. The shared `m_schedule` timer runs while After Action is enabled with a prompt. `runAfterActionIfDue` starts a run when Lore is idle, not paused, `afterActionBlocker()` is empty (no pending messages, no build, no verification queue, `reviewQueue` empty; that queue already leaves out entries the model has checked, so entries only the user can settle do not block), and `m_fingerprint != m_afterSeen`. `m_afterSeen` is set when a run starts and again when it finishes (after its own index refresh), so a run does not trigger itself; a run that stopped for a blocker or a wiki change clears it, so it retries as soon as the wiki is ready. Automatic runs with nothing new finish quietly (`finishAfterAction(..., quiet)`). With a board folder the fingerprint moves each minute, so expect at most one quiet scan a minute then.

Card work. The opt-in queue can run after a report or independently while
wiki work is pending. A saved project brief plans an initial set of ordered
cards with acceptance criteria. Lore stores the brief revision with its
board path in `.lore/after-action.json`. Planning keeps the original brief in
each assignment. Existing roadmap cards are eligible too.

`Store::workableCards` picks synced open cards, Doing first, and selects the
first unchecked checklist item (children before their parent). A result ticks
only that item; a pass over the whole card then verifies acceptance. Verified
or unnecessary cards get `done: true` and move to the board's completion list
when available. A blocked card records the reason and gets the Blocked label
when the board has it. Stale results are skipped when the note changed during
the call. The queue rescans after each result and on its idle timer, including
for follow-up notes after the plugin assigns them a `bh_id`. Follow-ups keep
existing label tags and use stable Lore IDs with title deduplication.

BoardHydra handoff uses the Obsidian plugin's Markdown contract. The plugin
turns list links into `idList`, label tags into `idLabels`, checkboxes into
`checklists[].checkItems[].state`, and `done` into `dueComplete`. It owns the
create and patch requests, preserves Lore's frontmatter keys, and supplies
graph color groups for label tags. Lore records its own writes as the board
baseline to avoid replaying them as user feedback.

Card coding uses Claude Code or Codex in the source folder, with a 30-minute
limit per assignment. Local coding uses the Codex runner with the configured
loopback Responses API endpoint. Local planning uses the existing HTTP model
client. Process errors and malformed results leave the card eligible for a
later pass. Verified progress and recent results survive reopening Lore.

Wiki feedback. Current checks with citations are searchable as
`verification:<memory id>` evidence. Recall shows the verdict and provenance
and gives lower weight to claims a current check contradicts. Ask checks the
underlying file hashes before saving a citation to a check. Later wiki builds
receive bounded check summaries alongside user reviews and still require event
citations for every new entry. Stale, superseded and forgotten checks are
excluded from both paths.

The Board roadmap processes up to six unplanned wiki items per model call.
It validates citations against the current batch, keeps existing cards, and
writes new notes to the vault before starting the next call. An omitted item
gets a card directly from its wiki entry. Already planned items serve as
context and do not inflate later prompts. Cancel leaves published notes and
their persisted mappings available to the next run. The status shows written
notes, BoardHydra sync acknowledgments, and remaining unplanned work.

## Lore 0.7.0: BoardHydra through its API, staged card work, rollback

Obsidian is gone. Lore no longer attaches vaults, detects the BoardHydra
plugin, writes card notes, or writes graph notes (`render()` removes the graph
notes older versions wrote, once, by their `lore-generated` marker, and sets
`obsidianCleaned`). The old `vault`, `obsidian`, `boardFolder`, and
`boardSignature` settings stay in `project_settings` but are unused and hidden
from `stats()`.

**Client and key.** `src/boardhydra.{h,cpp}` is a synchronous REST client
(local `QNetworkAccessManager` + `QEventLoop`) for worker threads and the CLI:
bearer key, `X-Client-Id`, JSON content type on writes, no Origin header (the
server refuses cross-origin writes), manual redirects, https only unless the
base is loopback (`LORE_BOARDHYDRA_URL` for tests). Errors are
`BoardHydra::Error{status, body}` with user-facing messages that never contain
the key; a 409 carries the current card, a 429 its `retryAfter`.
`src/secrets.{h,cpp}` keeps the key in Windows Credential Manager (`Lore:...`,
advapi32) or the Secret Service via `secret-tool` (argument array, secret on
stdin); `LORE_SECRET_STORE` substitutes a JSON file in tests, and the test
`main()` sets it so no test touches the real store.

**Board layer** (`src/board.cpp`, Store methods). Schema v11 adds
`board_cards.data` (the card as the server last sent it), `board_links`
(Lore roadmap/work id to card id), `action_log`, `changes`, and `plans`.
`readBoard(force)` reads `GET /boards/{id}` the first time (baseline only, no
events) and `GET /boards/{id}/changes?since=` afterwards, gated by `boardPoll`
minutes, a `.lore/board-refresh` marker, and a 429 backoff. The diff against
`board_cards` is the 0.6 diff (names, list names, item states), so existing
baselines carry over. Lore's own writes go through `patchCard` / `postCard` /
`addCardNote`, which update the baseline and log the action with its undo
(prior field values, archive a created card, delete a note); notes Lore posts
are recorded in `board_comments` so they are not read back. `pushRoadmap()`
replaces `boardCards()`: it runs after `readBoard` in `sync()` and after
`saveRoadmap`, only when the generation moved, and creates cards with
`id = hash(loreId)` so a lost answer never duplicates a card. Nothing in
`render()` touches the network, so hooks stay fast.

**List roles.** `listRoles` (JSON by list id) with `guessListRole` defaults:
plan, code, review, merge, done, skip. A finished card moves to the next list
to its right whose role differs and is not skip, else the done list
(`nextListFor`). `workableCards(processed, roles)` orders by list then card
position.

**Card pipeline** (`src/cardwork.cpp`, Backend). Stages run as worker jobs
(`runBoardJob`, all network and file work) and model calls; `workSoon` retries
a job until the worker is free, because `work()` drops quiet jobs while a
capture runs. Per card: `cardPrepare` (mirror into `.lore/workspace`, review
diff or accepted plan) → model in the workspace (code edits; plan, review, and
improve are read-only: Claude `--allowedTools Read,Glob,Grep`, Codex
`--sandbox read-only`) → `cardCheck` (diff against the baseline, run
`testCommand` in the workspace, stash files under `.lore/changes/<id>`, save
the change) → apply automatically (`applyMode=auto`, outcome changed, checks
passed, confidence ≥ `applyThreshold`) or note the card and wait. Applying
takes a snapshot, refuses files changed since the baseline (`conflict`), logs
`apply` with the snapshot id, then `markWorkedCard`. Processed stamps keep
waiting cards (blocked, pending, planned, stuck) and drop cards that moved on;
`m_seenWork` stops a card that did not move from being picked twice in a run.
Review returns carry the reviewer's notes into the next code prompt; after
three rounds the card waits. The merge job commits only the card's applied
files on `gitBranch` (default `lore/work`), records `gitBase`, optionally
merges with `--no-ff` and pushes; it refuses when the project has changes Lore
did not make (BRAIN.md excepted in same-folder projects). Continuous
improvement runs `improve` when full auto empties the board, adds at most five
cards via `createCards`, and stops for the day when a round finds nothing
(`after-action.json` → `improve.exhausted`) or after `improveCycles` rounds.

**Workspace and snapshots** (`src/workspace.{h,cpp}`). File set: `git ls-files
-co --exclude-standard`, else a walk; `.git`, `.lore`, `node_modules`,
virtualenvs and caches are never the project's own. The mirror copies `.git`
too when it is a folder (not a worktree's `.git` file), keeps ignored files,
and preserves mtimes so the next mirror copies only what changed. Snapshots are
content-addressed (`objects/aa/bb…` plus a manifest per snapshot); a hash is
reused only for files whose size and mtime match and that were last modified
more than two seconds before the previous snapshot started (racy-timestamp
rule). `prune(limit)` drops the oldest manifests and unreferenced objects.
Rollback restores the earliest snapshot at or after the chosen action (after a
safety snapshot), undoes later board actions newest first, marks applied
changes `rolledBack`, and never rewrites Git history.

**CLI.** `lore board status|cards|card|move|tick|untick|comment|create|refresh|
boards|connect|key` (key on stdin). BRAIN.md's board section names these
instead of note editing; `agent-skill.md` and the Update the board quick
command follow.

**UI.** BoardSetup: key (password field), board picker, roadmap with Create for
suggested lists and labels. BoardPage: list roles, open cards with Work this
card, feed. New WorkPage (sidebar `work`): waiting changes and plans, card
work settings (moved from After Action), checks and apply mode, Git, continuous
improvement, action log with rollback, snapshots. The setup guide's Obsidian
step is gone; the BoardHydra step uses BoardSetup.

**Validation.** Qt tests use `BoardFixture`, an in-process HTTP server with the
routes above, and the test binary as the coding agent (it now edits files in
its working directory). Covered: client headers and errors, key storage,
board read-back and own-echo suppression, undo, roadmap creation and additions,
workspace/conflict/snapshot/prune/restore, approval → apply → rollback,
plan-first, full auto through review, commit, and an improvement round (needs
Git), the brief runner, and the QML key and board flow. `tests/integration.py`
drives `lore board` against a Python fake server without a display. Not yet
validated live: a real boardhydra.com key and board, the live event stream
against the real server, and Secret Service on Linux.

## Lore 0.7.1: local models first (Ollama only), Claude Code and Codex as the backup

Local models run only on Ollama or a server with Ollama's API: the
OpenAI-compatible chat route, `response_format`, and LM Studio/llama.cpp/vLLM
support are gone. `Toolbox` keeps every Ollama server it finds (`m_servers`):
the configured address, `LORE_LLM_URLS`, and 11434 unless `LORE_LLM_DISCOVER=0`
(the test `main()` sets it, so tests never reach real servers). Each candidate
gets `/api/tags`; one that does not answer it is not used. `chatServer(model)`
picks the first server with the model; card work's Codex runner uses the same
server's `/v1` (Ollama's Responses API). Before the first scan finishes it
returns the configured address, as 0.6 did. A quiet rescan runs every 3
minutes (`LORE_LLM_RESCAN_MS`).

`local.backup` (auto, claude, codex, none; `LORE_LOCAL_BACKUP` sets the default,
`none` in tests) decides what stands in. `Backend::localReady(task)` is the
local model itself; `modelReady(task, "local")` also counts the backup once
the scan has finished. `startModel` switches to the backup when a finished
scan does not find the model; `startLocalModel` continues once with it when
the server refuses the connection, closes it, or answers 429/503.
`lore models --json` prints the local entry. Live check on the development
machine: Ollama found with gpt-oss:20b and gemma4:26b, and a Lore Ask through
gpt-oss:20b answered in 21 s (its answer cited unknown evidence and was
rejected by the citation check, as designed).
