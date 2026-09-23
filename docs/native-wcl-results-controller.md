# Native WCL result drafts in the hardware controller

`R16NetworkController` now keeps a decoded, undirected WCL scan request only
after `beginNativeWclScanRequest` admits the first real station dwell. The
request is paired with the foreground scan's epoch/request token. A separate,
heap-allocated `nativewclresults::Bridge` is created only for an admitted WCL
request. It borrows the observer's completed store only while the hardware
workloop gate is held. The existing WPA2 path needs neither this allocation nor
the later encoder scratch allocation.

A future exact-KC IO80211 frontend may use the kernel-only sequence below:

1. Independently verify the running kernel collection and supply a stable,
   owned 5456-byte WCL request to `beginNativeWclScanRequest` outside the
   hardware gate. Its success means scan admission, not scan completion.
2. Poll `copyNativeForegroundScanStatus` with the returned token until the
   matching scan is `complete` and `drained`. Cancellation, timeout, or failure
   cannot provide a successful result.
3. Call `armNativeWclScanResults(token, true)`. It allocates bounded encoder
   scratch outside the gate and preflights every eligible entry against the
   same immutable, fully completed snapshot. It does not emit an event.
4. Call `reserveNativeWclScanResult` with a stable caller-owned buffer of at
   least `nativewclbeacon::maxPayloadBytes` and a separate `Frame` output. A
   successful return is one byte draft for event 201 or 237. The frame always
   reports `emissionReady == false`; no PostOffice sender has been integrated.
5. `commitNativeWclScanResult(frame, true)` currently returns Unsupported and
   aborts the reservation. `commit(..., false)` aborts a matching reservation.
   `retireNativeWclScanResults(token)` explicitly aborts and closes an
   outstanding draft; it also clears a cancelled token when no draft was armed.

These entry points use the existing `controlLock -> hardware gate` stop fence;
the frontend must not call them while holding the hardware gate. A new scan
admission invalidates old drafts. Explicit cancellation, selection, disable,
failure, timeout, and shutdown abort the pending draft. Tokens and cache
generation prevent a late caller from attributing an old result to a newer
scan. No payload buffer, Entry pointer, or private Apple object escapes the
gate. Neither a native IO80211 interface nor a Wi-Fi menu is registered yet.
The caller must disregard its output buffer and `Frame` after any failed
reserve, since invalid aliases are rejected without modifying borrowed memory.
