#!/usr/bin/env python3
"""Build the native launcher with the pinned VitaSDK libraries.

Usage: python build.py --sdk /path/to/vitasdk
Produces ELF, VELF, eboot.bin and param.sfo in build/. No VPK is packaged.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from hashlib import sha256
from pathlib import Path
import argparse
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', default=os.environ.get('VITASDK'),
                        help='VitaSDK root; defaults to VITASDK')
    parser.add_argument('--jobs', type=int, default=4,
                        help='parallel compiler jobs (default: 4)')
    parser.add_argument('--check', action='store_true',
                        help='check tools and pinned libraries without compiling')
    args = parser.parse_args()
    if not args.sdk:
        parser.error('set VITASDK or pass --sdk')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    sdk = Path(args.sdk).expanduser().resolve()
    settings = json.loads((ROOT / 'dependencies.json').read_text(encoding='utf-8'))
    out = ROOT / 'build'

    def tool(name):
        candidates = (sdk / 'bin' / name, sdk / 'bin' / (name + '.exe'))
        result = next((p for p in candidates if p.is_file()), None)
        if result is None:
            raise RuntimeError(f'Missing SDK tool: {name}')
        return result

    cc = tool('arm-vita-eabi-gcc')
    ar = tool('arm-vita-eabi-ar')
    elf_create = tool('vita-elf-create')
    make_self = tool('vita-make-fself')
    make_sfo = tool('vita-mksfoex')
    for relative, expected in settings['required_archives'].items():
        archive = sdk / relative
        if not archive.is_file() or sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'This source requires the pinned archive in dependencies.json: {relative}')
    pkgconfig = sdk / 'arm-vita-eabi/lib/pkgconfig/sdl2.pc'
    if not pkgconfig.is_file():
        raise RuntimeError('Missing SDL2 pkg-config metadata in VitaSDK')
    if args.check:
        print('SDK tools and pinned SDL2/VitaGL archives verified.')
        return

    out.mkdir(exist_ok=True)
    backend = settings['required_archives']['arm-vita-eabi/lib/libvitaGL.a']
    (out / 'wb_backend.h').write_text(
        '#define WB_SHADER_CACHE_PATH WB_ROOT "/shaders-v1-' + backend[:16] + '"\n', encoding='ascii')
    env = os.environ.copy()
    env['PATH'] = str(sdk / 'bin') + os.pathsep + env.get('PATH', '')

    def run(command):
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout + result.stderr

    arch = ['-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=softfp']
    includes = ['-I', ROOT / 'src', '-I', ROOT / 'vendor/tlsf',
                '-I', ROOT / 'vendor/so_util', '-I', out,
                '-I', sdk / 'arm-vita-eabi/include']
    common = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
              '-ffp-contract=off', *arch, *includes]
    upstream = ['-std=gnu11', '-O2', '-ffp-contract=off', *arch, *includes]
    commands = []
    adapters = []

    def compile_unit(path, flags, name=None):
        obj = out / (name or (path.stem + '.o'))
        commands.append((path.relative_to(ROOT).as_posix(), [cc, *flags, '-c', path, '-o', obj]))
        return obj

    for path in sorted((ROOT / 'src').glob('*.c')):
        adapters.append(compile_unit(path, common))
    for path in sorted((ROOT / 'src').glob('*.S')):
        adapters.append(compile_unit(path, arch))
    for path in sorted((ROOT / 'installer').glob('*.c')):
        adapters.append(compile_unit(path, [*common, '-std=gnu11',
            '-Wno-unused-function', '-Wno-maybe-uninitialized',
            '-Wno-unused-parameter', '-Wno-empty-body']))
    for relative in ('vendor/fastlz/fastlz.c', 'vendor/tlsf/tlsf.c',
                     'vendor/setup/etc_utils.c', 'vendor/setup/eac_utils.c'):
        adapters.append(compile_unit(ROOT / relative, upstream))
    loader = compile_unit(ROOT / 'vendor/so_util/so_util.c', upstream)
    launcher = compile_unit(ROOT / 'launcher/main.c', common, 'launcher.o')

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(run, command): name for name, command in commands}
        for future in as_completed(futures):
            output = future.result()
            if output.strip():
                print(output.rstrip())
            print('Compiled ' + futures[future], flush=True)

    archive = out / 'libwonderboy_adapters.a'
    archive.unlink(missing_ok=True)
    run([ar, 'rcs', archive, *adapters])
    libs = list(dict.fromkeys(re.findall(r'-l([A-Za-z0-9_]+)', pkgconfig.read_text())))
    libs += ['z', 'pthread', 'kubridge_stub', 'SceLibKernel_stub', 'SceShaccCgExt',
             'taihen_stub', 'SceVshBridge_stub', 'stdc++', 'c', 'm', 'gcc']
    wrappers = ('vglInitExtended', 'invoke_splashscreen', 'clear_splashscreen',
                'sceKernelAllocMemBlock', 'sceGxmEndScene')
    elf = out / 'wonderboy.elf'
    run([cc, *arch, '-Wl,--emit-relocs',
         *['-Wl,--wrap=' + name for name in wrappers], launcher, loader,
         '-Wl,--start-group', archive, *['-l' + name for name in libs],
         '-Wl,--end-group', '-Wl,-Map=' + str(out / 'wonderboy.map'), '-o', elf])
    run([elf_create, '-s', elf, out / 'wonderboy.velf'])
    run([make_self, out / 'wonderboy.velf', out / 'eboot.bin'])
    run([make_sfo, '-s', 'TITLE_ID=' + settings['title_id'], '-s',
         'APP_VER=' + settings['application_version'], '-d', 'ATTRIBUTE=32768',
         '-d', 'ATTRIBUTE2=12', 'Wonder Boy', out / 'param.sfo'])
    print('Native launcher built in ' + str(out))


if __name__ == '__main__':
    main()
