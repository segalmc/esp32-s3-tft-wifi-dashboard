#!/usr/bin/env python3
"""Build/run with the central age vault; no uploads or device operations."""
from pathlib import Path
import os, subprocess, sys
project = Path(__file__).resolve().parents[1]
tools = Path(os.environ.get('HOT_WATER_TOOLS_ROOT', str(Path.home() / 'Projects/hot-water-tools')))
options = ['--allow-noninteractive'] if '--allow-noninteractive' in sys.argv[1:] else []
command = [a for a in sys.argv[1:] if a != '--allow-noninteractive']
if command[:1] == ['--']: command = command[1:]
command = command or ['pio', 'run', '-e', 'esp32-s3-n16r8-tft']
raise SystemExit(subprocess.run([sys.executable, str(tools / 'hwt.py'), *options, '--profile', 's3', '--project', str(project), '--', *command], cwd=project).returncode)
