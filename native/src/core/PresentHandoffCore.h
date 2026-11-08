#pragma once
// Phantasm - PresentHandoffCore: the engine-free half of PresentHandoff.h (the per-thread "inside a render-thread
// callback" depth, and the FIFO one pump thread empties into a poster). Tested by
// native/tests/PresentHandoffCoreTest.cpp.
//
// Author: Severause

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Phantasm::PresentHandoffCore
{
    /// How many render-thread callback scopes the calling thread is inside.
    inline int& Depth() noexcept
    {
        thread_local int depth = 0;
        return depth;
    }

    /// Held for the length of a callback Magelight runs on its render thread.
    struct Scope
    {
        Scope() noexcept { ++Depth(); }
        ~Scope() { --Depth(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

    inline bool Inside() noexcept { return Depth() > 0; }

    enum class Lane : std::uint8_t { Task, UITask };

    struct Job
    {
        Lane lane = Lane::Task;
        std::function<void()> fn;
    };

    /// A FIFO and the one thread that hands its jobs to `poster` in push order. Push takes only the queue's own lock
    /// and never waits on the poster, so a poster blocked on SKSE's lock stalls the pump thread, never a pusher.
    /// The pump waits `gather` after waking and then hands each run of one lane's jobs over as ONE job: a burst of
    /// posts (one frame's clicks) costs one wait on SKSE's lock and lands in one drain. The pump thread is detached
    /// and uses the object until the process ends: allocate one and never destroy it.
    class Pump
    {
    public:
        using Poster = std::function<void(Job&&)>;

        explicit Pump(Poster poster, std::chrono::milliseconds gather = std::chrono::milliseconds(0)) :
            m_poster(std::move(poster)), m_gather(gather)
        {}
        Pump(const Pump&) = delete;
        Pump& operator=(const Pump&) = delete;

        /// Start the pump thread now (idempotent). Push starts it otherwise.
        void Start()
        {
            std::lock_guard<std::mutex> lk(m_mx);
            StartLocked();
        }

        /// Queue a job (an empty one is dropped). A thread that cannot start leaves the job queued for the next try.
        void Push(Job job)
        {
            if (!job.fn) return;
            {
                std::lock_guard<std::mutex> lk(m_mx);
                m_queue.push_back(std::move(job));
                StartLocked();
            }
            m_cv.notify_one();
        }

        /// Jobs queued and not yet taken by the pump thread.
        std::size_t Pending() const
        {
            std::lock_guard<std::mutex> lk(m_mx);
            return m_queue.size();
        }

    private:
        static Job Combine(Lane lane, std::vector<std::function<void()>>&& fns)
        {
            if (fns.size() == 1) return Job{ lane, std::move(fns.front()) };
            return Job{ lane, [fns = std::move(fns)]() {
                for (const auto& fn : fns) fn();
            } };
        }

        void StartLocked() noexcept
        {
            if (m_started) return;
            try {
                std::thread([this]() { Run(); }).detach();
                m_started = true;
            } catch (...) {
            }
        }

        void Run()
        {
            for (;;) {
                std::deque<Job> waiting;
                {
                    std::unique_lock<std::mutex> lk(m_mx);
                    m_cv.wait(lk, [this]() { return !m_queue.empty(); });
                    if (m_gather.count() > 0) {
                        lk.unlock();
                        std::this_thread::sleep_for(m_gather);
                        lk.lock();
                    }
                    waiting.swap(m_queue);
                }
                while (!waiting.empty()) {
                    const Lane lane = waiting.front().lane;
                    std::vector<std::function<void()>> fns;
                    while (!waiting.empty() && waiting.front().lane == lane) {
                        fns.push_back(std::move(waiting.front().fn));
                        waiting.pop_front();
                    }
                    try {
                        m_poster(Combine(lane, std::move(fns)));
                    } catch (...) {
                        // That post is lost; the pump keeps serving the rest.
                    }
                }
            }
        }

        Poster m_poster;
        const std::chrono::milliseconds m_gather;
        mutable std::mutex m_mx;
        std::condition_variable m_cv;
        std::deque<Job> m_queue;
        bool m_started = false;
    };
}
