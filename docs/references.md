# Integration documentation

- [Claude Code hook reference](https://code.claude.com/docs/en/hooks):
  transcript_path, cwd, hook_event_name, lifecycle events, and additionalContext.
- [Codex project instructions](https://learn.chatgpt.com/docs/agent-configuration/agents-md):
  AGENTS.md is the entry route to BRAIN.md. Existing overrides and higher
  priority instructions still apply.
- [Codex non-interactive mode](https://learn.chatgpt.com/docs/non-interactive-mode):
  exec JSON events are used to collect the final learning result.
- Installed CLI help checked against codex-cli 0.170.0 and Claude Code 2.1.326.

Saved transcript formats can change across CLI versions. Fixtures cover Claude
Code message records and Codex session_meta/response_item JSONL records. Unknown
record types are preserved as raw JSON instead of being discarded.
