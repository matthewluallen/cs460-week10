"""Read-only operator inspection of retained, private build evidence.

Run after stopping the build service and confirming its compiler processes have
ended. Does not resume jobs, authenticate learners, install or download firmware.
The administrator owns the directory and keeps it unchanged during inspection.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from student.logic.build_artifact import candidate
from student.logic.build_jobs import TARGET, request_bytes


def read_json(folder, name, limit):
    path = folder / name
    if path.is_symlink() or not path.is_file():
        raise ValueError('Missing or nonregular ' + name)
    with path.open('rb') as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Oversized ' + name)
    return json.loads(data), data


def inspect_job(root, key):
    if not isinstance(key, str) or not re.fullmatch('[a-f0-9]{32}', key):
        raise ValueError('Expected a build identifier')
    folder = Path(root).resolve() / key
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError('Expected a private build directory')
    report = {'id': key, 'target': TARGET, 'evidence': 'invalid',
              'process_state': 'not-observed', 'restartable': False,
              'device_qualification': 'not-established',
              'learner_access': 'not-restored'}
    try:
        receipt, _ = read_json(folder, 'status.json', 65536)
        request, raw = read_json(folder, 'request.json', 131072)
        request_bytes(request)  # Reapply current target/schema admission.
        if (not isinstance(receipt, dict) or receipt.get('id') != key or
                receipt.get('target') != TARGET or
                receipt.get('request_sha256') != hashlib.sha256(raw).hexdigest()):
            raise ValueError('Receipt/request identity mismatch')
        status = receipt.get('status')
        if status not in ('queued', 'preparing', 'building', 'built', 'failed'):
            raise ValueError('Unknown retained status')
        report['recorded_status'] = status
        if status == 'built':
            _, manifest = candidate(folder, receipt)
            report.update(evidence='consistent-built-candidate', files=manifest['files'],
                          next_action='Preserve candidate; exact-image device qualification and destination/recovery gates still apply')
        elif status == 'failed':
            report.update(evidence='recorded-failure',
                          next_action='Inspect private failure and compiler logs; correct cause before explicitly creating a new build')
        else:
            report.update(evidence='unresolved-nonterminal-record',
                          next_action='Inspect the original service and compiler processes; this receipt alone cannot establish termination or authorize a restart')
    except (OSError, ValueError, TypeError, KeyError, AttributeError, RecursionError):
        # No raw diagnostics/paths or attacker-controlled receipt text in report.
        report['next_action'] = 'Retain all evidence and inspect locally; identity or artifact verification failed'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--job', required=True)
    args = parser.parse_args()
    try:
        report = inspect_job(args.root, args.job)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2))
    return 1 if report['evidence'] == 'invalid' else 0


if __name__ == '__main__':
    raise SystemExit(main())
