#!/usr/bin/env python3
"""Build a reusable QEMU TCG engine; one worker process per qemu_ios_main."""
import argparse,pathlib,os,subprocess
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('--abi',choices=['linux','arm64-v8a','x86_64'],default='linux');p.add_argument('--prefix',type=pathlib.Path,required=True);p.add_argument('--ndk',type=pathlib.Path);p.add_argument('--configure-only',action='store_true');a=p.parse_args()
src=pathlib.Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);prefix=a.prefix.resolve();env=os.environ.copy()
env['PKG_CONFIG_PATH']=str(prefix/'lib/pkgconfig')
if a.abi!='linux': env['PKG_CONFIG_LIBDIR']=env['PKG_CONFIG_PATH']
pc=out/'target-pkg-config'
pc.write_text('#!/bin/sh\nexec pkg-config --static "$@"\n');pc.chmod(0o755);env['PKG_CONFIG']=str(pc)
args=[src/'configure','--target-list=arm-softmmu','--without-default-features','--without-default-devices','--with-devices-arm=portable','--enable-tcg','--enable-pixman','--disable-docs','--disable-tools','--disable-werror','--disable-pie','--extra-cflags=-fPIC','--extra-ldflags=-L'+str(prefix/'lib')+' -lcrypto','--audio-drv-list=']
if a.abi!='linux':
 if not a.ndk:raise RuntimeError('--ndk required')
 bin=a.ndk.resolve()/'toolchains/llvm/prebuilt/linux-x86_64/bin';arch='aarch64' if a.abi=='arm64-v8a' else 'x86_64';triple='aarch64-linux-android' if arch=='aarch64' else 'x86_64-linux-android'
 env['PATH']=str(bin)+':'+env['PATH']
 for key,name in [('AR','llvm-ar'),('RANLIB','llvm-ranlib'),('NM','llvm-nm'),('STRIP','llvm-strip'),('READELF','llvm-readelf')]:env[key]=str(bin/name)
 args+=['--cpu='+arch,'--cc='+str(bin/(triple+'28-clang')),'--cxx='+str(bin/(triple+'28-clang++')),'--disable-containers']
# Build scripts use explicit argv, never shell evaluation. No private assets.
print(' '.join(map(str,args)),flush=True);subprocess.run(list(map(str,args)),cwd=out,env=env,check=True)
if not a.configure_only:
 subprocess.run(['ninja','-j2','-d','keeprsp','qemu-system-arm'],cwd=out,env=env,check=True)
 subprocess.run(['python3',str(src/'contrib/portable/make-shared.py'),str(out)],cwd=out,env=env,check=True)
