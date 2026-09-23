#!/usr/bin/env python3
"""Negative checks against a freshly compiled real native declaration probe."""
import copy
import importlib.util
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_audit', root / 'tools/build_native_sequoia.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/native-sequoia-contract'
manifest = json.loads(audit.MANIFEST.read_text())
tables = audit.object_vtables(out / 'probe.o')
assert not audit.audit_slots(manifest, tables)['mismatches']
key = '__ZTV29R16Audit_IO80211InfraProtocol'

# Merely landing on pure slots is not enough: two real callbacks interchanged
# must fail, even though the raw runtime table contains pure_virtual for both.
changed = copy.deepcopy(tables)
changed[key][467], changed[key][468] = changed[key][468], changed[key][467]
assert len(audit.audit_slots(manifest, changed)['mismatches']) == 2

# A similarly named callback from an unrelated class must not pass owner removal.
changed = copy.deepcopy(tables)
changed[key][467] = changed[key][467].replace('29R16Audit_IO80211InfraProtocol', '9Unrelated')
assert audit.audit_slots(manifest, changed)['mismatches']

# Inherited ABI changes and missing entries must fail closed.
changed = copy.deepcopy(tables)
controller = '__ZTV26R16Audit_IO80211Controller'
changed[controller][331] = '__ZN19IONetworkController29_RESERVEDIONetworkController6Ev'
assert audit.audit_slots(manifest, changed)['mismatches']
changed = copy.deepcopy(tables)
changed[key].pop()
assert audit.audit_slots(manifest, changed)['mismatches']
assert audit.audit_slots(manifest, {})['mismatches']

# Const qualification is retained when normalizing only the owning class.
assert audit.method_identity('__ZNK1A9getLoggerEv') != audit.method_identity('__ZN1B9getLoggerEv')
print('Native slot audit negative checks passed (swapped pure callbacks, owner, inherited slot, missing table, const).')
