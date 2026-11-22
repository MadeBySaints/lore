"""Evaluate offline retrieval and optionally time the full Claude prompt hook.

Pass --semantic only with a locally installed EmbeddingGemma. The default
uses fictional content, needs no model, and runs on Windows and Linux.
"""
import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / ('build/lore.exe' if os.name == 'nt' else 'build/lore')

# Each topic has two independently phrased queries: a direct lookup and a
# question using a different clue from the same evidence.
PROJECTS = {
    'atlas': [
        ('SQLite keeps the library usable offline.', 'Why SQLite?', 'Why must the library work offline?'),
        ('Confidential research documents stay on this computer.', 'Where are research documents kept?', 'What keeps confidential material private?'),
        ('Connect search results to the document preview next.', 'What is next for search results?', 'Which work remains on the document preview?'),
        ('The hosted database was rejected because network access is unavailable.', 'Why reject the hosted database?', 'What decision followed from unavailable network access?'),
        ('The interface accent color is yellow.', 'What accent color is used?', 'Where is yellow used?'),
        ('The import test passed with duplicate identifiers.', 'Did the import test pass?', 'What did verification say about duplicate identifiers?'),
        ('The invoice export is blocked by missing currency codes.', 'What blocks invoice export?', 'Where do missing currency codes matter?'),
        ('Restart recovery preserves the indexing cursor.', 'What does restart recovery preserve?', 'Which cursor survives a restart?'),
        ('Book cover thumbnails are generated lazily.', 'How are book cover thumbnails generated?', 'What is generated lazily?'),
    ],
    'relay': [
        ('The queue backs off on a rate limit response.', 'How does the queue handle a rate limit?', 'What response causes backoff?'),
        ('Webhook delivery retries use an idempotency key.', 'How do webhook delivery retries work?', 'Why is there an idempotency key?'),
        ('The acceptance test failed for expired signing keys.', 'Which acceptance test failed?', 'What happened with expired signing keys?'),
        ('We deferred the email digest until notification preferences ship.', 'Why was the email digest deferred?', 'What needs to ship before the digest?'),
        ('The maintainer approved a dedicated dead-letter queue.', 'Was a dead-letter queue approved?', 'What queue did the maintainer approve?'),
        ('The driver migration is blocked by the legacy barcode scanner.', 'What blocks the driver migration?', 'Which change is waiting on the barcode scanner?'),
        ('The shipment feed uses a monotonic sequence.', 'How is the shipment feed ordered?', 'What stream uses a monotonic sequence?'),
        ('The manifest stores UTC timestamps for dispatch.', 'Which timestamps are in the manifest?', 'Where does dispatch use UTC?'),
        ('The retry service logs the upstream correlation identifier.', 'What does the retry service log?', 'Where is the correlation identifier recorded?'),
    ],
    'grove': [
        ('Seed catalog pages cache the latest inventory snapshot.', 'What do seed catalog pages cache?', 'Which pages use the inventory snapshot?'),
        ('The greenhouse sensor reports humidity every minute.', 'How often does the greenhouse sensor report?', 'What humidity measurement cadence did we use?'),
        ('Soil readings are blocked by missing calibration points.', 'What blocks soil readings?', 'Where are calibration points still missing?'),
        ('We replaced CSV exports with newline-delimited JSON.', 'What replaced CSV exports?', 'Why do exports contain newline-delimited JSON?'),
        ('The irrigation controller stops on a low reservoir level.', 'What stops the irrigation controller?', 'What happens when the reservoir level is low?'),
        ('The forecast worker passed the winter storm replay.', 'Which forecast worker test passed?', 'What was the outcome of the winter storm replay?'),
        ('The alert sidebar stays collapsed after refresh.', 'How does the alert sidebar behave?', 'What remains collapsed after refresh?'),
        ('The planting calendar uses sunrise in the farm timezone.', 'What does the planting calendar use?', 'Where does the farm timezone affect sunrise?'),
        ('The fertilizer report rounds quantities to whole grams.', 'How does the fertilizer report round quantities?', 'Which report produces whole grams?'),
    ],
}

FILES = {
    'atlas': ('auth.py', 'class SessionManager:\n    """Authentication uses local sessions."""\n', 'SessionManager', 'auth.py'),
    'relay': ('workers/transport.py', 'def deliver_webhook():\n    """Send a signed payload."""\n', 'deliver_webhook', 'workers/transport.py'),
    'grove': ('weather/pressure.md', 'Barometric readings are sampled at dawn.\n', 'barometric readings', 'weather/pressure.md'),
}


def run(binary, project, *command, payload=None, profile=False):
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic',
               LORE_PROFILE='1' if profile else '0')
    response = subprocess.run([str(binary), *command, '--project', str(project), '--json'],
                              input=json.dumps(payload) if payload is not None else None,
                              capture_output=True, text=True, encoding='utf-8',
                              check=True, timeout=180, env=env)
    timings = {}
    for line in response.stderr.splitlines():
        if line.startswith('lore-profile '):
            timings = json.loads(line.removeprefix('lore-profile '))
    return json.loads(response.stdout), timings


