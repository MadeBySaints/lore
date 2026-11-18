"""Exercise the shipped CLI, hook protocol, and MCP transport without a model."""
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
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
        self.env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
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

    def test_help_and_version_do_not_need_a_display(self):
        env = dict(self.env, QT_QPA_PLATFORM='unavailable-display-driver')
        for flag in ['--help', '--version']:
            result = subprocess.run([str(BINARY), flag], env=env, text=True,
                                    capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('lore', result.stdout.lower())

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
                result = self.run_cli('learn', '--provider', provider)
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
        (self.project / 'auth.py').write_text('class SessionManager:\n    pass\n', encoding='utf-8')
        indexed = self.run_cli('index')
        self.assertEqual(json.loads(indexed.stdout)['files'], 1)
        brief = json.loads(self.run_cli('brief', '--query', 'SessionManager').stdout)
        self.assertEqual(brief['evidence'][0]['type'], 'file')
        self.assertIn('SessionManager', brief['text'])
        payload = dict(hook_event_name='UserPromptSubmit', session_id='fixture',
                       transcript_path=str(self.transcript), prompt='SessionManager')
        response = json.loads(self.run_cli('hook', input=json.dumps(payload)).stdout)
        self.assertIn('SessionManager', response['hookSpecificOutput']['additionalContext'])
        delivery = json.loads(self.run_cli('deliveries').stdout)[0]
        self.assertEqual(delivery['session'], 'fixture')
        self.assertEqual(delivery['text'], response['hookSpecificOutput']['additionalContext'])

    def test_ask_rejects_invalid_input_without_waiting_for_a_provider(self):
        for arguments in [('ask',), ('ask', '--query', 'question', '--provider', 'invalid')]:
            result = self.run_cli(*arguments, success=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(result.stderr.strip())

    def test_mcp_responds_before_stdin_closes(self):
        self.capture()
        process = subprocess.Popen([str(BINARY), 'mcp', '--project', str(self.project)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8', env=self.env)
        replies = queue.Queue()
        def read_responses():
            for line in process.stdout:
                replies.put(line)
            replies.put(None)
        reader = threading.Thread(target=read_responses, daemon=True)
        reader.start()
        def cleanup():
            process.stdin.close()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            reader.join(timeout=3)
            process.stdout.close()
            process.stderr.close()
        self.addCleanup(cleanup)

        def request(method, params=None):
            process.stdin.write(json.dumps(dict(jsonrpc='2.0', id=1, method=method,
                                                params=params or {})) + '\n')
            process.stdin.flush()
            try:
                line = replies.get(timeout=5)
            except queue.Empty:
                self.fail('MCP did not respond on the open stdio connection')
            self.assertTrue(line, 'MCP exited early')
            response = json.loads(line)
            self.assertNotIn('error', response)
            return response['result']

        self.assertIn('protocolVersion', request('initialize'))
        tools = request('tools/list')['tools']
        self.assertEqual(len(tools), 7)
        search = request('tools/call', dict(name='brain_search', arguments={'query': 'SQLite'}))
        self.assertIn('Choose SQLite', search['content'][0]['text'])
        sessions = request('tools/call', dict(name='brain_sessions', arguments={}))
        source = json.loads(sessions['content'][0]['text'])[0]['id']
        records = request('tools/call', dict(name='brain_session', arguments={'id': source, 'limit': 1}))
        self.assertEqual(len(json.loads(records['content'][0]['text'])), 1)

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
