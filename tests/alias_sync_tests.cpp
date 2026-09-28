#include "alias_sync.h"

#include <cstdlib>
#include <iostream>

using namespace Wayfarer;

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }

    void Acknowledge(AliasSyncQueue& queue, const AliasSyncSnapshotSet& state)
    {
        const auto ticket = queue.Begin(state, queue.DirtySlots(state));
        Check(ticket && queue.Complete(ticket, true), "dirty state can finish");
    }
} // namespace

int main()
{
    AliasSyncQueue queue;
    AliasSyncSnapshotSet state{};
    Check(queue.DirtySlots(state) == 0x03FF, "startup includes all ten slots for saved-state cleanup");
    Check(!queue.Pending(), "startup has no script queued");
    Check(!queue.Begin(state, 0), "an empty request does not start a script");
    Check(!queue.Begin(state, 0xFC00), "bits outside the ten slots do not start a script");
    Check(!queue.Complete(0, true), "zero is never a completion ticket");

    auto ticket = queue.Begin(state, 1);
    Check(ticket && queue.Pending(), "first selected slot starts one request");
    for (int attempt = 0; attempt < 1000; ++attempt) {
        Check(!queue.Begin(state, 0x03FF), "a slow VM cannot accumulate duplicate requests");
    }
    Check(!queue.Complete(ticket + 1, true) && queue.Pending(), "unknown callback cannot release a request");
    Check(queue.Complete(ticket, true), "matching callback completes a request");
    Check(!queue.Complete(ticket, true), "a duplicate callback has no effect");
    Check(queue.DirtySlots(state) == 0x03FE, "partial pass acknowledges only its selected slots");
    Acknowledge(queue, state);
    Check(queue.DirtySlots(state) == 0, "unchanged empty slots remain quiet");
    Check(queue.DirtySlots(state, 0x8401) == 1, "readiness mask is restricted to the ten real slots");

    // Every field can require script work even when the follower itself is unchanged.
    for (int field = 0; field < 6; ++field) {
        auto changed = state;
        auto& slot = changed[9];
        switch (field) {
        case 0:
            slot.follower = 11;
            break;
        case 1:
            slot.marker = 12;
            break;
        case 2:
            slot.seat = 13;
            break;
        case 3:
            slot.gather = 14;
            break;
        case 4:
            slot.social = 15;
            break;
        case 5:
            slot.handoff = 16;
            break;
        }
        Check(queue.DirtySlots(changed) == 0x0200, "a changed field dirties only its own slot");
    }

    state[2] = {101, 201, 301, 401, 501, 7};
    ticket = queue.Begin(state, queue.DirtySlots(state));
    auto latest = state;
    latest[2].social = 502;
    latest[6].follower = 106;
    Check(queue.Complete(ticket, true), "captured pass can finish after desired state changes");
    Check(queue.DirtySlots(latest) == ((1u << 2) | (1u << 6)),
          "changes during an outstanding pass remain dirty, including newly occupied slots");
    Acknowledge(queue, latest);
    Check(queue.DirtySlots(latest) == 0, "next pass converges to the latest state");

    latest[2] = {};
    latest[6] = {};
    Check(queue.DirtySlots(latest) == ((1u << 2) | (1u << 6)),
          "disable cleanup includes previously occupied slots");
    Acknowledge(queue, latest);
    Check(queue.DirtySlots(latest) == 0, "completed disable cleanup stays quiet");

    // Pending idles are work even when aliases and social targets have not changed.
    ticket = queue.Begin(latest, queue.DirtySlots(latest, 1u << 4));
    Check(ticket && queue.Complete(ticket, true), "ready idle can request a stable slot");
    Check(queue.DirtySlots(latest, 1u << 4) == (1u << 4),
          "unconsumed work is retried independently of acknowledged state");
    Check(queue.DirtySlots(latest) == 0, "consumed idle adds no periodic work");

    // A priority handoff can interrupt a slot outside the original request mask.
    // Its explicit cleanup flag must survive acknowledgement of that request.
    ticket = queue.Begin(latest, 1u << 3);
    const std::uint16_t cleanupWork = 1u << 7;
    Check(queue.Complete(ticket, false), "a sweep can abort after priority cleanup starts elsewhere");
    Check(queue.DirtySlots(latest, cleanupWork) == ((1u << 3) | (1u << 7)),
          "external cleanup selects its slot even with stable snapshots outside the failed mask");
    ticket = queue.Begin(latest, queue.DirtySlots(latest, cleanupWork));
    Check(queue.Complete(ticket, true), "a follow-up sweep services selected and priority cleanup slots");
    Check(queue.DirtySlots(latest, cleanupWork) == cleanupWork,
          "snapshot acknowledgement cannot erase explicit pending cleanup");
    Check(queue.DirtySlots(latest) == 0, "completed cleanup returns unchanged slots to idle");

    latest[1].follower = 999;
    ticket = queue.Begin(latest, queue.DirtySlots(latest));
    Check(queue.Complete(ticket, false) && !queue.Pending(), "failure releases the dispatch reservation");
    Check(queue.DirtySlots(latest) == 2, "failed pass does not acknowledge unperformed work");
    Acknowledge(queue, latest);
    Check(queue.DirtySlots(latest) == 0, "a later successful retry becomes quiet");

    // A failed pass may have applied B before aborting. Returning to desired A
    // must still correct the alias even though A was acknowledged previously.
    auto transient = latest;
    transient[1].follower = 1001;
    ticket = queue.Begin(transient, queue.DirtySlots(transient));
    Check(queue.Complete(ticket, false), "partially applied transient state can abort");
    Check(queue.DirtySlots(latest) == 2,
          "failed work invalidates its slot even when desired state returns to the prior acknowledgement");
    Acknowledge(queue, latest);
    Check(queue.DirtySlots(latest) == 0, "retry restores the acknowledged state after an aborted transition");

    latest[3].follower = 103;
    const auto beforeLoad = queue.Begin(latest, queue.DirtySlots(latest));
    queue.Reset();
    Check(!queue.Pending() && queue.DirtySlots(latest) == 0x03FF,
          "load forgets saved acknowledgements and cancels the native reservation");
    const auto afterLoad = queue.Begin(latest, 0xFFFF);
    Check(afterLoad > beforeLoad, "tickets advance across reset");
    Check(!queue.Complete(beforeLoad, true) && queue.Pending(),
          "a callback from before load cannot acknowledge or release new work");
    Check(queue.Complete(afterLoad, true), "current load callback remains valid");
    Check(queue.DirtySlots(latest) == 0, "upper mask bits do not create phantom work");
    queue.Reset();
    Check(!queue.Complete(afterLoad, true), "reset also rejects late callbacks without a new request");

    std::cout << "Alias sync tests passed\n";
}
