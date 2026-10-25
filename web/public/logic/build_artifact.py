"""Create a verified candidate bundle from a completed private build.

Requires an administrator-owned private directory unchanged during collection.
Pre-open type/symlink checks are not a hostile-filesystem sandbox.
This proves consistency with its build receipt, not authenticity, behavioral
qualification, or permission to install on any device. No flashing is performed.
"""
import hashlib
import io
import json
from pathlib import Path
import struct
import re
import zipfile

TARGET = 'm5stamplc-openplc-generated-v1'


def sha(data):return hashlib.sha256(data).hexdigest()


def candidate(folder, receipt):
    folder=Path(folder).resolve()
    if receipt.get('status')!='built' or receipt.get('target')!=TARGET:
        raise ValueError('A completed matching-target build is required')
    if not re.fullmatch('[a-f0-9]{32}',receipt.get('id','')) or not re.fullmatch('[a-f0-9]{64}',receipt.get('request_sha256','')):
        raise ValueError('Invalid build receipt lineage')
    files={}
    paths={'request.json':('request.json',131072),'project.json':('project.json',131072),
           'program.st':('firmware/authored-program.st',1048576),
           'firmware.bin':('firmware/.pio/build/m5stamplc/firmware.bin',0x330000),
           'firmware.elf':('firmware/.pio/build/m5stamplc/firmware.elf',32*1024*1024)}
    for name,(relative,limit) in paths.items():
        path=folder/relative
        if not path.resolve().is_relative_to(folder) or any(p.is_symlink() for p in [path,*path.parents] if p!=folder and p.is_relative_to(folder)):
            raise ValueError('Artifact must be a regular file in its private build')
        if not path.is_file() or path.stat().st_size>limit:raise ValueError('Artifact size/type mismatch')
        with path.open('rb') as source:data=source.read(limit+1)
        if len(data)>limit:raise ValueError('Artifact exceeds size limit')
        files[name]=data
    request_bytes=files.pop('request.json')
    if sha(request_bytes)!=receipt['request_sha256']:raise ValueError('Request identity mismatch')
    request=json.loads(request_bytes)
    if request.get('target')!=TARGET or request.get('project')!=json.loads(files['project.json']):
        raise ValueError('Request/project lineage mismatch')
    for name,key in [('project.json','project_sha256'),('program.st','source_sha256'),('firmware.bin','application_sha256'),('firmware.elf','elf_sha256')]:
        if sha(files[name])!=receipt.get(key):raise ValueError('Artifact identity mismatch: '+name)
    app=files['firmware.bin'];elf=files['firmware.elf']
    if len(app)<288 or app[0]!=0xE9 or not 1<=app[1]<=16 or struct.unpack_from('<H',app,12)[0]!=9:
        raise ValueError('Expected ESP32-S3 application image')
    # This profile emits esp_app_desc_t first in the first data segment. Refuse
    # a changed layout rather than searching arbitrary bytes for an identity.
    if struct.unpack_from('<I',app,28)[0]<256 or struct.unpack_from('<I',app,32)[0]!=0xABCD5432:
        raise ValueError('Unsupported application descriptor layout')
    if app[176:208].hex()!=sha(elf):raise ValueError('Application descriptor does not identify supplied ELF')
    if not elf.startswith(b'\x7fELF'):raise ValueError('Expected linked ELF image')
    manifest={'schema':'openplc-candidate/v1','target':TARGET,'build_id':receipt['id'],
              'request_sha256':receipt['request_sha256'],
              'files':{name:sha(data) for name,data in files.items()},
              'image_kind':'application-only','chip':'esp32s3','max_application_bytes':0x330000,
              'qualification':'unqualified-candidate','installed':False,
              'limits':'Hashes establish consistency, not authentication. Destination partition/recovery, exact-image QEMU and physical behavior gates remain. No bootloader, partition table or device configuration is included.'}
    files['candidate.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    archive=io.BytesIO()
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as bundle:
        for name,data in files.items():bundle.writestr(name,data)
    return archive.getvalue(),manifest
