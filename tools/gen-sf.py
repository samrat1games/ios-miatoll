#!/usr/bin/env python3
# kept for older notes: the SF look is now tools/gen-theme.py sf
import os, subprocess, sys
sys.exit(subprocess.call([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gen-theme.py'), 'sf']))
