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
