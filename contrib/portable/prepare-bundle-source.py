#!/usr/bin/env python3
"""Prepare a writable, asset-free source checkout from a validated bundle."""
import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tarfile

spec = importlib.util.spec_from_file_location('bundle_relink', Path(__file__).with_name('relink-bundle.py'))
relink = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relink)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('bundle', type=Path)
p.add_argument('destination', type=Path)
p.add_argument('--manifest-sha256')
a = p.parse_args()
bundle = a.bundle.resolve()
metadata = relink.validate(bundle, a.manifest_sha256)
destination = a.destination.absolute()
if destination.exists():
    p.error('Use a fresh writable source directory')
shutil.copytree(bundle / 'source/qemu', destination)
dtc = destination / 'subprojects/dtc'
dtc.mkdir(exist_ok=True)
with tarfile.open(bundle / 'source/dependencies/dtc-source.tar') as archive:
    archive.extractall(dtc, filter='data')
recipe = destination / 'contrib/portable/build-engine.py'
original = recipe.read_text()
needle = "'--audio-drv-list=']"
if original.count(needle) != 1:
    raise RuntimeError('Reviewed build recipe no longer matches asset-free preparation')
# This changes installation of unrelated firmware blobs, not selected objects.
recipe.write_text(original.replace(needle, "'--audio-drv-list=','--disable-install-blobs']"))
subprocess.run(['git', 'init', str(destination)], check=True)
subprocess.run(['git', 'add', '.'], cwd=destination, check=True)
subprocess.run(['git', '-c', 'user.name=Builder', '-c', 'user.email=builder@example.invalid',
                'commit', '-m', 'Import asset-free corresponding engine source'], cwd=destination, check=True,
               stdout=subprocess.DEVNULL)
print('Prepared source revision from bundle:', metadata['source_revision'])
print('Local identity:', subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=destination, text=True).strip())
print('Only recipe change: disable installation of unrelated firmware blobs')
