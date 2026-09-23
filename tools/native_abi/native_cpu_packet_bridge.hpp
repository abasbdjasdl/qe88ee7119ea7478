// SPDX-License-Identifier: GPL-2.0-or-later
// Offline contract only: no Skywalk object layout, implementation or kext entry.
#ifndef R16_NATIVE_CPU_PACKET_BRIDGE_HPP
#define R16_NATIVE_CPU_PACKET_BRIDGE_HPP
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "The CPU packet contract is not approved for a loaded driver"
#endif
#include <stddef.h>
#include <stdint.h>

class OSObject;
class IOSkywalkPacket;
class IOSkywalkTxSubmissionQueue;

namespace r16_native_audit {
using Status = int32_t; // IOReturn: full EAX; zero is success, NOT bool.
constexpr Status success = 0;
using TxAction = unsigned (*)(OSObject *, IOSkywalkTxSubmissionQueue *,
                            IOSkywalkPacket * const *, unsigned, void *);
using TxCapacityAction = unsigned (*)(OSObject *, IOSkywalkTxSubmissionQueue *,
                                    unsigned *);
static_assert(sizeof(Status) == 4 && sizeof(unsigned) == 4,
              "Pinned Darwin callback scalar ABI");

constexpr bool validConsumedCount(unsigned offered, unsigned consumed) {
    return consumed <= offered;
}

// Values obtained through audited packet APIs. This is OUR view, never a
// declaration of fields within an Apple object. base excludes dataOffset.
struct PacketView {
    const uint8_t *base{};
    size_t bufferSize{}, dataOffset{}, dataLength{};
    unsigned bufferCount{};
    bool prepared{}, supportedMetadata{};
};
inline bool validView(const PacketView &v, size_t length) {
    return v.prepared && v.supportedMetadata && v.bufferCount == 1 && v.base &&
           v.dataOffset <= v.bufferSize && length <= v.bufferSize-v.dataOffset;
}

enum class Phase { offline, running, stopping, stopped, quarantined };
enum class TxState { free, queued, inFlight, recycle };
enum class RxState { empty, allocated, prepared, transferring, quarantined };
enum class RxResult { delivered, stopped, busy, invalidFrame, allocationFailed,
                      preparationFailed, invalidBuffer, lengthFailed,
                      enqueueFailed, contractViolation };

template<class A, class B> struct Same { enum { value = false }; };
template<class A> struct Same<A,A> { enum { value = true }; };

// Port::Packet may be IOSkywalkPacket (opaque) or a host-test token. All entry
// points require one external serialization domain. Ports MUST NOT reenter
// this bridge. Ports do not throw, retain packets, fabricate private fields,
// or run backend TX under the Skywalk callback's workloop gate.
//
// Required Port methods:
// view(Packet*) -> PacketView; allocateRx(Packet**) -> Status;
// prepareRx(Packet*) -> Status; setRxLength(Packet*,unsigned) -> Status;
// enqueueRx(Packet* const*,unsigned) -> Status;
// completeTx(Packet* const*,unsigned) -> Status;
// reclaimRx(Packet*), reclaimPreparedTx(Packet*) -> void.
// completeTx uses the real TX completion queue. Its success consumes all;
// gate rejection consumes none. reclaimPreparedTx instead uses pool
// deallocatePacket WITHOUT first manually completing/notifying the packet.
// Source queues and pools MUST outlive every bridge-held packet.
template<class Port, size_t Slots = 8, size_t FrameBytes = 2048>
class CpuPacketBridge {
public:
    using Packet = typename Port::Packet;
    struct Ticket {
        size_t slot{};
        uint64_t generation{};
        const uint8_t *bytes{};
        unsigned length{};
    };
    struct Counters {
        uint64_t consumed{}, copied{}, invalidTx{}, txCompleted{}, txFailed{};
        uint64_t rxDelivered{}, rxReclaimed{}, nativeTxReturned{};
    };
private:
    struct Slot {
        Packet *native{};
        TxState state{TxState::free};
        uint64_t generation{};
        unsigned length{};
        uint8_t bytes[FrameBytes]{};
    } slots_[Slots];
    Phase phase_{Phase::offline};
    Packet *rx_{};
    RxState rxState_{RxState::empty};
    unsigned callbacks_{};
    bool receiving_{};
    uint64_t nextGeneration_{};
    Counters counters_{};

