// SPDX-License-Identifier: GPL-2.0-or-later
// Compile/host-test contract only; this is not a loaded native frontend.
#pragma once
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "Native Infra scan bridge is an offline prototype"
#endif
#include "../../src/network/NativeWclScan.hpp"
#include "../../src/network/NativeWclScanPlan.hpp"

namespace r16_infra_audit {
namespace wire = rtl8852be::network::nativewclscan;
namespace plan = rtl8852be::network::nativewclscanplan;
namespace foreground = rtl8852be::network::foregroundscan;

enum class Status : uint8_t { accepted, badArgument, unsupported, notReady,
                             busy, stale, failed };
struct Identity { uint64_t epoch{}, generation{}; };
inline bool same(Identity a, Identity b) {
    return a.epoch == b.epoch && a.generation == b.generation;
}
struct Snapshot {
    size_t copiedBytes{};
    wire::Policy policy{};
    Identity identity{};
};
// All fields are values. No input pointer, credential, or raw native message
// survives admission. opaqueSourceHeader is deliberately NOT the identity.
struct CopiedScan {
    Identity identity{};
    wire::Request request{};
    foreground::RequestedPlan channels{};
};
struct Backend {
    void *context{};
    // This missing real adapter is a SECURITY/ABI boundary, not memcpy glue.
    // It must validate the dispatch's operation, exact readable input length,
    // target KC and lifetime, and snapshot actual channel/private-MAC policy.
    // The opaque callback argument may NEVER be dereferenced by this bridge.
    // Success means exactly messageBytes copied into stable local storage.
    Status (*copyValidated)(void *, const void *opaqueArgument, uint8_t *local,
                            size_t capacity, Snapshot &){};
    // Must copy the small value before return; it cannot retain this reference.
    // accepted means the REAL backend admitted the requested channel order and
    // timing. It never means scan done/results published. Failure accepts none.
    // A future adapter uses R16NetworkController::beginNativePlannedForegroundScan
    // (not the bool/fixed-dwell API), and retains the returned backend Token in
    // a mapping to this independent dispatch Identity. See evidence document.
    Status (*submitCopied)(void *, const CopiedScan &){};
};

// Own this as heap/IOKit-object storage, never on the kernel stack. All calls,
// including copy/submit and terminal(), require the same external gate. The
// backend must not reenter or synchronously issue terminal() during submit.
// Backend context and this storage must outlive any admitted backend work.
class ScanBridge {
    uint8_t scratch_[wire::messageBytes]{};
    Backend backend_{};
    uint64_t epoch_{}, lastBoundEpoch_{}, lastGeneration_{};
    Identity inFlight_{};
    bool entering_{}, poisoned_{};

    void wipe() { wire::detail::clear(scratch_, sizeof(scratch_)); }
    Status finish(Status status) { wipe(); entering_ = false; return status; }
public:
    ScanBridge() = default;
    ScanBridge(const ScanBridge &) = delete;
    ScanBridge &operator=(const ScanBridge &) = delete;
    // No destructor rollback: the future owner must cancel+drain first or
    // quarantine this storage and context. No Apple stop/unload claim is made.
    bool bind(Backend backend, uint64_t epoch) {
        if (entering_ || inFlight_.generation || epoch_ || poisoned_ || !epoch ||
            epoch <= lastBoundEpoch_ ||
            !backend.context || !backend.copyValidated || !backend.submitCopied)
            return false;
        // Preserve history across unbind: an old delayed terminal ticket must
        // never match a new request. Epochs are strictly increasing, no wrap.
        backend_ = backend; epoch_ = lastBoundEpoch_ = epoch; return true;
    }
    bool unbindIdle() {
        if (entering_ || inFlight_.generation) return false;
        backend_ = {}; epoch_ = 0; lastGeneration_ = 0; wipe(); return true;
    }
    void poison() { poisoned_ = true; }
    Identity inFlight() const { return inFlight_; }
    bool idle() const { return !entering_ && !inFlight_.generation; }
    // This is only backend retirement bookkeeping. The caller must already
    // have observed actual success/cancel/failure and drained that generation.
    // It does not emit a WCL event or fabricate scan success.
    bool terminal(Identity identity) {
        if (entering_ || !identity.generation || !same(identity, inFlight_))
            return false;
        inFlight_ = {}; return true;
    }
    Status scan(const void *opaqueArgument) {
        if (!opaqueArgument) return Status::badArgument;
        if (entering_ || inFlight_.generation) return Status::busy;
        if (poisoned_ || !epoch_) return Status::notReady;
        entering_ = true; wipe();
        Snapshot snapshot{};
        const Status copied = backend_.copyValidated(
            backend_.context, opaqueArgument, scratch_, sizeof(scratch_), snapshot);
        if (copied != Status::accepted) return finish(copied);
        if (snapshot.copiedBytes != sizeof(scratch_)) return finish(Status::badArgument);
        if (snapshot.identity.epoch != epoch_ || !snapshot.identity.generation ||
            snapshot.identity.generation <= lastGeneration_) return finish(Status::stale);
        // Consume the trusted dispatch generation even if its decoded request
        // or backend admission fails. Retrying requires a new dispatch ticket.
        lastGeneration_ = snapshot.identity.generation;
        CopiedScan value{}; value.identity = snapshot.identity;
        const auto decoded = wire::decode(scratch_, sizeof(scratch_), snapshot.policy,
                                          value.request);
        if (decoded.status != wire::Status::knownSubset) return finish(Status::unsupported);
        if (plan::map(decoded, value.request, value.channels).status != plan::Status::planned)
            return finish(Status::unsupported);
        // Native filters are already rejected; home/away timing is explicitly
        // unsupported. No fixed dwell, saved SSID or default-all channel mask.
        const Status admitted = backend_.submitCopied(backend_.context, value);
        if (admitted == Status::accepted) inFlight_ = value.identity;
        wire::detail::clear(&value, sizeof(value));
        return finish(admitted);
    }
};
static_assert(sizeof(CopiedScan) <= 192, "small copied backend value");
} // namespace r16_infra_audit
