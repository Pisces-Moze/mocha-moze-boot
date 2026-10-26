#!/usr/bin/env python3
"""Add the verified Mocha backlight to an independent animation-test initramfs."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path


def unpack(data):
    entries = []
    offset = 0
    while offset + 110 <= len(data):
        header = data[offset:offset + 110]
        if header[:6] != b'070701':
            raise ValueError('Expected newc initramfs')
        fields = [int(header[6 + i * 8:14 + i * 8], 16) for i in range(13)]
        end = offset + 110 + fields[11]
        if fields[11] < 1 or end > len(data) or data[end - 1] != 0:
            raise ValueError('Invalid archive name')
        name = data[offset + 110:end - 1].decode('utf-8')
        start = (end + 3) & ~3
        content = data[start:start + fields[6]]
        if len(content) != fields[6]:
            raise ValueError('Truncated archive entry')
        if name == 'TRAILER!!!':
            if data[(start + fields[6] + 3) & ~3:].strip(b'\0'):
                raise ValueError('Unexpected data after archive trailer')
            return entries
        entries.append((name, fields, content))
        offset = (start + fields[6] + 3) & ~3
    raise ValueError('Archive trailer missing')


def pack(entries):
    output = bytearray()
    trailer = [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 11, 0]
    for name, original, content in entries + [('TRAILER!!!', trailer, b'')]:
        fields = list(original)
        encoded = name.encode('utf-8') + b'\0'
        fields[6], fields[11] = len(content), len(encoded)
        output.extend(b'070701' + ''.join(f'{v:08x}' for v in fields).encode('ascii'))
        output.extend(encoded)
        output.extend(bytes(-len(output) % 4))
        output.extend(content)
        output.extend(bytes(-len(output) % 4))
    output.extend(bytes(-len(output) % 512))
    return bytes(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('module', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--base-sha256', required=True)
    parser.add_argument('--module-sha256', required=True)
    args = parser.parse_args()
    base, module = args.base.read_bytes(), args.module.read_bytes()
    if hashlib.sha256(base).hexdigest() != args.base_sha256 or hashlib.sha256(module).hexdigest() != args.module_sha256:
        raise SystemExit('Input hash mismatch')
    if b'vermagic=6.12.111-moze.1-native-vde2 ' not in module or b'name=mocha_miui_backlight\0' not in module:
        raise SystemExit('Unexpected backlight module identity')
    entries = unpack(gzip.decompress(base))
    names = [name for name, _, _ in entries]
    if len(names) != len(set(names)) or names.count('init') != 1 or 'mocha-display-backlight.ko' in names:
        raise SystemExit('Unexpected base archive entries')
    updated = []
    early = b'''# The panel depends on this backlight; load it before systemd/GPU startup.
if [ "$(uname -r)" = "6.12.111-moze.1-native-vde2" ]; then
    if /bin/busybox insmod /mocha-display-backlight.ko; then
        say 'Early panel backlight loaded.'
    else
        say 'Early backlight unavailable; retaining normal userspace retry.'
    fi
fi
'''
    marker = b"say 'Loading Debian from internal eMMC APP partition.'"
    for name, fields, content in entries:
        if name == 'init':
            if content.count(marker) != 1:
                raise SystemExit('Unknown base init script')
            content = content.replace(marker, early + marker, 1)
            fields = list(fields)
            fields[6] = len(content)
        updated.append((name, fields, content))
    updated.append(('mocha-display-backlight.ko', [len(entries) + 1, 0o100644, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], module))
    packed = pack(updated)
    check = unpack(packed)
    if [(n, f, c) for n, f, c in check if n != 'mocha-display-backlight.ko'] != updated[:-1]:
        raise SystemExit('Archive round-trip mismatch')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result = gzip.compress(packed, compresslevel=6, mtime=0)
    args.output.write_bytes(result)
    meta = {'bytes': len(result), 'sha256': hashlib.sha256(result).hexdigest(),
            'base_sha256': args.base_sha256, 'module_sha256': args.module_sha256,
            'early_backlight': True, 'existing_entries_preserved_except_init': True}
    args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta))
