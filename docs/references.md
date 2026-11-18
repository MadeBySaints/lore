# Integration documentation

- [Claude Code hook reference](https://code.claude.com/docs/en/hooks):
  transcript_path, cwd, hook_event_name, lifecycle events, and additionalContext.
  Command hooks use exec form (`command` plus `args`) to preserve native Windows
  paths and avoid shell quoting on every platform.
- [Codex project instructions](https://learn.chatgpt.com/docs/agent-configuration/agents-md):
  AGENTS.md is the entry route to BRAIN.md. Existing overrides and higher
  priority instructions still apply.
- [Codex non-interactive mode](https://learn.chatgpt.com/docs/non-interactive-mode):
  exec JSON events are used to collect the final learning result.
- Installed CLI help checked against codex-cli 0.170.0 and Claude Code 2.1.326.
- [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html):
  windeployqt bundles the Qt DLLs, QML modules, and runtime plugins.
- [Ollama embeddings](https://docs.ollama.com/api/embed) and
  [EmbeddingGemma](https://ollama.com/library/embeddinggemma): optional loopback
  embeddings, batched inputs, and a separately downloaded local model.
- [SQLite FTS5](https://sqlite.org/fts5.html): lexical ranking for exact terms;
  task retrieval combines it with memory/file matches and optional vectors.

Saved transcript formats can change across CLI versions. Fixtures cover Claude
Code message records and Codex session_meta/response_item JSONL records. Unknown
record types are preserved as raw JSON instead of being discarded.
