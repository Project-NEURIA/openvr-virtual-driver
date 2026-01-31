#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <memory>
#include <atomic>

namespace mpsc {

template<typename T>
class Sender;

template<typename T>
class Receiver;

template<typename T>
struct Channel {
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<size_t> sender_count{0};
    std::atomic<bool> receiver_alive{true};
};

template<typename T>
class Sender {
public:
    Sender(std::shared_ptr<Channel<T>> channel) : m_channel(channel) {
        m_channel->sender_count++;
    }

    Sender(const Sender& other) : m_channel(other.m_channel) {
        m_channel->sender_count++;
    }

    Sender& operator=(const Sender& other) {
        if (this != &other) {
            decrement_sender();
            m_channel = other.m_channel;
            m_channel->sender_count++;
        }
        return *this;
    }

    Sender(Sender&& other) noexcept : m_channel(std::move(other.m_channel)) {
        other.m_channel = nullptr;
    }

    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            decrement_sender();
            m_channel = std::move(other.m_channel);
            other.m_channel = nullptr;
        }
        return *this;
    }

    ~Sender() {
        decrement_sender();
    }

    // Returns false if receiver is gone
    bool send(T value) {
        if (!m_channel || !m_channel->receiver_alive) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_channel->mtx);
            m_channel->queue.push(std::move(value));
        }
        m_channel->cv.notify_one();
        return true;
    }

private:
    void decrement_sender() {
        if (m_channel) {
            if (--m_channel->sender_count == 0) {
                m_channel->cv.notify_all();
            }
        }
    }

    std::shared_ptr<Channel<T>> m_channel;
};

template<typename T>
class Receiver {
public:
    Receiver(std::shared_ptr<Channel<T>> channel) : m_channel(channel) {}

    // Non-copyable
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    // Movable
    Receiver(Receiver&& other) noexcept : m_channel(std::move(other.m_channel)) {
        other.m_channel = nullptr;
    }

    Receiver& operator=(Receiver&& other) noexcept {
        if (this != &other) {
            if (m_channel) {
                m_channel->receiver_alive = false;
            }
            m_channel = std::move(other.m_channel);
            other.m_channel = nullptr;
        }
        return *this;
    }

    ~Receiver() {
        if (m_channel) {
            m_channel->receiver_alive = false;
            m_channel->cv.notify_all();
        }
    }

    // Blocks until value available. Returns nullopt if all senders are gone.
    std::optional<T> recv() {
        if (!m_channel) return std::nullopt;

        std::unique_lock<std::mutex> lock(m_channel->mtx);
        m_channel->cv.wait(lock, [this] {
            return !m_channel->queue.empty() || m_channel->sender_count == 0;
        });

        if (m_channel->queue.empty()) {
            return std::nullopt;
        }

        T value = std::move(m_channel->queue.front());
        m_channel->queue.pop();
        return value;
    }

    // Non-blocking. Returns nullopt if no value available.
    std::optional<T> try_recv() {
        if (!m_channel) return std::nullopt;

        std::lock_guard<std::mutex> lock(m_channel->mtx);
        if (m_channel->queue.empty()) {
            return std::nullopt;
        }

        T value = std::move(m_channel->queue.front());
        m_channel->queue.pop();
        return value;
    }

private:
    std::shared_ptr<Channel<T>> m_channel;
};

template<typename T>
std::pair<Sender<T>, Receiver<T>> channel() {
    auto ch = std::make_shared<Channel<T>>();
    return { Sender<T>(ch), Receiver<T>(ch) };
}

} // namespace mpsc
