"""Exercise the shipped CLI, and hook protocol without a model."""
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUFFIX = '.exe' if os.name == 'nt' else ''
BINARY = ROOT / f'build/lore{SUFFIX}'
PROVIDER_FIXTURE = ROOT / f'build/tests/lore_tests{SUFFIX}'


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='lore-test-')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.project = self.root / "atlas ' & $ mémoire (project)"
        self.project.mkdir()
        self.env = dict(os.environ, LORE_LLM_URL='disabled-for-tests',
                        QT_QPA_PLATFORM='offscreen',
                        QT_QPA_PLATFORMTHEME='generic', QT_QUICK_BACKEND='software',
                        QT_FORCE_STDERR_LOGGING='1')
        self.run_cli('init')
        self.transcript = self.root / 'session.jsonl'
        self.records = [dict(type=role, cwd=str(self.project),
                             timestamp='2026-11-18T10:00:00Z',
                             message=dict(role=role, content=[dict(type='text', text=text)]))
                        for role, text in [('user', 'Choose SQLite for local storage.'),
                                           ('assistant', 'Agreed. Next build the full text index.')]]
        self.transcript.write_text(''.join(json.dumps(r) + '\n' for r in self.records), encoding='utf-8')

    def run_cli(self, *args, input=None, success=True):
        result = subprocess.run([str(BINARY), *args, '--project', str(self.project), '--json'],
                                input=input, text=True, encoding='utf-8', capture_output=True,
                                timeout=15, env=self.env)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return result

    def capture(self):
        return self.run_cli('import', '--provider', 'claude', '--file', str(self.transcript))

    def run_installed_hook(self, event, **payload):
        settings = json.loads((self.project / '.claude/settings.local.json').read_text(encoding='utf-8'))
        matches = [hook for handler in settings['hooks'][event] for hook in handler['hooks']
                   if hook.get('statusMessage') == 'Lore project memory']
        self.assertEqual(len(matches), 1)
        hook = matches[0]
        data = dict(cwd=str(self.project), hook_event_name=event, **payload)
        result = subprocess.run([hook['command'], *hook['args']], input=json.dumps(data),
                                text=True, encoding='utf-8', capture_output=True, env=self.env, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_help_and_version_do_not_need_a_display(self):
        env = dict(self.env, QT_QPA_PLATFORM='unavailable-display-driver')
        for flag in ['--help', '--version']:
            result = subprocess.run([str(BINARY), flag], env=env, text=True,
                                    capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('lore', result.stdout.lower())

    def test_keyword_file_recall_backfills_and_tracks_changes(self):
        source = self.project / 'ledger.py'
        source.write_text('def settleInvoice():\n    return "originaltoken"\n', encoding='utf-8')
        self.run_cli('index')

        def evidence(query):
            return json.loads(self.run_cli('brief', '--query', query).stdout)['evidence']

        old_id = evidence('originaltoken')[0]['id']
        with closing(sqlite3.connect(self.project / '.lore/brain.sqlite')) as db:
            db.executescript("""
                DROP TRIGGER project_files_ai;
                DROP TRIGGER project_files_ad;
                DROP TRIGGER project_files_au;
                DROP TABLE project_files_fts;
                PRAGMA user_version=16;
            """)
        self.assertEqual(evidence('originaltoken')[0]['id'], old_id)

        source.write_text('def settleInvoice():\n    return "replacementtoken"\n', encoding='utf-8')
        self.run_cli('index')
        self.assertFalse(evidence('originaltoken'))
        self.assertEqual(evidence('replacementtoken')[0]['path'], 'ledger.py')
        source.rename(self.project / 'billing.py')
        self.run_cli('index')
        self.assertEqual(evidence('replacementtoken')[0]['path'], 'billing.py')
        self.assertFalse(evidence('ledger.py'))

    def test_hook_profiling_keeps_protocol_on_stdout(self):
        self.env['LORE_PROFILE'] = '1'
        payload = dict(hook_event_name='UserPromptSubmit', cwd=str(self.project),
                       transcript_path=str(self.transcript), prompt='Why SQLite?')
        result = self.run_cli('hook', input=json.dumps(payload))
        self.assertIn('SQLite', json.loads(result.stdout)['hookSpecificOutput']['additionalContext'])
        report = next(line for line in result.stderr.splitlines() if line.startswith('lore-profile '))
        stages = json.loads(report.removeprefix('lore-profile '))
        self.assertEqual(set(stages), {'startup_ms', 'store_open_ms', 'capture_ms', 'recall_ms', 'audit_ms'})
        self.assertTrue(all(value >= 0 for value in stages.values()))

    def test_prompt_hook_returns_context_when_capture_writer_is_busy(self):
        payload = dict(hook_event_name='UserPromptSubmit', cwd=str(self.project),
                       transcript_path=str(self.transcript), prompt='Why SQLite?')
        with closing(sqlite3.connect(self.project / '.lore/brain.sqlite')) as db:
            db.execute('BEGIN IMMEDIATE')
            result = self.run_cli('hook', input=json.dumps(payload))
            self.assertIn('project memory is busy', json.loads(result.stdout)['hookSpecificOutput']['additionalContext'])
            self.assertIn('could not record', result.stderr)
            db.rollback()
        result = self.run_cli('hook', input=json.dumps(payload))
        self.assertIn('SQLite', json.loads(result.stdout)['hookSpecificOutput']['additionalContext'])
        self.assertEqual(len(json.loads(self.run_cli('search', '--query', 'SQLite').stdout)), 1)

    def test_python_declarations_are_incremental_and_do_not_execute_source(self):
        source = self.project / 'rules.py'
        source.write_text('raise RuntimeError("do not execute")\n'
                          'class Invoice:\n    async def settle(self):\n'
                          + '        pass\n' * 150
                          + 'class Refund:\n    def settle(self):\n        pass\n', encoding='utf-8')

        def index_functions():
            return json.loads(self.run_cli('index-functions', '--python', sys.executable).stdout)

        self.assertEqual(index_functions()['indexed'], 1)
        self.assertEqual(index_functions()['indexed'], 0)
        response = json.loads(self.run_cli('brief', '--query', 'Invoice.settle').stdout)
        self.assertEqual(response['evidence'][0]['qualified'], 'Invoice.settle')
        self.assertEqual(response['evidence'][0]['origin'], 'Python syntax tree')
        with closing(sqlite3.connect(self.project / '.lore/brain.sqlite')) as db:
            records = json.loads(db.execute('SELECT records FROM function_files').fetchone()[0])
        self.assertEqual([(item['qualified'], item['endLine']) for item in records],
                         [('Invoice.settle', 153), ('Refund.settle', 156)])
        source.rename(self.project / 'billing.py')
        self.assertEqual(index_functions()['indexed'], 1)
        response = json.loads(self.run_cli('brief', '--query', 'Invoice.settle').stdout)
        self.assertEqual(response['evidence'][0]['path'], 'billing.py')

    def test_bootstrap_preserves_existing_files_and_executes_hooks(self):
        agents = self.project / 'AGENTS.md'
        agents.write_text('# Project rules\nKeep the existing rules.\n', encoding='utf-8')
        self.run_cli('connect')
        settings = json.loads((self.project / '.claude/settings.local.json').read_text(encoding='utf-8'))
        hook = settings['hooks']['SessionStart'][0]['hooks'][0]
        self.assertTrue(Path(hook['command']).samefile(BINARY))
        self.assertEqual(hook['args'][:2], ['hook', '--project'])
        self.assertTrue(Path(hook['args'][2]).samefile(self.project))
        self.assertNotIn('shell', hook)
        payload = dict(hook_event_name='SessionStart', transcript_path=str(self.transcript),
                       cwd=str(self.project))
        result = subprocess.run([hook['command'], *hook['args']], input=json.dumps(payload),
                                text=True, encoding='utf-8', capture_output=True, env=self.env, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        response = json.loads(result.stdout)
        self.assertIn('Read this first', response['hookSpecificOutput']['additionalContext'])
        self.assertIn('Keep the existing rules.', agents.read_text(encoding='utf-8'))
        payload.update(hook_event_name='UserPromptSubmit', prompt='Can you fix the SQLite problem?')
        result = self.run_cli('hook', input=json.dumps(payload))
        self.assertIn('Choose SQLite', json.loads(result.stdout)['hookSpecificOutput']['additionalContext'])
        for event in ['Stop', 'PreCompact', 'SessionEnd']:
            hook = settings['hooks'][event][0]['hooks'][0]
            payload['hook_event_name'] = event
            result = subprocess.run([hook['command'], *hook['args']], input=json.dumps(payload),
                                    text=True, encoding='utf-8', capture_output=True, env=self.env, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, '')
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 2)

    def test_clear_captures_the_conversation_before_it_resets(self):
        self.run_cli('connect')
        before = dict(transcript_path=str(self.transcript), session_id='before-clear')
        self.run_installed_hook('UserPromptSubmit', **before, prompt='Choose SQLite')
        correction = dict(self.records[0], message=dict(role='user', content=[
            dict(type='text', text='Correction: keep the existing SQLite schema.')]))
        noise = dict(self.records[0], message=dict(role='user', content=[
            dict(type='tool_result', content='verbosefixtureoutput')]))
        late = dict(self.records[1], message=dict(role='assistant', content=[
            dict(type='text', text='Updated src/storage.cpp. The migration check failed before the fix and now passes.')]))
        with self.transcript.open('a', encoding='utf-8') as f:
            for record in (correction, noise, late):
                f.write(json.dumps(record) + '\n')
        self.assertEqual(self.run_installed_hook('SessionEnd', **before, reason='clear'), '')
        # The archive already holds the final reply, before SessionStart runs.
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 4)
        self.assertTrue(json.loads(self.run_cli('search', '--query', 'migration check').stdout))
        self.run_installed_hook('SessionEnd', **before, reason='clear')
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 4)
        fresh = self.root / 'after-clear.jsonl'
        fresh.write_text(json.dumps(dict(self.records[0], message=dict(role='user', content=[dict(type='text', text='New topic after clear.')]))) + '\n', encoding='utf-8')
        fresh_context = json.loads(self.run_installed_hook(
            'SessionStart', source='clear', session_id='after-clear', transcript_path=str(fresh)
        ))['hookSpecificOutput']['additionalContext']
        self.assertIn('Correction: keep the existing SQLite schema.', fresh_context)
        self.assertIn('The migration check failed before the fix and now passes.', fresh_context)
        self.assertNotIn('verbosefixtureoutput', fresh_context)
        self.assertNotIn('New topic after clear.', fresh_context)
        self.assertLess(len(fresh_context), 5000)
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 5)
        raw = [line for export in (self.project / '.lore/sources').glob('*.jsonl')
               for line in export.read_text(encoding='utf-8').splitlines()]
        self.assertEqual([json.loads(line) for line in raw].count(late), 1)

    def test_clear_start_recovers_saved_tail_when_ending_hook_misses_it(self):
        self.run_cli('connect')
        before = dict(transcript_path=str(self.transcript), session_id='before-clear')
        self.run_installed_hook('UserPromptSubmit', **before, prompt='Choose SQLite')
        late = dict(self.records[1], message=dict(role='assistant', content=[dict(type='text', text='Recover the amethyst checkpoint.')]))
        last_line = json.dumps(late) + '\n'
        with self.transcript.open('a', encoding='utf-8') as f:
            f.write(last_line[:len(last_line) // 2])
        self.run_installed_hook('SessionEnd', **before, reason='clear')
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 2)
        with self.transcript.open('a', encoding='utf-8') as f:
            f.write(last_line[len(last_line) // 2:])

        other = self.root / 'unrelated'
        other.mkdir()
        sibling = self.root / 'other-project.jsonl'
        sibling.write_text(json.dumps(dict(self.records[0], cwd=str(other))) + '\n', encoding='utf-8')
        fresh = self.root / 'after-clear.jsonl'  # The new session has not written a transcript yet.
        after = dict(transcript_path=str(fresh), session_id='after-clear')
        self.run_installed_hook('SessionStart', **after, source='startup')
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 2)
        self.run_installed_hook('SessionStart', **after, source='clear')
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 3)
        reply = self.run_installed_hook('UserPromptSubmit', **after, prompt='Where is the amethyst checkpoint?')
        self.assertIn('Recover the amethyst checkpoint.', json.loads(reply)['hookSpecificOutput']['additionalContext'])
        self.run_installed_hook('SessionStart', **after, source='clear')
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 3)

    def test_first_prompt_recovers_clear_checkpoint_once(self):
        self.run_cli('connect')
        self.run_installed_hook('SessionEnd', transcript_path=str(self.transcript),
                                session_id='before-clear', reason='clear')
        fresh = dict(transcript_path=str(self.root / 'fresh.jsonl'), session_id='after-clear')
        first = self.run_installed_hook('UserPromptSubmit', **fresh, prompt='Continue')
        self.assertIn('Recent saved conversation excerpts', first)
        self.assertIn('Choose SQLite for local storage.', first)
        second = self.run_installed_hook('SessionStart', **fresh, source='clear')
        self.assertNotIn('Recent saved conversation excerpts', second)
        third = self.run_installed_hook('UserPromptSubmit', **fresh, prompt='Continue')
        self.assertNotIn('Recent saved conversation excerpts', third)

    def test_handoff_and_read_only_mcp_resolve_the_same_evidence(self):
        self.capture()
        handoff = json.loads(self.run_cli('handoff').stdout)
        self.assertIn('Choose SQLite', handoff['latestRequest']['body'])
        self.assertIn('full text index', handoff['latestReport']['body'])
        evidence_id = handoff['latestRequest']['id']
        messages = [
            dict(id=1, method='initialize', params=dict(protocolVersion='2025-06-18')),
            dict(method='notifications/initialized'),
            dict(id=2, method='tools/list'),
            dict(id=3, method='tools/call', params=dict(name='get_handoff')),
            dict(id=4, method='tools/call', params=dict(
                name='open_evidence', arguments=dict(id=evidence_id))),
            dict(id=5, method='tools/call', params=dict(
                name='open_evidence', arguments=dict(id='event:' + '0' * 24 + ':0'))),
            dict(id=6, method='tools/call', params=dict(
                name='search_evidence', arguments=dict(query='SQLite'))),
        ]
        payload = ''.join(json.dumps(dict(request, jsonrpc='2.0')) + '\n' for request in messages)
        replies = {reply['id']: reply['result'] for reply in map(
            json.loads, self.run_cli('mcp', input=payload).stdout.splitlines())}
        self.assertEqual(replies[1]['protocolVersion'], '2025-06-18')
        self.assertEqual({tool['name'] for tool in replies[2]['tools']},
                         {'get_handoff', 'open_evidence', 'search_evidence'})
        self.assertEqual(json.loads(replies[3]['content'][0]['text'])['session'], handoff['session'])
        self.assertIn('Choose SQLite', json.loads(replies[4]['content'][0]['text'])['body'])
        self.assertTrue(replies[5]['isError'])
        self.assertTrue(json.loads(replies[6]['content'][0]['text'])['evidence'])

    def test_database_backup_includes_committed_wal_records(self):
        self.capture()
        backup = self.root / 'verified.sqlite'
        archive = self.project / '.lore' / 'brain.sqlite'
        with closing(sqlite3.connect(archive)) as conn:
            conn.execute("UPDATE events SET time='fixture backup time' WHERE seq=1")
            conn.commit()
            self.assertTrue(json.loads(self.run_cli('backup', '--to', str(backup)).stdout)['verified'])
        with closing(sqlite3.connect(backup)) as conn:
            self.assertEqual(conn.execute('PRAGMA quick_check').fetchone()[0], 'ok')
            self.assertEqual(conn.execute('SELECT time FROM events WHERE seq=1').fetchone()[0],
                             'fixture backup time')
        self.assertNotEqual(self.run_cli('backup', '--to', str(backup), success=False).returncode, 0)
        destination = self.root / 'restored archive'
        restored = subprocess.run([str(BINARY), 'restore', '--file', str(backup), '--project',
                                   str(destination), '--json'], capture_output=True, text=True,
                                  encoding='utf-8', env=self.env, timeout=15)
        self.assertEqual(restored.returncode, 0, restored.stderr)
        originals = sorted(path.name for path in (self.project / '.lore' / 'sources').glob('*.md'))
        restored_pages = sorted(path.name for path in (destination / '.lore' / 'sources').glob('*.md'))
        self.assertEqual(restored_pages, originals)
        self.assertTrue((destination / 'BRAIN.md').exists())

    def test_post_compact_resumes_the_current_transcript(self):
        self.run_cli('connect')
        reply = self.run_installed_hook('PostCompact', transcript_path=str(self.transcript),
                                        session_id='compacting')
        self.assertIn('Choose SQLite for local storage.', reply)
        self.assertIn('PostCompact', reply)

    def test_chatgpt_export_preview_preserves_roles_and_active_branch(self):
        export = self.root / 'conversations.json'

        def node(role, text, parent=None):
            return dict(parent=parent, message=dict(author=dict(role=role),
                        create_time=1700000000, content=dict(parts=[text])))

        export.write_text(json.dumps([dict(title='ChatGPT export', current_node='answer',
            mapping=dict(request=node('user', 'Export fixture question'),
                         answer=node('assistant', 'Selected export answer', 'request'),
                         other=node('assistant', 'Discarded branch', 'request')))]), encoding='utf-8')
        args = ('import', '--chatgpt', '--file', str(export))
        preview = json.loads(self.run_cli(*args, '--dry-run').stdout)
        self.assertEqual(preview['conversations'], [{'id': '0', 'title': 'ChatGPT export'}])
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 0)
        self.assertEqual(json.loads(self.run_cli(*args, '--session', '0').stdout)['added'], 2)
        self.assertEqual(json.loads(self.run_cli(*args).stdout)['added'], 0)
        handoff = json.loads(self.run_cli('handoff').stdout)
        self.assertEqual(handoff['latestRequest']['role'], 'user')
        self.assertEqual(handoff['latestReport']['body'], 'Selected export answer')
        self.assertEqual(handoff['latestReport']['time'], '2023-11-14T22:13:20.000Z')

    def test_failed_migration_reconnect_preserves_original_archive(self):
        self.capture()
        settings = self.project / '.claude' / 'settings.local.json'
        settings.parent.mkdir(exist_ok=True)
        settings.write_text('{"hooks": 123}', encoding='utf-8')
        destination = self.root / 'reconnect failed'
        result = self.run_cli('migrate', '--to', str(destination), success=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('original archive remains', result.stderr)
        self.assertTrue((self.project / '.lore' / 'brain.sqlite').is_file())
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 2)

    def test_connecting_later_recovers_earlier_sessions(self):
        # Sessions from before a project was connected stay on disk; the first
        # sync finds them by their working folder.
        claude_root = self.root / 'claude-projects'
        folder = claude_root / 'earlier'
        folder.mkdir(parents=True)
        (folder / 'old.jsonl').write_text(self.transcript.read_text(encoding='utf-8'), encoding='utf-8')
        (self.root / 'codex-sessions').mkdir()
        self.run_cli('sync', '--codex-root', str(self.root / 'codex-sessions'), '--claude-root', str(claude_root))
        self.assertEqual(json.loads(self.run_cli('status').stdout)['events'], 2)

    def test_separate_working_directory_via_cli(self):
        working = self.root / "Lore-atlas ' & $ files"
        result = subprocess.run([str(BINARY), 'init', '--project', str(working), '--source', str(self.project), '--json'],
                                env=self.env, text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(Path(json.loads(result.stdout)['source']).samefile(self.project))
        result = subprocess.run([str(BINARY), 'connect', '--project', str(working), '--json'],
                                env=self.env, text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        hook = json.loads((self.project / '.claude/settings.local.json').read_text(encoding='utf-8'))['hooks']['SessionStart'][0]['hooks'][0]
        self.assertTrue(Path(hook['args'][2]).samefile(working))
        payload = dict(hook_event_name='SessionStart', transcript_path=str(self.transcript), cwd=str(self.project))
        result = subprocess.run([hook['command'], *hook['args']], input=json.dumps(payload), env=self.env,
                                text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('project brain', json.loads(result.stdout)['hookSpecificOutput']['additionalContext'])
        self.assertTrue((working / 'BRAIN.md').exists())
        self.assertTrue((working / '.lore/sources/index.md').read_text(encoding='utf-8').count('](') >= 1)
        self.assertIn(str(working / 'BRAIN.md'), (self.project / 'CLAUDE.md').read_text(encoding='utf-8'))

    def test_migrate_moves_an_in_project_archive_out(self):
        self.capture()
        self.run_cli('connect')
        workspaces = self.root / 'Documents'
        env = dict(self.env, LORE_WORKSPACE_ROOT=str(workspaces))
        result = subprocess.run([str(BINARY), 'migrate', '--project', str(self.project), '--json'], env=env,
                                text=True, encoding='utf-8', capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        moved = json.loads(result.stdout)
        working = Path(moved['project'])
        self.assertTrue(moved['migrated'])
        self.assertTrue(moved['oldCopyRemoved'])
        self.assertTrue(working.samefile(workspaces / f'Lore-{self.project.name}'))
        self.assertFalse((self.project / '.lore').exists())
        self.assertFalse((self.project / 'BRAIN.md').exists())
        self.assertTrue((working / 'BRAIN.md').exists())
        status = subprocess.run([str(BINARY), 'status', '--project', str(working), '--json'], env=env,
                                text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(json.loads(status.stdout)['events'], 2)
        self.assertIn(str(working / 'BRAIN.md'), (self.project / 'CLAUDE.md').read_text(encoding='utf-8'))
        self.assertNotIn('/.lore/', (self.project / '.gitignore').read_text(encoding='utf-8'))
        hook = json.loads((self.project / '.claude/settings.local.json').read_text(encoding='utf-8'))['hooks']['SessionStart'][-1]['hooks'][0]
        self.assertTrue(Path(hook['args'][2]).samefile(working))
        again = subprocess.run([str(BINARY), 'migrate', '--project', str(working), '--json'], env=env,
                               text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertFalse(json.loads(again.stdout)['migrated'])
        inside = subprocess.run([str(BINARY), 'migrate', '--project', str(working), '--to', str(self.project / 'nested'), '--json'],
                                env=env, text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertNotEqual(inside.returncode, 0)

    def test_exec_hook_accepts_executable_paths_with_special_characters(self):
        folder = self.root / "Lore's & $ tools (mémoire)"
        folder.mkdir()
        binary = folder / f'lore{SUFFIX}'
        shutil.copy2(BINARY, binary)
        result = subprocess.run([str(binary), 'connect', '--project', str(self.project), '--json'],
                                env=self.env, text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        hook = json.loads((self.project / '.claude/settings.local.json').read_text(encoding='utf-8'))['hooks']['SessionStart'][0]['hooks'][0]
        result = subprocess.run([hook['command'], *hook['args']],
                                input=json.dumps(dict(hook_event_name='SessionStart',
                                                      transcript_path=str(self.transcript))),
                                env=self.env, text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Read this first', json.loads(result.stdout)['hookSpecificOutput']['additionalContext'])

    def test_idempotent_import_and_model_failure_leave_pending_messages(self):
        self.assertEqual(json.loads(self.capture().stdout)['added'], 2)
        self.assertEqual(json.loads(self.capture().stdout)['added'], 0)
        status = json.loads(self.run_cli('status').stdout)
        self.assertEqual(status['pending'], 2)
        prompt = json.loads(self.run_cli('learn', '--dry-run').stdout)
        self.assertEqual(len(prompt['events']), 2)
        self.fake_provider('codex', invalid=True)
        result = self.run_cli('learn', '--provider', 'codex', success=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn('unknown event', result.stderr)
        status = json.loads(self.run_cli('status').stdout)
        self.assertEqual(status['pending'], 2)
        self.assertEqual(status['memories'], 0)

    def fake_provider(self, provider, invalid=False):
        executable = self.root / f'{provider}{SUFFIX}'
        shutil.copy2(PROVIDER_FIXTURE, executable)
        executable.chmod(0o700)
        if invalid:
            self.env['LORE_TEST_INVALID_REFS'] = '1'
        else:
            self.env.pop('LORE_TEST_INVALID_REFS', None)
        self.env['PATH'] = str(self.root) + os.pathsep + os.environ['PATH']

    def test_both_provider_response_formats_build_referenced_wiki(self):
        self.capture()
        for provider in ['codex', 'claude']:
            with self.subTest(provider=provider):
                self.fake_provider(provider)
                self.env['LORE_TEST_ARGS_FILE'] = str(self.root / f'{provider}-args.txt')
                result = self.run_cli('learn', '--provider', provider, '--model', 'test-model', '--effort', 'high')
                args = Path(self.env['LORE_TEST_ARGS_FILE']).read_text(encoding='utf-8')
                expected = ('--model test-model --effort high' if provider == 'claude'
                            else '-m test-model -c model_reasoning_effort="high"')
                self.assertIn(expected, args)
                self.assertTrue(json.loads(result.stdout)['ok'])
                status = json.loads(self.run_cli('status').stdout)
                self.assertEqual(status['pending'], 0)
                wiki = list((self.project / '.lore/wiki').glob('[0-9]*.md'))
                self.assertTrue(wiki)
                self.assertIn('.md#', wiki[-1].read_text(encoding='utf-8'))
                # Add another real record so the second provider has work.
                with self.transcript.open('a') as f:
                    f.write(json.dumps(dict(self.records[0], timestamp=f'new-{provider}')) + '\n')
                self.capture()

    def test_project_index_and_audited_hook_context(self):
        (self.project / 'auth.py').write_text(
            'class SessionManager:\n    def renew(self):\n        return True\n', encoding='utf-8')
        indexed = self.run_cli('index')
        self.assertEqual(json.loads(indexed.stdout)['files'], 1)
        brief = json.loads(self.run_cli('brief', '--query', 'SessionManager').stdout)
        self.assertEqual(brief['evidence'][0]['type'], 'file')
        self.assertIn('SessionManager', brief['text'])
        # Seed a completed model scan without making a model request.
        record = dict(id='fixture-renew', name='renew', qualified='SessionManager.renew',
                      line=2, signature='def renew(self):', summary='Continue the existing session.', calls=[])
        with closing(sqlite3.connect(self.project / '.lore/brain.sqlite')) as db, db:
            db.execute("INSERT INTO function_files(path,hash,indexed,records) SELECT path,hash,modified,? FROM project_files WHERE path='auth.py'",
                       (json.dumps([record]),))
        payload = dict(hook_event_name='UserPromptSubmit', session_id='fixture',
                       transcript_path=str(self.transcript), prompt='Where is renew() defined?')
        response = json.loads(self.run_cli('hook', input=json.dumps(payload)).stdout)
        context = response['hookSpecificOutput']['additionalContext']
        self.assertIn('SessionManager.renew', context)
        self.assertIn('auth.py:2', context)
        self.assertLessEqual(len(context.encode('utf-8')), 3600)
        delivery = json.loads(self.run_cli('deliveries').stdout)[0]
        self.assertEqual(delivery['session'], 'fixture')
        self.assertEqual(delivery['text'], response['hookSpecificOutput']['additionalContext'])

    def test_plain_text_import_and_tool_noise_filter(self):
        conversation = self.project / 'conversation.txt'
        conversation.write_text('User: Keep the SQLite schema.\nChatGPT: Migration passed.\n', encoding='utf-8')
        args = ('import', '--transcript-text', '--file', str(conversation))
        self.assertEqual(json.loads(self.run_cli(*args).stdout)['added'], 2)
        self.assertEqual(json.loads(self.run_cli(*args).stdout)['added'], 0)
        tool = dict(self.records[0], message=dict(role='user', content=[dict(
            type='tool_result', content='throwawaytooloutput')]))
        with self.transcript.open('a', encoding='utf-8') as transcript:
            transcript.write(json.dumps(tool) + '\n')
        self.capture()
        self.assertEqual(json.loads(self.run_cli('search', '--query', 'throwawaytooloutput').stdout), [])
        events = json.loads(self.run_cli('learn', '--dry-run').stdout)['events']
        self.assertTrue(any(event['text'] == 'Migration passed.' for event in events))
        self.assertTrue(all('throwawaytooloutput' not in event['text'] for event in events))

    def test_sync_cache_and_project_filter(self):
        codex_root, claude_root = self.root / 'codex-logs', self.root / 'claude-logs'
        codex_root.mkdir()
        claude_root.mkdir()
        (claude_root / 'included.jsonl').write_bytes(self.transcript.read_bytes())
        other = self.root / 'unrelated'
        other.mkdir()
        (claude_root / 'unrelated.jsonl').write_text(json.dumps(dict(self.records[0], cwd=str(other))) + '\n')
        args = ['sync', '--codex-root', str(codex_root), '--claude-root', str(claude_root)]
        first = json.loads(self.run_cli(*args).stdout)
        self.assertEqual(first['added'], 2)
        second = json.loads(self.run_cli(*args).stdout)
        self.assertEqual(second['filesChecked'], 0)
        self.assertEqual(json.loads(self.run_cli('status').stdout)['sources'], 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
