"""Create fictional project data and render README previews without a model."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/lore'
scratch = Path(tempfile.mkdtemp(prefix='lore-preview-'))
project = scratch / 'atlas'
project.mkdir()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
           QT_QUICK_BACKEND='software', QT_FORCE_STDERR_LOGGING='1',
           LORE_THEME_DIRECTORY=str(scratch / 'theme-fallback'),
           LORE_PREFERENCES_FILE=str(scratch / 'preferences.json'),
           LORE_SKIP_TOOL_CHECKS='1')


def run(*args, **kwargs):
    subprocess.run([str(BINARY), *args, '--project', str(project)], env=env,
                   check=True, capture_output=True, text=True, timeout=20, **kwargs)


run('init')
claude = scratch / 'claude.jsonl'
conversation = [
    ('user', 'Build Atlas, a local research library. Keep documents and references on disk.'),
    ('assistant', 'We will store metadata in SQLite and use FTS5 for keyword search. Original documents stay untouched.'),
    ('user', 'Agreed. Use a small entry point that links to topics rather than loading the entire library.'),
    ('assistant', 'The project index will link to topic pages, decisions, and original sources.'),
]
claude.write_text(''.join(json.dumps(dict(type=role, cwd=str(project),
    timestamp='2026-11-18T10:00:00Z', message=dict(role=role, content=text))) + '\n'
    for role, text in conversation))
run('import', '--provider', 'claude', '--file', str(claude))
codex = scratch / 'codex.jsonl'
records = [dict(type='session_meta', timestamp='2026-11-18T11:00:00Z',
                payload=dict(cwd=str(project), id='demo-codex-session'))]
for role, text in [
    ('user', 'The document importer is ready. What should we work on next?'),
    ('assistant', 'Next: connect the search index to the library view, then verify every result can open its original source.'),
    ('user', 'Yes. Keep this as our next step and preserve the source citations.'),
]:
    records.append(dict(type='response_item', timestamp='2026-11-18T11:00:00Z',
        payload=dict(type='message', role=role, content=[dict(type='output_text' if role == 'assistant' else 'input_text', text=text)])))
codex.write_text(''.join(json.dumps(record) + '\n' for record in records))
run('import', '--provider', 'codex', '--file', str(codex))
tools = scratch / 'tools'
tools.mkdir()
fake = tools / 'codex'
fake.write_text('''#!/usr/bin/python3
import sys,json
events=json.loads(sys.stdin.read().split('NEW EVENTS:\\n',1)[1])
def entry(title,kind,body,index):
    return dict(title=title,kind=kind,body=body,refs=[events[index]['id']])
result={'entries':[
 entry('A library that lives on your machine','fact','Atlas is a local research library. Documents and references stay on disk.',0),
 entry('SQLite and full text search','decision','Use SQLite for metadata and FTS5 for keyword search. Preserve original documents.',1),
 entry('A small index, a linked knowledge base','decision','Keep the entry point short. Follow topic pages and citations to retrieve detail when needed.',2),
 entry('Connect search results to their original sources','next','Wire the search index into the library view, then verify that every result opens its source.',5)
]}
print(json.dumps({'type':'item.completed','item':{'type':'agent_message','text':json.dumps(result)}}))
''')
fake.chmod(0o700)
env['PATH'] = str(tools) + os.pathsep + os.environ['PATH']
run('learn', '--provider', 'codex')
run('connect')
env['PATH'] = os.environ['PATH']
language = scratch / 'language'
env['LORE_LANGUAGE_FILE'] = str(language)
language.write_text('en\n')
run('open', '--screenshot', str(ROOT / 'docs/lore.png'))
language.write_text('da\n')
run('open', '--screenshot', str(ROOT / 'build/lore-da.png'))
print(project)
