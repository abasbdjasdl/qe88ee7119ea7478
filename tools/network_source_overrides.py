"""Auditable overrides of the pinned protocol sources; no cryptographic changes."""
def without_tkip_key_logging(source: str) -> str:
    lines = source.splitlines(keepends=True)
    removed = []
    kept = []
    for line in lines:
        if line.lstrip().startswith('XYLog(') and any(x in line for x in ('k->k_key[', 'ctx->rxmic[', 'ctx->txmic[')):
            if not line.rstrip().endswith(');'):
                raise ValueError('Unexpected multiline key log: review pinned source')
            removed.append(line)
        else:
            kept.append(line)
    if len(removed) != 2:
        raise ValueError('Pinned TKIP key-log sites changed; review before compiling')
    return ''.join(kept)
