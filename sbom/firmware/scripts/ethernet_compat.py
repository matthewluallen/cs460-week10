"""Version-checked Ethernet_Generic 2.8.1 write-result correction.

The pinned implementation unconditionally sets an error and returns0 after its
send loop, even when all bytes reached socketSend. Preserve its actual byte
count, including partial failures, so clients can enforce response delivery.
"""
from pathlib import Path
import hashlib

ORIGINAL = '0d6747917b4105438bb496ba7b154d7732bb30a0543131d79b17718b25a7deaa'
PATCHED = '407ffa7a9379c6a9858b24bef5a032b8f0bc70fa2c806e271f5114daf3151003'
BEFORE = b'''  setWriteError();

  return 0;
}

////////////////////////////////////////

int EthernetClient::available()'''
AFTER = b'''  if (totalBytesSent < size) setWriteError();

  return totalBytesSent;
}

////////////////////////////////////////

int EthernetClient::available()'''

def apply(path):
    data=path.read_bytes();digest=hashlib.sha256(data).hexdigest()
    if digest==PATCHED:return
    if digest!=ORIGINAL:raise RuntimeError('Unrecognized Ethernet_Generic 2.8.1 write implementation')
    assert data.count(BEFORE)==1
    data=data.replace(BEFORE,AFTER)
    assert hashlib.sha256(data).hexdigest()==PATCHED
    path.write_bytes(data)
    print('TBD: corrected pinned Ethernet_Generic write byte count')

if 'Import' in globals():
    Import('env')
    apply(Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'Ethernet_Generic/src/EthernetClient_Impl.h')
