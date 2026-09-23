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


def with_session_link_callback(source: str) -> str:
    """Redirect two pinned net80211 link notifications to the current owner.

    The upstream _ifnet controller slot is IOEthernetController-typed. The
    native IO80211 owner must be allowed to leave it null, while the working
    Ethernet owner still publishes exactly its old medium/status combination.
    No frame, state-machine or authentication logic is changed here.
    """
    anchor = '#include <net80211/ieee80211_priv.h>\n'
    up = 'ifp->controller->setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, ifp->controller->getCurrentMedium());'
    down = 'ifp->controller->setLinkStatus(kIONetworkLinkValid);'
    if source.count(anchor) != 1 or source.count(up) != 1 or source.count(down) != 1:
        raise ValueError('Pinned net80211 link-status sites changed; review before compiling')
    source = source.replace(anchor, anchor +
                            'extern void r16_net80211_link_status(struct _ifnet *, bool);\n', 1)
    source = source.replace(up, 'r16_net80211_link_status(ifp, true);', 1)
    source = source.replace(down, 'r16_net80211_link_status(ifp, false);', 1)
    return source
