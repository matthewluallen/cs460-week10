"""Private build-only job store for the Logic Studio generated OpenPLC target.

Administrator supplies all toolchain paths. No upload or device-control command
exists here. HTTP/session admission belongs to the enclosing classroom service.
"""
from dataclasses import dataclass
from pathlib import Path
import copy
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import threading
import uuid

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'm5stamplc-openplc-generated-v1'


@dataclass(frozen=True)
class Toolchain:
    stage: Path
    qualified: Path
    libdeps: Path
    pio: Path


def request_bytes(request):
    if not isinstance(request, dict) or set(request) != {'target', 'project', 'analogBindings'}:
        raise ValueError('Expected target, project and analogBindings only')
    if request['target'] != TARGET or not isinstance(request['project'], dict):
        raise ValueError('Unsupported target or project')
    bindings = request['analogBindings']
    if not isinstance(bindings, dict) or any(not ((k == 'IW0' and v == 'board-temperature-celsius-x10') or (k in ('IW1','IW2','IW3') and isinstance(v,str) and re.fullmatch(r'process-binding-sha256:[0-9a-f]{64}',v))) for k, v in bindings.items()):
        raise ValueError('Unsupported analog binding')
    encoded = json.dumps(request, allow_nan=False, sort_keys=True, separators=(',', ':')).encode()
    if len(encoded) > 131072:
        raise ValueError('Project exceeds build request limit')
    return encoded


def read_diagnostics(path):
    try:
        if path.stat().st_size > 65536:return []
        value=json.loads(path.read_text())
        if not isinstance(value,list):return []
        result=[]
        for row in value[:40]:
            if (not isinstance(row,dict) or row.get('phase') not in ('project','structured-text') or
                row.get('severity') not in ('error','warning') or not isinstance(row.get('message'),str) or
                len(row['message'])>512):continue
            clean={k:row[k] for k in ('phase','severity','message')}
            if row.get('location_basis')=='generated-target-st' and all(type(row.get(k)) is int and 0<row[k]<=1000000 for k in ('line','column')):
                clean.update({k:row[k] for k in ('line','column','location_basis')})
                elements=row.get('elements',[])
                if isinstance(elements,list):clean['elements']=[v for v in elements[:8] if isinstance(v,str) and len(v)<=128]
            result.append(clean)
        return result
    except (OSError,ValueError,RecursionError):return []


class BuildJobs:
    def __init__(self, root, toolchain, *, limit=16, timeout=600):
        self.root = Path(root).resolve()
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        if any(self.root.iterdir()):
            raise ValueError("Use a fresh build session directory; existing evidence is retained")
        self.toolchain = toolchain
        self.limit = limit
        self.timeout = timeout
        self._lock = threading.Lock()
        self._jobs = {}
        self._active = None

    def submit(self, request):
        # Encode before launching the worker: callers cannot mutate this snapshot.
        encoded = request_bytes(request)
        with self._lock:
            if self._active is not None:
                raise RuntimeError('A build is already running')
            if len(self._jobs) >= self.limit:
                raise RuntimeError('Build session limit reached; retain evidence before starting another session')
            key = uuid.uuid4().hex
            folder = self.root / key
            folder.mkdir(mode=0o700)
            (folder / 'request.json').write_bytes(encoded)
            self._jobs[key] = {'id': key, 'target': TARGET, 'status': 'queued',
                               'request_sha256': hashlib.sha256(encoded).hexdigest(),
                               'device_tested': False, 'installed': False}
            self._active = key
            try:
                self._save(key)
                threading.Thread(target=self._build, args=(key,), daemon=False).start()
            except Exception:
                self._active = None
                self._jobs[key].update(status='failed', error='Build worker could not start')
                self._save(key)
                raise
            return copy.deepcopy(self._jobs[key])

    def get(self, key):
        with self._lock:
            return copy.deepcopy(self._jobs[key])

    def _save(self, key):
        path = self.root / key / 'status.json'
        temp = path.with_suffix('.tmp')
        temp.write_text(json.dumps(self._jobs[key], indent=2) + '\n')
        temp.replace(path)

    def _change(self, key, **fields):
        with self._lock:
            self._jobs[key].update(fields)
            self._save(key)

    def _run(self, argv, log):
        # Timeout kills the owned process group, including compiler descendants.
        with log.open('ab') as output:
            process = subprocess.Popen(argv, stdout=output, stderr=subprocess.STDOUT,
                                       stdin=subprocess.DEVNULL, start_new_session=True)
            try:
                code = process.wait(timeout=self.timeout)
            except BaseException:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                raise
            # A wrapper may exit while compiler children remain. End this owned
            # process group before admitting any subsequent job, on every exit.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            if code:
                raise subprocess.CalledProcessError(code, argv)

    def _build(self, key):
        folder = self.root / key
        try:
            self._change(key, status='preparing')
            request = json.loads((folder / 'request.json').read_bytes())
            project = folder / 'project.json'
            project.write_text(json.dumps(request['project'], sort_keys=True, separators=(',', ':')))
            target = folder / 'firmware'
            tc = self.toolchain
            self._run([sys.executable, str(ROOT / 'firmware/tests/openplc_authored_prepare.py'),
                       '--stage', str(tc.stage), '--qualified', str(tc.qualified),
                       '--libdeps', str(tc.libdeps), '--out', str(target), '--project', str(project),
                       '--analog-bindings', json.dumps(request['analogBindings'])], folder / 'prepare.log')
            self._change(key, status='building')
            self._run([str(tc.pio), 'run', '-d', str(target), '-e', 'm5stamplc'], folder / 'build.log')
            manifest = json.loads((target / 'openplc-authored-preparation.json').read_text())
            if (manifest['target'] != TARGET or
                    manifest['project_sha256'] != hashlib.sha256(project.read_bytes()).hexdigest() or
                    manifest['source_sha256'] != hashlib.sha256((target / 'authored-program.st').read_bytes()).hexdigest()):
                raise ValueError('Build preparation identity mismatch')
            image = target / '.pio/build/m5stamplc/firmware.bin'
            elf = image.with_suffix('.elf')
            diagnostics=read_diagnostics(target/'learner-diagnostics.json')
            self._change(key, status='built', diagnostics=diagnostics, project_sha256=manifest['project_sha256'],
                         source_sha256=manifest['source_sha256'],
                         application_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                         elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                         qualification='Build only; exact-image QEMU and physical acceptance required')
        except Exception as exc:
            # Raw diagnostics stay private; do not expose host paths or tool output.
            (folder / 'failure.txt').write_text(repr(exc) + '\n')
            diagnostics=read_diagnostics(folder/'firmware/learner-diagnostics.json')
            message=next((d['message'] for d in diagnostics if d.get('severity')=='error'), 'Build failed; retained compiler diagnostics require operator review')
            self._change(key, status='failed', error=message, diagnostics=diagnostics)
        finally:
            with self._lock:
                self._active = None
