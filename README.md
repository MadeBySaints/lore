# Lore

Lore keeps local project memory for coding sessions. It captures useful conversation turns, builds a cited wiki, and gives the next agent a short starting point after context is cleared.

Lore 0.8.1 adds editable session handoffs, read-only MCP tools, archive backup and restore, and cross-project search. Existing Lore archives retain their original records and citations.

## Get started

On Windows, download **Lore-Setup** from [Lore releases](https://github.com/MadeBySaints/lore/releases), or extract the portable ZIP and run `lore-desktop.exe`. Keep the portable files together.

On Linux, build with Qt 6 and a C++17 compiler:

```sh
./bin/build
./build/lore open --project /absolute/path/to/project
```

Use `./bin/install` for the Arch package or `./bin/install-user` for a user-local launcher and binary.

1. Open Lore, choose a project folder, and use **Initialize project**. The setup guide shows the files it creates or updates. Lore defaults to a separate working directory under `Documents/Lore-<project>`.
2. Use Claude Code or Codex in that project. Lore reads their saved local transcripts incrementally. Keep Lore open for polling. Claude Code also has project-local lifecycle hooks.
3. For ChatGPT in the browser or a local chat client without a supported transcript, open **Sources → Save a conversation** and paste the conversation before clearing the chat. Use `User:` and `Assistant:` labels when available. Lore treats unlabeled text as assistant evidence.
4. Choose **Build wiki**. Select Claude Code, Codex, or a local Ollama model in **Settings → AI & models**. Automatic learning is opt-in and uses the selected provider, including its quota when applicable.
   The build and verification preset menus let each project choose a focus, such as architecture, bugs and fixes, or checking file references.
5. Start a fresh session. The managed blocks in `AGENTS.md` and `CLAUDE.md` point to `BRAIN.md`. Use **Resume work** on the Project page to choose a conversation, inspect its evidence, and edit or copy a handoff. Claude Code can recover a handoff after `/clear` or compaction. Task-specific recall brings in more evidence when relevant.

New captures keep conversation text and skip tool-only records, command wrappers, and injected context. Include important verification outcomes and exact blockers in the agent’s final response. The wiki links back to source turns and distinguishes agent claims from user decisions.

## Code and transcript knowledge

- **Index** lists indexed project files and opens their snapshots. **Function index** scans eligible source files with your chosen model and shows scan coverage. The **Function wiki** shows signatures, candidate calls, and source references for dependencies and effects such as config reads, shared-state writes, and file access. Agents can follow the function index link in `BRAIN.md`.
- **Search**, Claude Code prompt recall, and `lore brief --project <path> --query "Where is myFunction called?"` can retrieve relevant function records alongside wiki and file evidence. Search results open the cited turn or file line when it is available.
- **Transcript analysis** processes one conversation or the archive. Review cited findings, or turn on automatic publishing above a confidence threshold you choose.
- A balanced wiki build reuses previously analyzed turns when the conversation has published findings and its review queue is clear. The wiki shows messages skipped after repeated local-model failures and lets you queue them again by conversation.
- To index Python declarations locally without a model, run `lore index-functions --python <absolute-path-to-python> --project <path> --json` with Python 3.8 or newer. It parses indexed source without executing it. The Function index can still add model-based descriptions and dependency candidates.
- **Sources** can forget one or all saved conversations. **Delete forgotten** permanently removes the remaining Lore records and the wiki entries that cite them. Lore continues to skip those conversations during capture. Original provider logs and project files stay where they are.

## Files and commands

Lore stores its database, wiki, file index, and conversation sources under `.lore/` in the working directory. `BRAIN.md` links to those files. Lore preserves existing agent instructions and unrelated Claude hooks. Back up the working directory to keep memory and citations together.

Existing archives keep original records, wiki references, and historical data. This build opens new projects with project memory only and refreshes generated pointers when the archive is rendered.

```sh
lore sync --project /path/to/lore-workspace --json
lore import --project /path/to/lore-workspace --transcript-text --file conversation.txt --json
lore learn --project /path/to/lore-workspace --provider local --json
lore search --project /path/to/lore-workspace --query "SQLite migration" --json
lore brief --project /path/to/lore-workspace --query "What should I pick up next?" --json
lore handoff --project /path/to/lore-workspace --json
lore import --project /path/to/lore-workspace --chatgpt --file conversations.json --dry-run --json
lore doctor --project /path/to/lore-workspace --json
lore backup --project /path/to/lore-workspace --to /path/to/project-memory.sqlite --json
lore restore --file /path/to/project-memory.sqlite --project /path/to/new-archive --json
```

Restore opens a separate archive and rebuilds its Markdown exports. Connect the restored copy explicitly when you want to switch the project's managed pointers. Reconnect an existing archive to register the new Claude Code post-compaction hook. In **Conversations**, you can choose a chat from an extracted ChatGPT export. The CLI preview lists selections for `--session`. Remove `--dry-run` to import.

For agent access, register a local stdio MCP server with the absolute path to `lore` or `lore.exe` as its command and `["mcp", "--project", "<absolute archive path>"]` as its arguments. The server opens that archive read-only. Agents can call `search_evidence`, `open_evidence`, and `get_handoff`. Local model jobs stay local unless you explicitly configure a cloud fallback.

Open **Terminal** to follow model output during a run. Use **Ctrl+K** to search and **Ctrl+O** to switch projects.

## Windows build and Claude Code hooks

Use a Visual Studio 2022 x64 developer PowerShell with the C++ build tools and
Qt 6.8+ MSVC 2022 x64 tools on PATH. Python is needed only for integration tests.

```powershell
.\bin\build.ps1
.\bin\test.ps1
python tests/integration.py
.\bin\package-windows.ps1
.\bin\installer-windows.ps1

.\build\windows\lore.exe init --project 'C:\Work\my-project'
.\build\windows\lore.exe connect --project 'C:\Work\my-project'
```

Keep the complete `build/windows` folder together; it contains runtime
dependencies in addition to lore.exe and the console-free lore-desktop.exe
launcher. `installer-windows.ps1` requires Inno Setup 6 and produces the per-user
setup program. Windows CI builds, installs, launches, and uninstalls this package,
checks that project memory survives, and uploads installer and portable artifacts.

Connect writes the same project-local settings used on Linux. Hooks use
Claude Code's **exec form**: `command` is the absolute lore.exe path and `args`
contains `hook`, `--project`, and the project directory as separate strings.
No shell is involved, so neither Git Bash nor PowerShell quoting is required.
Use a current Claude Code version that supports command hooks with `args`.
Start a new Claude Code session after connecting. Run connect again after moving
the executable or project; it updates Lore's hooks and preserves unrelated hooks.
Existing Linux shell-form Lore hooks migrate on reconnect.

Windows tests cover native hook invocation, source capture, context injection,
and fake-provider learning. Live Claude/Codex authentication and their
installed Windows CLI variants require separate validation on the target machine.

## Validate

```sh
./bin/build
./bin/test
python3 tests/integration.py
python3 tests/evaluate_retrieval.py
python3 tests/evaluate_retrieval.py --benchmark
```

Tests use temporary projects and fake model providers.
The benchmark times the complete Claude prompt-hook process on a fictional archive. Pass `--binary` to compare packaged builds. Set `LORE_PROFILE=1` for per-stage timing on stderr when running a hook or briefing.
