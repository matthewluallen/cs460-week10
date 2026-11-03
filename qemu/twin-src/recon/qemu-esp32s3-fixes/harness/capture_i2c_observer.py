#!/usr/bin/env python3
"""Bounded host-side QEMU transaction capture; no physical instrument claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import time
import uuid

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--qemu', type=Path, required=True)
p.add_argument('--flash', type=Path, required=True)
p.add_argument('--upstream-revision', required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--seconds', type=float, default=15)
a = p.parse_args()
if not 1 <= a.seconds <= 60:
    p.error('seconds must be between 1 and 60')
a.out.mkdir(parents=True, exist_ok=False)
qemu, flash, out = a.qemu.resolve(), a.flash.resolve(), a.out.resolve()
def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()
identity = {'qemu_sha256': sha(qemu), 'flash_sha256': sha(flash)}
cmd = ['unshare', '--net', str(qemu), '-M', 'esp32s3', '-m', '8M', '-snapshot',
       '-drive', f'file={flash},if=mtd,format=raw',
       '-object', 'can-bus,id=canbus0', '-serial', f'file:{out}/uart0.log',
       '-serial', 'null', '-serial', f'file:{out}/console.log',
       '-nographic', '-monitor', 'none', '-no-reboot',
       '-trace', f'enable=esp32_i2c_observe,file={out}/trace.log']
env = dict(os.environ, TBD_W5_BIND='127.0.0.1')
# Dedicated child, private logs, finite per-file bound. Reaching a bound is
# recorded as incomplete acquisition, never as a complete prefix.
def bound():
    resource.setrlimit(resource.RLIMIT_FSIZE, (32*1024*1024, 32*1024*1024))
start = time.monotonic()
with (out/'stderr.log').open('wb') as err, (out/'stdout.log').open('wb') as stdout:
    proc = subprocess.Popen(cmd, env=env, stdout=stdout, stderr=err, preexec_fn=bound)
    reason = 'process-exited'
    try:
        proc.wait(timeout=a.seconds)
    except subprocess.TimeoutExpired:
        reason = 'bounded-window-ended'
    except BaseException as exc:
        reason = 'interrupted-' + type(exc).__name__
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                reason = 'termination-timeout-killed'; proc.kill(); proc.wait()
rx = re.compile(r'esp32_i2c_observe controller=(\S+) path=(\S+) seq=(\d+) epoch=(\d+) virtual_ns=(-?\d+) event=(\w+) address=(-?\d+) direction=(-?\d+) data=(-?\d+) nack=(-?\d+)')
records=[]; unparsed=0; diagnostics=[]
for line in (out/'trace.log').read_text(errors='replace').splitlines() if (out/'trace.log').exists() else []:
    match=rx.search(line)
    if not match:
        if line == 'Adding SPI flash device':
            diagnostics.append(line)
        else:
            unparsed+=1
        continue
    keys=['controller_token','controller_path','sequence','reset_epoch','virtual_ns','event','address','direction','data','nack']
    values=list(match.groups())
    for i in [2,3,4,6,7,8,9]: values[i]=int(values[i])
    records.append(dict(zip(keys,values)))
last={}; gaps=[]; paths={}; tokens={}; identity_errors=[]
for r in records:
    token=r['controller_token']; path=r['controller_path']
    if not path.startswith('/machine/') or paths.get(token,path)!=path or tokens.get(path,token)!=token:
        identity_errors.append({'controller_token':token,'controller_path':path})
    paths[token]=path; tokens[path]=token
    if token in last and r['sequence']!=last[token]+1:
        gaps.append({'controller_token':token,'before':last[token],'after':r['sequence']})
    last[token]=r['sequence']
(out/'transactions.json').write_text(json.dumps(records,indent=2)+'\n')
result={'schema':'qemu-i2c-observation-capture/v1','session':str(uuid.uuid4()),
        'source_kind':'qemu-peripheral-transactions','identity':identity,
        'upstream_revision':a.upstream_revision,'machine':'esp32s3','command':cmd,
        'controller_mapping':paths,
        'identity_errors':identity_errors,
        'identity_scope':'QOM controller path within this captured machine/session; bus role requires pinned machine topology',
        'program_identity':'not independently acquired in this capture',
        'seconds_requested':a.seconds,'host_elapsed_seconds':time.monotonic()-start,
        'termination':reason,'returncode':proc.returncode,'records':len(records),
        'events':sorted({r['event'] for r in records}), 'unparsed_lines':unparsed,
        'sequence_gaps':gaps,'full_capture_completeness':False,
        'trace_diagnostics':diagnostics,
        'firmware_startup_marker_seen':'===== Startup complete =====' in (out/'console.log').read_text(errors='replace'),
        'clock':'QEMU virtual nanoseconds; no wire-edge or physical timing qualification',
        'flash_unchanged':sha(flash)==identity['flash_sha256'],
        'files':{f.name:sha(f) for f in out.iterdir() if f.is_file()}}
(out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'studio-bus-capture.json').write_text(json.dumps({'schema':'studio-qemu-bus/v1','manifest':result,'trace':(out/'trace.log').read_text() if (out/'trace.log').exists() else ''})+'\n')
print(json.dumps(result))
raise SystemExit(0 if records and not identity_errors and not gaps and not unparsed and result['flash_unchanged'] and result['firmware_startup_marker_seen'] and reason=='bounded-window-ended' else 1)
