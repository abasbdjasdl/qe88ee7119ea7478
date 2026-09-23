#!/usr/bin/env python3
"""Run the shared runtime-identity body tests using loader-owned dependency fakes."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('native_dependency_identity_test.py')),run_name='__main__')
