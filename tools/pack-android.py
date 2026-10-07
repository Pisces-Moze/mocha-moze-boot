#!/usr/bin/env python3
"""Pack the tested Mocha legacy Android header; never use a private backup."""
import argparse,struct,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('payload',type=Path);p.add_argument('elf',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
elf=a.elf.read_bytes();data=a.payload.read_bytes()
if elf[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',elf,18)[0]!=40 or struct.unpack_from('<I',elf,24)[0]!=0x80a00000:raise SystemExit('Expected ARM32 ELF entry 0x80a00000')
if not 128*1024<len(data)<8*1024*1024:raise SystemExit('Unexpected payload size')
h=hashlib.sha1()
for part in (data,b'',b''):h.update(part);h.update(struct.pack('<I',len(part)))
header=struct.pack('<8s10I16s512s32s',b'ANDROID!',len(data),0x10008000,0,0x12000000,0,0x10f00000,0x10000100,2048,0,0,b'Mocha-Moze-UBoot',b'',h.digest().ljust(32,b'\0'))
pad=lambda b:b+bytes(-len(b)%2048)
result=pad(header)+pad(data);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(result)
meta=dict(bytes=len(result),sha256=hashlib.sha256(result).hexdigest(),entry='0x80a00000',page=2048)
a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
