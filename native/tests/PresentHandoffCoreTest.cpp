// Phantasm - native self-tests: PresentHandoffCore (the render-thread handoff).
//
// Author: Severause

#include "PresentHandoffCore.h"
#include "TestHarness.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Phantasm::Tests
{
    using namespace Phantasm::PresentHandoffCore;

    namespace
    {
        // What the poster saw: each job's value with the lane of the post that ran it, and the post count.
        struct Record
        {
            std::mutex mx;
            std::vector<std::pair<int, Lane>> seen;
            std::atomic<Lane> postLane{ Lane::Task };
            std::atomic<int> posts{ 0 };
            std::atomic<bool> gate{ true };       // false holds the poster, as SKSE's lock would
            std::atomic<bool> entered{ false };   // the poster has taken a post
        };

        template <class Pred>
        bool WaitFor(Pred pred, int ms = 5000)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < until) {
                if (pred()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return pred();
        }

        std::size_t SeenCount(Record& r)
        {
            std::lock_guard<std::mutex> lk(r.mx);
            return r.seen.size();
        }

        // Never destroyed: the pump thread is detached (see Pump).
        Pump* RecordingPump(std::shared_ptr<Record> rec, std::chrono::milliseconds gather = std::chrono::milliseconds(0))
        {
            return new Pump([rec](Job&& job) {
                rec->entered.store(true);
                while (!rec->gate.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                rec->posts.fetch_add(1);
                rec->postLane.store(job.lane);
                job.fn();
            }, gather);
        }

        Job Note(std::shared_ptr<Record> rec, int value, Lane lane = Lane::Task)
        {
            return Job{ lane, [rec, value]() {
                std::lock_guard<std::mutex> lk(rec->mx);
                rec->seen.emplace_back(value, rec->postLane.load());
            } };
        }

        bool InOrder(Record& r, int from, int to)
        {
            std::lock_guard<std::mutex> lk(r.mx);
            if (r.seen.size() != static_cast<std::size_t>(to - from + 1)) return false;
            for (int i = from; i <= to; ++i)
                if (r.seen[i - from].first != i) return false;
            return true;
        }
    }

    void RunPresentHandoffCoreTests()
    {
        // The scope is per thread and nests.
        Check(!Inside() && Depth() == 0, "a thread starts outside every scope");
        {
            Scope outer;
            Check(Inside(), "a scope marks the thread inside");
            {
                Scope inner;
                Check(Depth() == 2, "scopes nest");
            }
            Check(Inside() && Depth() == 1, "leaving the inner scope keeps the outer one");
            bool otherInside = true;
            std::thread([&otherInside]() { otherInside = Inside(); }).join();
            Check(!otherInside, "another thread is not inside this thread's scope");
        }
        Check(!Inside(), "leaving the last scope marks the thread outside");

        // Every job arrives once, in push order.
        {
            auto rec = std::make_shared<Record>();
            Pump* pump = RecordingPump(rec);
            for (int i = 0; i < 500; ++i) pump->Push(Note(rec, i));
            Check(WaitFor([&]() { return SeenCount(*rec) == 500; }), "every pushed job reaches the poster");
            Check(InOrder(*rec, 0, 499), "jobs run in push order");
        }

        // A burst inside the gather window goes out as one post.
        {
            auto rec = std::make_shared<Record>();
            Pump* pump = RecordingPump(rec, std::chrono::milliseconds(250));
            for (int i = 0; i < 100; ++i) pump->Push(Note(rec, i));
            Check(WaitFor([&]() { return SeenCount(*rec) == 100; }), "the gathered burst arrives");
            Check(rec->posts.load() == 1 && InOrder(*rec, 0, 99), "a burst inside the gather window is one post, in order");
        }

        // A poster blocked (on SKSE's lock, in the game) never blocks a pusher; what waited meanwhile goes out as
        // one post per run of a lane, in order.
        {
            auto rec = std::make_shared<Record>();
            rec->gate.store(false);
            Pump* pump = RecordingPump(rec);
            pump->Push(Note(rec, 0));
            Check(WaitFor([&]() { return rec->entered.load(); }), "the pump takes the first job");
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 1; i <= 3; ++i) pump->Push(Note(rec, i, Lane::Task));
            for (int i = 4; i <= 5; ++i) pump->Push(Note(rec, i, Lane::UITask));
            pump->Push(Note(rec, 6, Lane::Task));
            for (int i = 7; i <= 200; ++i) pump->Push(Note(rec, i, Lane::Task));
            const auto took = std::chrono::steady_clock::now() - t0;
            Check(took < std::chrono::seconds(1) && SeenCount(*rec) == 0, "pushes return while the poster is blocked");
            Check(pump->Pending() == 200, "the jobs wait in the queue meanwhile");
            rec->gate.store(true);
            Check(WaitFor([&]() { return SeenCount(*rec) == 201; }), "the queue drains once the poster is free");
            Check(InOrder(*rec, 0, 200) && pump->Pending() == 0, "the drained jobs keep their order");
            Check(rec->posts.load() == 4, "each run of a lane is one post (first job, 3 tasks, 2 UI tasks, the rest)");
            bool lanesKept = true;
            {
                std::lock_guard<std::mutex> lk(rec->mx);
                for (const auto& [value, lane] : rec->seen)
                    lanesKept = lanesKept && lane == ((value == 4 || value == 5) ? Lane::UITask : Lane::Task);
            }
            Check(lanesKept, "each job goes out on its own lane");
        }

        // The poster may push (no pump lock is held while it runs), and pushes from several threads all arrive.
        {
            auto rec = std::make_shared<Record>();
            auto self = std::make_shared<Pump*>(nullptr);
            auto pushedBack = std::make_shared<std::atomic<bool>>(false);
            Pump* pump = new Pump([rec, self, pushedBack](Job&& job) {
                job.fn();
                if (!pushedBack->exchange(true)) (*self)->Push(Note(rec, -2));
            });
            *self = pump;
            pump->Push(Note(rec, -1));
            Check(WaitFor([&]() { return SeenCount(*rec) == 2; }), "a push from inside the poster is served");
            std::vector<std::thread> pushers;
            for (int t = 0; t < 4; ++t)
                pushers.emplace_back([pump, rec, t]() { for (int i = 0; i < 100; ++i) pump->Push(Note(rec, t * 1000 + i)); });
            for (auto& th : pushers) th.join();
            Check(WaitFor([&]() { return SeenCount(*rec) == 402; }), "pushes from several threads all arrive");
            bool perThreadOrdered = true;
            {
                std::lock_guard<std::mutex> lk(rec->mx);
                int last[4] = { -1, -1, -1, -1 };
                for (std::size_t k = 2; k < rec->seen.size(); ++k) {
                    const int v = rec->seen[k].first, t = v / 1000, i = v % 1000;
                    if (i <= last[t]) perThreadOrdered = false;
                    last[t] = i;
                }
            }
            Check(perThreadOrdered, "each thread's pushes keep their order");
        }

        // A post that throws is lost; the pump keeps serving. An empty job is never queued.
        {
            auto rec = std::make_shared<Record>();
            Pump* pump = RecordingPump(rec);
            auto threw = std::make_shared<std::atomic<bool>>(false);
            pump->Push(Job{ Lane::Task, [threw]() { threw->store(true); throw std::runtime_error("job"); } });
            Check(WaitFor([&]() { return threw->load() && pump->Pending() == 0; }), "the throwing job runs");
            pump->Push(Job{ Lane::Task, {} });
            Check(pump->Pending() == 0, "an empty job is never queued");
            pump->Push(Note(rec, 7));
            Check(WaitFor([&]() { return SeenCount(*rec) == 1; }), "the pump survives a throwing post");
        }
    }
}
