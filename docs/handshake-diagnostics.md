# Handshake diagnostics (0.1.18)

These numeric properties distinguish protocol progress from network success.
They contain no addresses, SSIDs, frame payloads or key material.

- RequestInit/Scan/Auth/Assoc/Run count requested net80211 transitions, including
  requests subsequently rejected. LastRequest packs old state in bits 40..47,
  requested state in bits 32..39 and the signed argument's uint32 representation
  in bits 0..31. These are not accepted-transition counts.
- RunCommitted counts successful calls to the saved RUN handler which leave
  net80211 in RUN. PortAuthorizations counts actual station port authorization.
  Neither proves DHCP or connectivity.
- RxBridgeOk means the frame reached ieee80211_input, which returns void; it
  does not imply acceptance by the protocol. RxBridgeError/LastError record the
  bridge return code, while the existing net80211 counters record protocol drops.
- EapolKey/Replay/BadMic, Deauth/Disassoc, Supplicant/PortValid/RsnEnabled and
  the selected Rx/Tx/CCMP counters are existing protocol statistics/state.
- DataDrained/PendingTx/ActionDeferred distinguish waiting for TX ownership
  release from an executing hardware station action. TxPrepareErrors/LastError
  exclude ordinary empty-queue EAGAIN.

Values are sampled once per second; separate registry properties are not an
atomic transaction. LastRequest and cumulative counts complement the final
state, but do not constitute a full event trace. Capture still verifies link,
IPv4 and an interface-bound HTTPS request independently.

## 0.1.19 RX boundary

Effective hardware decryption follows pinned rtw89 core.c
rtw89_core_update_rx_status: HW_DEC && !SW_DEC && !ICV_ERR. The previous
bridge rejected HW_DEC even when SW_DEC requested software processing.
CRC/ICV errors still fail descriptor decoding; genuinely hardware-decrypted
frames still fail the bridge. No decrypted/replay-verified flags are supplied
to net80211. This source mismatch is confirmed; causation of missing EAPOL
on the tested machine remains unproven.

New counts classify wireless management/control/data/reserved frames, effective
hardware decryption, HW_DEC+SW_DEC fallback, and clear local-addressed EAPOL
LLC candidates before gating and at the bridge. Candidates do not prove valid
EAPOL-Key messages. No payload/key/address is saved. Last clear local-addressed
deauthentication reason is numeric and does not authenticate the sender.

## 0.1.20 EAPOL bridge boundary

EapolBridgeOk/Failed separate candidate-specific return codes from unrelated
control-frame rejections. Stage maps to:1 attachment,2 decode,3 crypto/type,
4 minimum length,5 frame type,6 version,7 channel,8 allocation,9 copy,10 pullup,
11 node,12 input call,13 returned from input. Stage13 is not acceptance.
Lengths packs mbuf packet length low32 and first segment length high32.
Header packs FC0/FC1/fragment number/protocol state in consecutive bytes.
Envelope packs EAPOL type low16 (256 means missing), advertised body length
bits16..31, and RX frame length high32. No payload/key/nonce/MAC is recorded.
DropMask records per-candidate statistic changes: bits0..9 short,wrongdir,
wrongbss,duplicate,nowep,unencrypted,decap,unauth,eapol_key,nombuf. Last-candidate
values do not describe every prior packet.

The native packet bridge is exercised with synthetic EAPOL and a fake mbuf
implementation for bytes, FCS removal, lengths, crypto filtering and cleanup.
This does not execute the real macOS kernel or the net80211 input state machine.

## 0.1.21 contiguous receive allocation

Hardware0.1.20 observed EapolBridgeOk121/Failed0, stage13, last packet length131
and first length60, FC0=8/FC1=2/fragment0/protocolRUN, EAPOL-Key type3/body95.
The last packet increments only is_rx_nombuf (DropMask512). The input PAE code
requests a99-byte key header after decapsulation. XNU m_allocpacket_internal
may split small unrestricted allocations; m_pullup has plain-mbuf size limits.

RX now requests maxchunks=1 and verifies first and packet lengths after copy.
No fallback delivers fragmented buffers. Allocation failure still drops safely.
A60-byte first-segment model reproduces the previous bridge test failure; the
fixed native bridge passes with131-byte contiguous data, plus28/1514/2346 sizes.
This model is source-informed; actual hardware handshake remains to be tested.
Stage10 now means contiguous-length validation rather than header pullup.

References (Apple source, accessed2026-09-22):
https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/uipc_mbuf.c
https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kpi_mbuf.c
