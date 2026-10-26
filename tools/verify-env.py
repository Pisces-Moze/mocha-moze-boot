#!/usr/bin/env python3
"""Validate the actual linked default environment before packaging Mocha U-Boot."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def linked_environment(elf, payload):
    if elf[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', elf)
    if header[2] != 40 or header[4] != 0x80a00000:
        raise ValueError('Expected Mocha ARM32 ELF and load address')
    sections = [struct.unpack_from('<10I', elf, header[6] + i * header[11])
                for i in range(header[12])]
    for table in sections:
        if table[1] != 2:
            continue
        strings = sections[table[6]]
        names = elf[strings[4]:strings[4] + strings[5]]
        for offset in range(table[4], table[4] + table[5], table[9]):
            name, address, size, _, _, section = struct.unpack_from('<IIIBBH', elf, offset)
            if names[name:names.find(b'\0', name)] != b'default_environment':
                continue
            area = sections[section]
            start = area[4] + address - area[3]
            data = elf[start:start + size]
            if len(data) != size or payload[address - header[4]:address - header[4] + size] != data:
                raise ValueError('ELF and packaged payload environments differ')
            return data
    raise ValueError('Linked default_environment symbol not found')


def validate(data, profile):
    if not data.endswith(b'\0\0'):
        raise ValueError('Default environment lacks double-NUL termination')
    if data[data.index(b'\0\0') + 2:].strip(b'\0'):
        raise ValueError('Environment terminates before its final entries')
    values = {}
    for item in data.split(b'\0'):
        if not item:
            continue
        key, separator, value = item.partition(b'=')
        if not separator or not key:
            raise ValueError('Malformed environment entry (empty name): ' + repr(item[:100]))
        if any(byte <= 32 or byte >= 127 for byte in key):
            raise ValueError('Invalid environment name: ' + repr(key))
        values[key.decode('ascii')] = value.decode('ascii')
    if profile == 'ram':
        if 'fastboot usb 0' not in values.get('bootcmd', ''):
            raise ValueError('RAM diagnostics must enter USB Fastboot')
    else:
        for key in ['bootcmd', 'sample_power_key', 'boot_debian']:
            if not values.get(key):
                raise ValueError('Missing boot environment variable: ' + key)
        for key in ['sample_power_key', 'boot_debian']:
            if 'run ' + key not in values['bootcmd']:
                raise ValueError('Autoboot does not call ' + key)
        kernel = '/boot/uImage-moze-log-candidate' if profile == 'animation' else '/boot/uImage-desktop'
        if kernel not in values['boot_debian']:
            raise ValueError('Wrong kernel path for selected profile')
        initramfs = '/boot/initramfs-moze-log.cpio.gz' if profile == 'animation' else '/boot/initramfs.cpio.gz'
        if initramfs not in values['boot_debian']:
            raise ValueError('Wrong initramfs path for selected profile')
    return {'profile': profile, 'entries': len(values), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(), 'valid': True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('payload', type=Path)
    parser.add_argument('profile', choices=['emmc', 'ram', 'animation'])
    args = parser.parse_args()
    try:
        print(json.dumps(validate(linked_environment(args.elf.read_bytes(), args.payload.read_bytes()), args.profile)))
    except (ValueError, IndexError, struct.error, UnicodeError) as error:
        raise SystemExit('Invalid linked boot environment: ' + str(error))