def import_turns(binary, project, transcript, statements):
    transcript.write_text(''.join(
        json.dumps(dict(type='user', cwd=str(project), message=dict(role='user', content=text))) + '\n'
        for text in statements), encoding='utf-8')
    run(binary, project, 'import', '--provider', 'claude', '--file', str(transcript))


def evaluate(binary, scratch, semantic):
    results = []
    for name, topics in PROJECTS.items():
        project = scratch / name
        project.mkdir()
        run(binary, project, 'init')
        relative, code, symbol, path = FILES[name]
        file = project / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(code, encoding='utf-8')
        run(binary, project, 'index')
        import_turns(binary, project, scratch / f'{name}.jsonl', [row[0] for row in topics])
        if semantic:
            run(binary, project, 'semantic', '--action', 'index')
        options = ('--semantic',) if semantic else ()
        cases = [(question, 'body', evidence, 'conversation')
                 for evidence, direct, alternate in topics for question in (direct, alternate)]
        cases.extend((question, 'path', relative, 'file') for question in (symbol, path))
        if semantic and name == 'atlas':
            cases.append(('Why avoid sending sensitive material to servers?', 'body', topics[1][0], 'conversation'))
        for question, field, expected, evidence_type in cases:
            response, _ = run(binary, project, 'brief', '--query', question, *options)
            hit = any(item.get('type') == evidence_type and expected in item.get(field, '')
                      for item in response['evidence'][:3])
            results.append(dict(project=name, question=question, hit=hit, retrieval=response['retrieval']))
        for question in ('xyzunknownunrelated', f'qzxabsentfrom{name}'):
            response, _ = run(binary, project, 'brief', '--query', question, *options)
            results.append(dict(project=name, question=question, abstained=not response['evidence'],
                                retrieval=response['retrieval']))
    known = [case for case in results if 'hit' in case]
    unknown = [case for case in results if 'abstained' in case]
    report = dict(mode='semantic' if semantic else 'keyword', known_cases=len(known), unknown_cases=len(unknown),
                  recall_at_3=sum(case['hit'] for case in known) / len(known),
                  unknown_query_abstained=all(case['abstained'] for case in unknown), cases=results)
    print(json.dumps(report, indent=2))
    if report['recall_at_3'] < 0.9 or (not semantic and not report['unknown_query_abstained']):
        raise SystemExit('Retrieval regression')
    if semantic and any(case['retrieval'] != 'hybrid' for case in results):
        raise SystemExit('Semantic evaluation fell back to keyword retrieval')


def benchmark(binary, scratch, samples, turns):
    project = scratch / 'benchmark'
    project.mkdir()
    run(binary, project, 'init')
    for index in range(160):
        (project / f'module_{index:03}.py').write_text(
            '\n'.join(f'# Fixture module {index} declaration {line}' for line in range(50)), encoding='utf-8')
    (project / 'auth.py').write_text(FILES['atlas'][1], encoding='utf-8')
    run(binary, project, 'index')
    transcript = scratch / 'benchmark.jsonl'
    import_turns(binary, project, transcript, [
        f'Catalog entry {index}: the metadata refresh checked the index and recorded its status.'
        for index in range(turns - 1)
    ] + ['The SessionManager stores session tokens for local authentication.'])
    payload = dict(hook_event_name='UserPromptSubmit', transcript_path=str(transcript),
                   cwd=str(project), prompt='Where is SessionManager used for local authentication?', session_id='benchmark')
    elapsed = []
    stages = []
    for index in range(samples + 1):
        start = time.perf_counter()
        response, timings = run(binary, project, 'hook', payload=payload, profile=True)
        elapsed.append(round((time.perf_counter() - start) * 1000, 2))
        if 'SessionManager' not in response['hookSpecificOutput']['additionalContext']:
            raise SystemExit('Benchmark hook omitted the expected evidence')
        if index and timings:
            stages.append(timings)
    print(json.dumps(dict(turns=turns, files=161, warm_samples=samples, first_hook_ms=elapsed[0],
                          warm_median_ms=round(statistics.median(elapsed[1:]), 2),
                          warm_p95_ms=sorted(elapsed[1:])[math.ceil(samples * 0.95) - 1],
                          warm_stage_median_ms={
                              stage: round(statistics.median(sample[stage] for sample in stages), 2)
                              for stage in stages[0]
                          } if stages else {}), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=BINARY)
    parser.add_argument('--semantic', action='store_true')
    parser.add_argument('--benchmark', action='store_true', help='Time the hook on a larger archive.')
    parser.add_argument('--samples', type=int, default=30)
    parser.add_argument('--turns', type=int, default=10_000)
    args = parser.parse_args()
    if args.samples < 1 or args.turns < 1:
        parser.error('--samples and --turns must be positive')
    with tempfile.TemporaryDirectory(prefix='lore-evaluation-') as directory:
        scratch = Path(directory)
        if args.benchmark:
            benchmark(args.binary.resolve(), scratch, args.samples, args.turns)
        else:
            evaluate(args.binary.resolve(), scratch, args.semantic)


if __name__ == '__main__':
    main()
