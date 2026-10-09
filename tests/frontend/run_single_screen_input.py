#!/usr/bin/env python3
"""Run frontend integration tests against an existing Linux Ninja development build.

Usage: python3 tests/frontend/run_single_screen_input.py build/cloud
Requires compile_commands.json, GNU objcopy, Qt/SDL runtime libraries, and an X
session (or xvfb-run). Reuses production objects; source files are not modified.
"""
import json
from pathlib import Path
import shlex
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1] if len(sys.argv) > 1 else repo / 'build/cloud').resolve()
subprocess.run(['cmake', '--build', str(build), '--target', 'melonDS', '--parallel', '5'], check=True)
commands = json.loads((build / 'compile_commands.json').read_text())
entry = next(item for item in commands if item['file'].endswith('/qt_sdl/main.cpp'))
args = entry.get('arguments') or shlex.split(entry['command'])
output = args[args.index('-o') + 1]
test_dir = build / 'single-screen-tests'
test_dir.mkdir(exist_ok=True)
test_object = test_dir / 'input.o'
args[args.index('-o') + 1] = str(test_object)
args[args.index('-c') + 1] = str(repo / 'tests/frontend/single_screen_input.cpp')
subprocess.run(args, cwd=entry['directory'], check=True)
original_main = Path(entry['directory']) / output
renamed_main = test_dir / 'application-main.o'
subprocess.run(['objcopy', '--redefine-sym', 'main=melonDSApplicationMain', str(original_main), str(renamed_main)], check=True)
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'melonDS'], text=True).splitlines()
link = shlex.split(lines[-1])
# CMake's Ninja linker rule wraps the compiler command in ': && ... && :'.
if link[:2] == [':', '&&']:
    link = link[2:]
if link[-2:] == ['&&', ':']:
    link = link[:-2]
link[link.index('-o') + 1] = str(test_dir / 'input-test')
link = [str(renamed_main) if (build / arg).resolve() == original_main.resolve() else arg for arg in link]
link.insert(link.index('-o'), str(test_object))
subprocess.run(link, cwd=build, check=True)
for mode in ([], ['--dsi']):
    subprocess.run([str(test_dir / 'input-test'), *mode], cwd=build, check=True, timeout=60)