    static_assert(Slots > 0 && FrameBytes >= 14 && FrameBytes <= UINT32_MAX,
                  "Bounded Ethernet copy storage required");
    static_assert(Same<decltype(((Port*)nullptr)->allocateRx((Packet**)nullptr)),Status>::value,
                  "allocateRx returns IOReturn, never bool");
    static_assert(Same<decltype(((Port*)nullptr)->prepareRx((Packet*)nullptr)),Status>::value,
                  "prepareRx returns IOReturn");
    static_assert(Same<decltype(((Port*)nullptr)->setRxLength((Packet*)nullptr,0u)),Status>::value,
                  "setRxLength returns IOReturn");
    static_assert(Same<decltype(((Port*)nullptr)->enqueueRx((Packet* const*)nullptr,0u)),Status>::value,
                  "enqueueRx returns IOReturn, never a consumed count");
    static_assert(Same<decltype(((Port*)nullptr)->completeTx((Packet* const*)nullptr,0u)),Status>::value,
                  "completeTx queue returns IOReturn");

    static void copy(uint8_t *out, const uint8_t *in, size_t length) {
        for (size_t i=0; i<length; ++i) out[i]=in[i];
    }
    bool held(Packet *p) const {
        for (const auto &slot:slots_) if (slot.native==p) return true;
        return false;
    }
    void clear(Slot &slot) {
        slot.native=nullptr; slot.length=0; slot.state=TxState::free;
        // Generation deliberately persists to reject stale completions.
    }
    RxResult reclaimRx(Port &port, RxResult result) {
        if (rx_) { port.reclaimRx(rx_); ++counters_.rxReclaimed; }
        rx_=nullptr; rxState_=RxState::empty; receiving_=false;
        return result;
    }
public:
    CpuPacketBridge() = default;
    CpuPacketBridge(const CpuPacketBridge&) = delete;
    CpuPacketBridge &operator=(const CpuPacketBridge&) = delete;
    // There is deliberately no destructor rollback. The owner must keep this
    // storage/pools/queues alive until finishStop succeeds, or quarantine them.
    bool begin(unsigned realQueueCapacity, bool queuesInitializedAndEnabled) {
        if (phase_!=Phase::offline || realQueueCapacity<8 ||
            !queuesInitializedAndEnabled) return false;
        phase_=Phase::running; return true;
    }
    Phase phase() const { return phase_; }
    RxState rxState() const { return rxState_; }
    Packet *quarantinedRx() const { return rx_; }
    const Counters &counters() const { return counters_; }
    size_t outstandingTx() const {
        size_t n=0; for (const auto &slot:slots_) n+=slot.state!=TxState::free;
        return n;
    }
    unsigned capacity(unsigned *byteBudget) const {
        unsigned n=0;
        if (phase_==Phase::running)
            for (const auto &slot:slots_) n+=slot.state==TxState::free;
        if (byteBudget) {
            const uint64_t bytes=uint64_t(n)*FrameBytes;
            *byteBudget=bytes>UINT32_MAX?UINT32_MAX:unsigned(bytes);
        }
        return n;
    }
    // Return accepted PREFIX length only. A malformed frame is accepted into
    // a recycle slot so it cannot wedge the queue forever. No native packet is
    // returned during this callback: the framework advances its own indices
    // only after we return. The unconsumed suffix remains framework-owned.
    unsigned submitTx(Port &port, Packet * const *packets, unsigned offered) {
        if (!packets || !offered || phase_!=Phase::running || callbacks_) return 0;
        ++callbacks_;
        unsigned consumed=0;
        while (consumed<offered && phase_==Phase::running) {
            Slot *target=nullptr;
            for (auto &slot:slots_) if (slot.state==TxState::free) {target=&slot;break;}
            if (!target) break;
            Packet *packet=packets[consumed];
            if (!packet || held(packet) || nextGeneration_==UINT64_MAX) {
                phase_=Phase::quarantined; break;
            }
            const PacketView view=port.view(packet);
            target->native=packet; target->generation=++nextGeneration_;
            target->state=TxState::recycle;
            ++consumed; ++counters_.consumed;
            if (!validView(view,view.dataLength) || view.dataLength<14 ||
                view.dataLength>FrameBytes) { ++counters_.invalidTx; continue; }
            copy(target->bytes,view.base+view.dataOffset,view.dataLength);
            target->length=unsigned(view.dataLength);
            target->state=TxState::queued; ++counters_.copied;
        }
        --callbacks_;
        return validConsumedCount(offered,consumed)?consumed:0;
    }
    // Called later by the backend's worker, never inside submitTx. Byte storage
    // is valid until finishTx; hardware ownership still needs its normal drain.
    bool takeTx(Ticket &ticket) {
        ticket={};
        if (phase_!=Phase::running || callbacks_) return false;
        size_t oldest=Slots;
        for (size_t i=0;i<Slots;++i)
            if (slots_[i].state==TxState::queued &&
                (oldest==Slots || slots_[i].generation<slots_[oldest].generation)) oldest=i;
        if (oldest==Slots) return false;
        auto &s=slots_[oldest]; s.state=TxState::inFlight;
        ticket={oldest,s.generation,s.bytes,s.length}; return true;
    }
    bool finishTx(const Ticket &ticket, bool hardwareSucceeded) {
        if (callbacks_ || ticket.slot>=Slots) return false;
        auto &s=slots_[ticket.slot];
        if (s.state!=TxState::inFlight || s.generation!=ticket.generation) return false;
        s.state=TxState::recycle;
        if (hardwareSucceeded) ++counters_.txCompleted; else ++counters_.txFailed;
        return true;
    }
    // Native completion success consumes exactly one. Nonzero keeps the packet
    // for retry; shutdown may use the alternate audited pool reclaim path.
    unsigned returnCompletedTx(Port &port, bool usePoolReclaim=false) {
        if (callbacks_ || phase_==Phase::quarantined) return 0;
        unsigned n=0;
        for (auto &s:slots_) if (s.state==TxState::recycle) {
            if (usePoolReclaim) port.reclaimPreparedTx(s.native);
            else if (port.completeTx(&s.native,1)!=success) continue;
            clear(s); ++n; ++counters_.nativeTxReturned;
        }
        return n;
    }
    RxResult receive(Port &port, const uint8_t *bytes, size_t length) {
        if (phase_!=Phase::running) return RxResult::stopped;
        if (receiving_ || rx_) return RxResult::busy;
        if (!bytes || length<14 || length>FrameBytes) return RxResult::invalidFrame;
        receiving_=true;
        const Status allocation=port.allocateRx(&rx_);
        if ((allocation!=success && rx_) || (allocation==success && !rx_)) {
            // Inconsistent ownership output is not a license to guess a free.
            rxState_=RxState::quarantined; phase_=Phase::quarantined;
            receiving_=false; return RxResult::contractViolation;
        }
        if (allocation!=success) {receiving_=false;return RxResult::allocationFailed;}
        rxState_=RxState::allocated;
        if (port.prepareRx(rx_)!=success) return reclaimRx(port,RxResult::preparationFailed);
        rxState_=RxState::prepared;
        const PacketView view=port.view(rx_);
        if (!validView(view,length)) return reclaimRx(port,RxResult::invalidBuffer);
        // Port confirms exclusive ownership/writability for its RX allocation.
        copy(const_cast<uint8_t*>(view.base)+view.dataOffset,bytes,length);
        if (port.setRxLength(rx_,unsigned(length))!=success)
            return reclaimRx(port,RxResult::lengthFailed);
        if (phase_!=Phase::running) return reclaimRx(port,RxResult::stopped);
        rxState_=RxState::transferring;
        if (port.enqueueRx(&rx_,1)!=success) return reclaimRx(port,RxResult::enqueueFailed);
        // Success has disposed the wrapper and transferred its handle. Never
        // inspect or free it again, even if the network stack later drops it.
        rx_=nullptr; rxState_=RxState::empty; receiving_=false;
        ++counters_.rxDelivered; return RxResult::delivered;
    }
    void beginStop() {
        if (phase_==Phase::running) phase_=Phase::stopping;
        if (phase_==Phase::stopping)
            for (auto &s:slots_) if (s.state==TxState::queued) s.state=TxState::recycle;
    }
    bool finishStop(bool externalQueuesDisabled, bool backendHardwareDrained) {
        if (phase_!=Phase::stopping || !externalQueuesDisabled ||
            !backendHardwareDrained || callbacks_ || receiving_ || rx_ || outstandingTx())
            return false;
        phase_=Phase::stopped; return true;
    }
};
} // namespace r16_native_audit
#endif
