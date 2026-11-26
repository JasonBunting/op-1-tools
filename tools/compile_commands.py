#!/usr/bin/env python3
"""Write compile_commands.json at the repository root for clangd (code navigation and diagnostics in editors and
Claude Code). Covers every plugin's sources (plugin.json "sources", C only) and its test/*.c host tests.

clang has no Blackfin target, so files are parsed as 32-bit freestanding C (i686); firmware addresses are plain
integer casts and parse fine, Blackfin inline assembly does not (expect diagnostics there). The SDK comes from
HUNDEKRAFT_KENNEL in .env (a WSL path, mapped to Windows here when run on Windows).

    python tools/compile_commands.py
"""
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def kennel_dir():
    env = ROOT / '.env'
    val = os.environ.get('HUNDEKRAFT_KENNEL')
    if not val and env.is_file():
        for line in env.read_text().splitlines():
            m = re.match(r'\s*HUNDEKRAFT_KENNEL\s*=\s*(.+?)\s*$', line)
            if m:
                val = m.group(1).strip('"\'')
    if not val:
        sys.exit('compile_commands: set HUNDEKRAFT_KENNEL in .env (see .env.example)')
    m = re.match(r'/mnt/([a-z])/(.*)', val)
    if m and os.name == 'nt':
        val = f'{m.group(1).upper()}:/{m.group(2)}'
    return Path(val)


def main():
    sdk = kennel_dir() / 'synth_sdk'
    if not sdk.is_dir():
        sys.exit(f'compile_commands: no synth_sdk under {sdk.parent}')
    base = ['clang', '--target=i686-unknown-none-elf', '-ffreestanding', '-fno-builtin', '-std=gnu11', '-O2', '-Wall']
    entries = []
    plugins = sorted(ROOT.glob('*/plugin/plugin.json')) + sorted(ROOT.glob('research/*/plugin/plugin.json'))
    for pj in plugins:
        pdir = pj.parent
        meta = json.loads(pj.read_text())
        incs = [f'-I{pdir}'] + [f'-I{(pdir / d).resolve()}' for d in meta.get('include_dirs', [])] + [f'-I{sdk}']
        flags = meta.get('cflags', [])
        for src in meta.get('sources', []):
            if not src.endswith('.c'):
                continue
            f = (pdir / src).resolve()
            entries.append({'directory': str(ROOT), 'file': str(f),
                            'arguments': base + flags + meta.get('source_cflags', {}).get(src, []) + incs + ['-c', str(f)]})
        for t in sorted((pdir / 'test').glob('*.c')):          # host tests: plain hosted C (gcc on WSL)
            # the -D flags of the gcc line in the test's header comment; M_PI from the C library on Windows too.
            # Linux-only headers (mirror's sys/mman.h) still show up as errors when clangd runs on Windows.
            head = t.read_text(errors='replace')[:2000]
            defs = sorted(set(re.findall(r'(?<=\s)-D[A-Za-z_]\w*(?:=\S+)?', head)))
            entries.append({'directory': str(ROOT), 'file': str(t),
                            'arguments': ['clang', '-std=gnu11', '-O2', '-Wall', '-D_USE_MATH_DEFINES', *defs,
                                          f'-I{pdir}', f'-I{sdk}', '-c', str(t)]})
    out = ROOT / 'compile_commands.json'
    out.write_text(json.dumps(entries, indent=1) + '\n')
    print(f'{out}: {len(entries)} files')


if __name__ == '__main__':
    main()
