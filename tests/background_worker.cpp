// SPDX-License-Identifier: GPL-2.0-or-later
// A worker waiting for background state must never block its own shutdown.
#include "BackgroundWorker.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>

int main()
{
    using namespace std::chrono_literals;
    std::mutex state;
    std::atomic<bool> stop{false};
    std::atomic<int> runs{0};
    auto stopping = [&] { return stop.load(); };
    auto work = [&] { ++runs; };

    assert(RunWhenIdle(state, stopping, work, 5ms) && runs == 1);
    assert(state.try_lock() && "The state mutex must be released after running");
    state.unlock();

    std::unique_lock<std::mutex> held(state);
    auto abandoned = std::async(std::launch::async, [&] { return RunWhenIdle(state, stopping, work, 5ms); });
    assert(abandoned.wait_for(50ms) == std::future_status::timeout);
    stop = true;
    assert(abandoned.wait_for(1s) == std::future_status::ready && "Stop must end the wait while the mutex stays held");
    assert(!abandoned.get() && runs == 1);

    stop = false;
    auto deferred = std::async(std::launch::async, [&] { return RunWhenIdle(state, stopping, work, 5ms); });
    assert(deferred.wait_for(30ms) == std::future_status::timeout);
    held.unlock();
    assert(deferred.get() && runs == 2);

    // A missing receiver returns only after two consecutive sightings.
    HardwareReturnTracker tracker;
    std::atomic<bool> back{false};
    int checks = 0;
    tracker.Watch("receiver", [&] { ++checks; return back.load() ? std::string("receiver") : std::string(); });
    tracker.Watch("other", [] { return std::string(); });
    assert(tracker.Check().empty() && checks == 1);
    back = true;
    assert(tracker.Check().empty() && "One sighting is not enough");
    back = false;
    assert(tracker.Check().empty() && "A miss resets the count");
    back = true;
    assert(tracker.Check().empty());
    std::vector<std::string> returned = tracker.Check();
    assert(returned.size() == 1 && returned[0] == "receiver");
    assert(!tracker.Empty() && "Only the returned entry is removed");
    tracker.Forget("other");
    assert(tracker.Empty());

    // Two same-model receivers are missing; one comes back on another port.
    // Both checks find it, but it may resolve only one of them.
    tracker.Watch("a", [] { return std::string("moved"); });
    tracker.Watch("b", [] { return std::string("moved"); });
    tracker.Check();
    returned = tracker.Check();
    assert(returned.size() == 1 && "One returning receiver resolves one missing receiver");
    assert(!tracker.Empty() && "The other stays tracked across the rescan");
    tracker.Clear();

    // A receiver back at its own path is preferred over one that moved.
    tracker.Watch("a", [] { return std::string("b"); });
    tracker.Watch("b", [] { return std::string("b"); });
    tracker.Check();
    returned = tracker.Check();
    assert(returned.size() == 1 && returned[0] == "b");
    tracker.Clear();
    assert(tracker.Empty() && tracker.Check().empty());
    std::printf("PASS: background worker idle wait and hardware return tracking\n");
}
