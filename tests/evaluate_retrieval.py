"""Evaluate ranking and abstention on a fictional project; no answer-model calls.

Pass --semantic after installing Ollama's embeddinggemma model to evaluate
actual embeddings too. The default run measures the offline keyword baseline.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / ('build/lore.exe' if os.name == 'nt' else 'build/lore')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--semantic', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lore-evaluation-') as scratch:
        project = Path(scratch) / 'atlas'
        project.mkdir()
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic')

        def run(*command):
            response = subprocess.run([str(BINARY), *command, '--project', str(project), '--json'],
                                      capture_output=True, text=True, encoding='utf-8',
                                      check=True, timeout=180, env=env)
            return json.loads(response.stdout)

        run('init')
        (project / 'auth.py').write_text('class SessionManager:\n    """Authentication uses local sessions."""\n', encoding='utf-8')
        run('index')
        statements = [
            'We chose SQLite because the library must work offline.',
            'All documents must stay on this computer to protect confidential research.',
            'Next: connect search results to the document preview.',
            'We rejected a hosted database because the library should work without a network.',
            'Yellow is the accent color used by the interface.',
        ]
        transcript = Path(scratch) / 'conversation.jsonl'
        transcript.write_text(''.join(json.dumps(dict(type='user', cwd=str(project),
            message=dict(role='user', content=text))) + '\n' for text in statements), encoding='utf-8')
        run('import', '--provider', 'claude', '--file', str(transcript))
        if args.semantic:
            run('semantic', '--action', 'index')
        cases = [
            ('Why SQLite?', 'SQLite'),
            ('Where is SessionManager?', 'SessionManager'),
            ('What happens next with search?', 'document preview'),
            ('Why reject a hosted database?', 'without a network'),
            ('What accent color does the interface use?', 'Yellow'),
        ]
        if args.semantic:
            cases.append(('Why avoid sending sensitive material to servers?', 'confidential research'))
        results = []
        for question, expected in cases:
            response = run('brief', '--query', question)
            top = response['evidence'][:3]
            results.append(dict(question=question, hit=any(expected in item['body'] for item in top),
                                retrieval=response['retrieval']))
        unknown = run('brief', '--query', 'xyzunknownunrelated')
        score = sum(item['hit'] for item in results) / len(results)
        report = dict(mode='semantic' if args.semantic else 'keyword', recall_at_3=score,
                      unknown_query_abstained=not unknown['evidence'], cases=results)
        print(json.dumps(report, indent=2))
        # The semantic unknown query is deliberately reported rather than
        # presumed reliable: dense similarity can retrieve unrelated evidence.
        if score < 0.8 or (not args.semantic and unknown['evidence']):
            raise SystemExit('Retrieval regression')


if __name__ == '__main__':
    main()
