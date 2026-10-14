#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify linked ANGLE Bionic closure, SONAMEs and 16 KiB LOAD alignment."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--libraries', required=True, type=Path)
parser.add_argument('--readelf', default='readelf')
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
if args.output.exists():
    parser.error('Use a new report path')
# Public NDK libraries provided by the target Android runtime. This permits no
# Linux libc/loader and does not waive alignment checks for packaged objects.
platform = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so',
            'libnativewindow.so', 'libvulkan.so', 'libz.so'}
pending = [f'lib{name}_lighttouch_angle.so' for name in ('EGL', 'GLESv1_CM', 'GLESv2')]
objects = {}
while pending:
    name = pending.pop(0)
    if name in objects or name in platform:
        continue
    path = args.libraries / name
    if not path.is_file():
        parser.exit(2, f'Missing packaged dependency: {name}\n')
    headers = subprocess.check_output([args.readelf, '-W', '-h', str(path)], text=True)
    segments = subprocess.check_output([args.readelf, '-W', '-l', str(path)], text=True)
    dynamic = subprocess.check_output([args.readelf, '-W', '-d', str(path)], text=True)
    if 'ELF64' not in headers or 'Advanced Micro Devices X86-64' not in headers:
        parser.exit(2, f'Unexpected ABI for x86 candidate: {name}\n')
    needed = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', dynamic)
    sonames = re.findall(r'\(SONAME\).*\[([^\]]+)\]', dynamic)
    if sonames != [name]:
        parser.exit(2, f'SONAME mismatch: {name}: {sonames}\n')
    loads = [line.split() for line in segments.splitlines() if line.strip().startswith('LOAD ')]
    if not loads or any(int(row[-1], 16) < 16384 for row in loads):
        parser.exit(2, f'LOAD alignment below 16 KiB: {name}\n')
    if any(int(row[1], 16) % 16384 != int(row[2], 16) % 16384 for row in loads):
        parser.exit(2, f'LOAD offset/address are not congruent at 16 KiB: {name}\n')
    if any(name.startswith(('libc.so.', 'libstdc++', 'ld-linux')) for name in needed):
        parser.exit(2, f'Linux dependency in Bionic candidate: {needed}\n')
    objects[name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                     'soname': sonames[0], 'needed': needed,
                     'load_alignments': [int(row[-1], 16) for row in loads]}
    pending.extend(needed)
record = {'description': 'ELF dependency/alignment check only; actual Android loading, GLES1/GLES2 and ZIP alignment remain required.',
          'objects': objects,
          'platform_dependencies': sorted(set(dep for item in objects.values()
                                              for dep in item['needed'] if dep in platform))}
args.output.write_text(json.dumps(record, indent=2) + '\n')
print(f'PASS x86 Bionic packaged closure: {len(objects)} objects, SONAMEs and 16 KiB LOAD alignment')
