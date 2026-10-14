#!/usr/bin/env python3
"""Relink Ninja's engine objects, preserving exactly its target dependencies.

Usage: make-shared.py BUILD [--output PATH]
No shell evaluation; generated additions stay in the private build directory.
Existing macOS entry/control/audio implementation is reused unchanged, with
only Mach-O identity replaced by an embedded source revision for ELF targets.
"""
import argparse, json, pathlib, shlex, subprocess, hashlib
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path)
a=p.parse_args();build=a.build.resolve();src=pathlib.Path(__file__).resolve().parents[2]
output=a.output.resolve() if a.output else build/'libqemu-arm.so'
commands=json.loads((build/'compile_commands.json').read_text())
entry=next(c for c in commands if c['file'].endswith('/system/main.c'))
compile_args=shlex.split(entry['command']); compiler=compile_args[:1]
base=[];skip=False
for arg in compile_args[1:]:
 if skip:skip=False;continue
 if arg in ('-o','-MF','-MQ','-MT'):skip=True;continue
 if arg in ('-c','-MD','-MMD') or arg.endswith('system/main.c'):continue
 base.append(arg)
base.extend(['-iquote', str(src/'target/arm'),'-iquote',str(build/'arm-softmmu'),'-fPIC','-I'+str(src/'contrib/ios-app'),'-I'+str(src/'contrib/macos-app')])
extras=(src/'contrib/macos-app/qemu-macos-extras.c').read_text().replace('#include <mach-o/loader.h>','')
extras=extras.replace('fn == qmp_system_reset || fn == qmp_quit || fn == qmp_system_powerdown', 'fn == qmp_stop || fn == qmp_system_reset || fn == qmp_quit || fn == qmp_system_powerdown')
start=extras.index('const char *qemu_ios_build_id(void)');end=extras.index('/* Guest output capture',start)
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=src,text=True).strip()
source_status=subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],cwd=src,text=True)
dirty=bool(source_status)
identity=revision+('-dirty' if dirty else '')
extras=extras[:start]+f'const char *qemu_ios_build_id(void) {{ return "{identity}"; }}\n\n'+extras[end:]
(build/'qemu-portable-extras.c').write_text(extras)
objects=[]
for source in [src/'contrib/ios-app/qemu-ios-entry.c',src/'contrib/ios-app/qemu-ios-ui.c',build/'qemu-portable-extras.c']:
 obj=build/(source.stem+'.o'); objects.append(str(obj))
 subprocess.run(compiler+base+['-c',str(source),'-o',str(obj)],cwd=build,check=True)
subprocess.run(['ninja','-j2','-d','keeprsp','qemu-system-arm'],cwd=build,check=True)
link=shlex.split(subprocess.check_output(['ninja','-t','commands','qemu-system-arm'],cwd=build,text=True).strip().splitlines()[-1])
expanded=[]
for arg in link:
 if arg.startswith('@'):
  expanded.extend(shlex.split((build/arg[1:]).read_text()))
 else: expanded.append(arg)
args=[];skip=False
for arg in expanded:
 if skip:skip=False;continue
 if arg=='-o':skip=True;continue
 if arg.endswith('system_main.c.o'):args.extend(objects);continue
 if arg in ('-pie','-Wl,-pie') or arg.startswith('-Wl,--dynamic-list='):continue
 args.append(arg)
(build/'portable.exports').write_text('{ global: qemu_ios_*; local: *; };\n')
args+=['-shared','-Wl,-z,defs','-Wl,-z,max-page-size=16384','-Wl,--version-script='+str(build/'portable.exports'),'-Wl,-soname,libqemu-arm.so','-o',str(output)]
(build/'portable-link.json').write_text(json.dumps(args,indent=2)+'\n')
subprocess.run(args,cwd=build,check=True)
tracked=subprocess.check_output(['git','ls-files','-z'],cwd=src).decode().split('\0')
source_inputs={name:hashlib.sha256((src/name).read_bytes()).hexdigest() for name in tracked if name and (src/name).is_file()}
link_inputs={}
for arg in args:
 candidate=pathlib.Path(arg)
 if candidate.suffix in ['.o','.a'] or '.so' in candidate.name:
  if not candidate.is_absolute():candidate=build/candidate
  if candidate.is_file():link_inputs[str(candidate)]=hashlib.sha256(candidate.read_bytes()).hexdigest()
library_dirs=[]
for index,arg in enumerate(args):
 if arg=='-L':library_dirs.append(pathlib.Path(args[index+1]))
 elif arg.startswith('-L'):library_dirs.append(pathlib.Path(arg[2:]))
resolved_libraries={}; static=False
for arg in args:
 if arg in ['-static','-Wl,-Bstatic']:static=True
 elif arg=='-Wl,-Bdynamic':static=False
 if not arg.startswith('-l'):continue
 names=[arg[3:]] if arg.startswith('-l:') else ([f'lib{arg[2:]}.a'] if static else [f'lib{arg[2:]}.so',f'lib{arg[2:]}.a'])
 candidate=None
 for directory in library_dirs:
  directory=directory if directory.is_absolute() else build/directory
  candidate=next((directory/name for name in names if (directory/name).is_file()),None)
  if candidate:break
 if candidate is None:
  for name in names:
   path=pathlib.Path(subprocess.check_output(compiler+['-print-file-name='+name],cwd=build,text=True).strip())
   if path.is_file():candidate=path;break
 if candidate is None:raise RuntimeError('Cannot record resolved library input '+arg)
 candidate=candidate.resolve()
 resolved_libraries[arg]=str(candidate)
 link_inputs[str(candidate)]=hashlib.sha256(candidate.read_bytes()).hexdigest()
manifest={'schema':1,'source_status':source_status,'source_inputs':source_inputs,'link_inputs':link_inputs,'resolved_libraries':resolved_libraries,'tools':{'compiler':subprocess.check_output(compiler+['--version'],cwd=build,text=True).splitlines()[0],'ninja':subprocess.check_output(['ninja','--version'],text=True).strip()},'source_revision':revision,'source_dirty':dirty,'api_major':2,'portable_version':1,'library_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'link_argv':args,'inputs':{str(path.relative_to(src)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [src/'contrib/ios-app/qemu-ios-ui.h',src/'contrib/ios-app/qemu-ios-ui.c',src/'contrib/macos-app/qemu-macos-extras.c',src/'contrib/portable/dependencies.json']}}
(build/'portable-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(output)
