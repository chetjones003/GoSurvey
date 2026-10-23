"""Fail when parallel Catch2/Qt execution omits, duplicates or skips a discovered case."""
import argparse
from collections import Counter
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def verify_group(root: Path, kind: str) -> dict:
    manifests = sorted(root.glob('shard-*/discovery.json'))
    if not manifests:
        raise ValueError(f'{kind}: no discovery manifests at {root}')
    inventories = [json.loads(path.read_text(encoding='utf-8-sig')) for path in manifests]
    first = inventories[0]
    count = first['count']
    if sorted(item['index'] for item in inventories) != list(range(count)):
        raise ValueError(f'{kind}: missing or duplicate shard indexes')
    expected = first['expected']
    if len(expected) != len(set(expected)):
        raise ValueError(f'{kind}: duplicate discovered names')
    actual = []
    for manifest, inventory in zip(manifests, inventories):
        if inventory['expected'] != expected or inventory['count'] != count:
            raise ValueError(f'{kind}: discovery differs across shards')
        if kind == 'catch':
            result = ET.parse(manifest.parent / 'results.xml').getroot()
            cases = result.findall('TestCase')
            summary = result.find('OverallResultsCases')
            if summary is None:
                raise ValueError('catch: incomplete execution report')
            if int(summary.get('failures', '0')) or int(summary.get('skips', '0')):
                raise ValueError('catch: failed or skipped cases')
            names = [case.get('name') for case in cases]
            for case in cases:
                outcome = case.find('OverallResult')
                if (outcome is None or outcome.get('success') != 'true'
                        or int(outcome.get('skips', '0')) or case.find('.//Skip') is not None):
                    raise ValueError(f'catch: incomplete, failed or skipped {case.get("name")}')
        else:
            result = ET.parse(manifest.parent / 'artifacts/qt-tests.xml').getroot()
            if any(int(result.get(key, '0')) for key in ('failures', 'errors', 'skipped')):
                raise ValueError('qt: failed or skipped cases')
            if any(result.find(f'.//{tag}') is not None for tag in ('failure', 'error', 'skipped')):
                raise ValueError('qt: failed or skipped cases')
            names = [case.get('name') for case in result.findall('testcase')
                     if case.get('name') not in ('initTestCase', 'cleanupTestCase')]
            if Counter(names) != Counter(inventory['selected']):
                raise ValueError('qt: execution does not match this shard selection')
        actual.extend(names)
    if Counter(actual) != Counter(expected):
        missing = list((Counter(expected) - Counter(actual)).elements())
        extra = list((Counter(actual) - Counter(expected)).elements())
        raise ValueError(f'{kind}: missing={missing[:5]}, duplicate/unexpected={extra[:5]}')
    return {'cases': len(expected), 'shards': count, 'passed': True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    args = parser.parse_args()
    if not args.run_id or any(ch not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for ch in args.run_id):
        raise ValueError('Invalid test run identifier')
    report = {kind: verify_group(args.artifacts / folder / args.run_id, kind)
              for kind, folder in [('catch', 'catch'), ('qt', 'qt-editor')]}
    destination = args.artifacts / f'coverage-{args.run_id}.json'
    destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
