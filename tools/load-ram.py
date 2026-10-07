#!/usr/bin/env python3
"""RAM-only, padded Fastboot blocks for verified Mocha U-Boot2026.07."""
import argparse,subprocess,tempfile,hashlib,struct,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--fastboot',default='fastboot')
for name in ['kernel','dtb','initrd']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();base=[a.fastboot,'-s',a.serial]
def run(args,disconnect=False):
 try:r=subprocess.run(base+args,capture_output=True,timeout=45)
 except subprocess.TimeoutExpired:
  if disconnect:return
  raise
 output=(r.stdout+r.stderr).decode(errors='replace')
 if r.returncode and not disconnect:raise SystemExit('Fastboot operation failed; return to stock Fastboot and retry')
 return output
v=run(['getvar','version-bootloader']);prod=run(['getvar','product'])
if 'U-Boot' not in v or '2026.07' not in v or 'mocha' not in prod.lower():raise SystemExit('Expected Mocha U-Boot2026.07')
def cmd(s,disconnect=False):
 if len(('oem run:'+s).encode())>64:raise ValueError('Fastboot wire command too long')
 return run(['oem','run:'+s],disconnect)
k=a.kernel.read_bytes();h=bytearray(k[:64])
magic,crc,_,size,load,entry,data_crc=struct.unpack('>7I',h[:28]);h[4:8]=bytes(4)
if (magic,load,entry)!=(0x27051956,0x80008000,0x80008000) or size!=len(k)-64 or zlib.crc32(h)!=crc or zlib.crc32(k[64:])!=data_crc:raise SystemExit('Invalid uncompressed uImage CRC/address')
if k[28:32]!=bytes((5,2,2,0)):raise SystemExit('Expected uncompressed ARM Linux uImage')
for path,addr,limit in [(a.kernel,0x80007fc0,0x88000000),(a.initrd,0x88000000,0x8c000000),(a.dtb,0x8c000000,0x8d000000)]:
 data=path.read_bytes()
 if not data or addr+len(data)>=limit:raise SystemExit('RAM image overlap or empty artifact')
 with tempfile.TemporaryDirectory() as d:
  block=Path(d)/'chunk.bin'
  for off in range(0,len(data),1024*1024):
   chunk=data[off:off+1024*1024];block.write_bytes(chunk.ljust(1024*1024,b'\0'))
   run(['stage',str(block)]);cmd(f'cp.b 91000000 {addr+off:x} {len(chunk):x}')
 print(path.name,len(data),hashlib.sha256(data).hexdigest(),flush=True)
cmd('setenv fdt_high ffffffff');cmd('setenv initrd_high ffffffff')
cmd('setenv bootargs rdinit=/init mem=1700M maxcpus=4')
for arg in ['root=/dev/ram0','console=ttyS0,115200n8','console=tty0','panic=30','clk_ignore_unused','regulator_ignore_unused','earlycon=mochafb']:
 cmd('setenv bootargs ${bootargs} '+arg)
cmd('fdt addr 8c000000');cmd('fdt resize 10000')
cmd(f'bootm 80007fc0 88000000:{a.initrd.stat().st_size:x} 8c000000',True)
print('RAM boot dispatched; observe actual display and USB before any storage write.')
