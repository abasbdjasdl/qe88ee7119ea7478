"""Verify pinned, unmodified firmware fixture and its accompanying license."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "firmware"
manifest = json.loads((root / "provenance.json").read_text())
for item in manifest["files"]:
    path = root / item["name"]
    raw = path.read_bytes()
    if len(raw) != item["bytes"] or hashlib.sha256(raw).hexdigest() != item["sha256"]:
        raise SystemExit(f"Pinned fixture mismatch: {path.name}")
print("PASS: pinned unmodified firmware and license SHA-256")
