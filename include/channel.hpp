/// @file   include/channel.hpp
/// @brief  Channel — thread-safe IrisValue queue connecting two backends.
///
/// One end calls push(), the other calls pop() or try_pop(). No size limit —
/// the producer never blocks. pop() blocks until a value is available;
/// try_pop() returns immediately with std::nullopt if the queue is empty.

#pragma once

#include <value.hpp>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace iris {

class Channel {
    std::queue<IrisValue>   queue_;
    mutable std::mutex      mu_;
    std::condition_variable cv_;

public:
    /// Push a value into the channel. Never blocks.
    void push(IrisValue&& v) {
        {
            std::unique_lock lock(mu_);
            queue_.push(std::move(v));
        }
        cv_.notify_one();
    }

    /// Remove and return the front value. Blocks until one is available.
    IrisValue pop() {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [this]{ return !queue_.empty(); });
        IrisValue v = std::move(queue_.front());
        queue_.pop();
        return v;
    }

    /// Remove and return the front value if present; std::nullopt otherwise.
    std::optional<IrisValue> try_pop() {
        std::unique_lock lock(mu_);
        if (queue_.empty()) return std::nullopt;
        IrisValue v = std::move(queue_.front());
        queue_.pop();
        return v;
    }

    bool empty() const {
        std::unique_lock lock(mu_);
        return queue_.empty();
    }
};

} // namespace iris
