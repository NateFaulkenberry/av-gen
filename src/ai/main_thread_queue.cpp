#include "ai/main_thread_queue.hpp"

#include "core/log.hpp"

#include <exception>

namespace avgen::ai {

MainThreadQueue::~MainThreadQueue() { shutdown(); }

void MainThreadQueue::bindToCurrentThread() {
    const std::lock_guard lock(mutex_);
    owner_ = std::this_thread::get_id();
    haveOwner_ = true;
}

bool MainThreadQueue::run(std::function<void()> fn, const CancelToken& cancel,
                          std::chrono::milliseconds timeout) {
    if (!fn) {
        return false;
    }
    bool inlineHere = false;
    {
        const std::lock_guard lock(mutex_);
        if (stopped_) {
            return false;
        }
        // Inline on the pumping thread. Enqueuing here would wait for a pump that cannot run,
        // because this *is* the thread that pumps.
        inlineHere = haveOwner_ && owner_ == std::this_thread::get_id();
    }
    if (inlineHere) {
        // Outside the lock deliberately: `fn` touches the engine and may take a while, and holding
        // the queue lock across it would block every worker trying to enqueue.
        fn();
        return true;
    }
    if (cancel.cancelled()) {
        return false;
    }

    auto item = std::make_shared<Item>();
    item->fn = std::move(fn);
    {
        const std::lock_guard lock(mutex_);
        if (stopped_) {
            return false;
        }
        queue_.push_back(item);
    }

    // Erases `item` from the queue if it has not started. Returns true when it was still waiting,
    // which means it never ran and never will.
    const auto withdraw = [this, &item]() {
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (it->get() == item.get()) {
                queue_.erase(it);
                return true;
            }
        }
        return false;
    };

    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!item->done) {
        if (stopped_) {
            return false;
        }
        if (done_.wait_for(lock, std::chrono::milliseconds{25}) == std::cv_status::timeout) {
            // Cancelled while still queued: take it back out so it never runs. Work that has
            // already started is always allowed to finish -- stopping a half-applied engine
            // mutation is worse than completing it and rolling the transaction back.
            if (cancel.cancelled() && !item->done && withdraw()) {
                return false;
            }
            if (std::chrono::steady_clock::now() > deadline && !item->done && withdraw()) {
                log::warn("ai: a tool waited {} ms for the main thread and gave up",
                          timeout.count());
                return false;
            }
        }
    }
    return item->ran;
}

std::size_t MainThreadQueue::pump(double budgetMs) {
    {
        const std::lock_guard lock(mutex_);
        owner_ = std::this_thread::get_id();
        haveOwner_ = true;
    }
    const auto start = std::chrono::steady_clock::now();
    std::size_t ran = 0;
    while (true) {
        std::shared_ptr<Item> item;
        {
            const std::lock_guard lock(mutex_);
            if (stopped_ || queue_.empty()) {
                break;
            }
            item = queue_.front();
            queue_.pop_front();
        }
        // Outside the lock: the work touches the engine, and a worker enqueuing meanwhile must not
        // be blocked behind it.
        try {
            item->fn();
            item->ran = true;
        } catch (const std::exception& e) {
            // A tool body that throws is already caught in ToolRegistry::invoke; this is the
            // backstop for anything else posted here. It must not escape into the frame loop.
            log::error("ai: main-thread work threw: {}", e.what());
        } catch (...) {
            log::error("ai: main-thread work threw a non-standard exception");
        }
        {
            const std::lock_guard lock(mutex_);
            item->done = true;
        }
        done_.notify_all();
        ++ran;
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (elapsedMs >= budgetMs) {
            break; // the rest waits for the next frame
        }
    }
    return ran;
}

void MainThreadQueue::shutdown() {
    {
        const std::lock_guard lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        for (auto& item : queue_) {
            item->done = true;
            item->ran = false;
        }
        queue_.clear();
    }
    done_.notify_all();
}

std::size_t MainThreadQueue::pending() const {
    const std::lock_guard lock(mutex_);
    return queue_.size();
}

} // namespace avgen::ai
