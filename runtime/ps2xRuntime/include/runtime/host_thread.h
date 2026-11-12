#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#if defined(__APPLE__)
#include <pthread.h>
#endif

// A std::thread with an explicit stack size on Darwin. macOS gives pthreads a 512 KB stack by default; the
// recompiled game executes deep guest call chains on this thread, so it needs the same 16 MB the Windows and Linux
// builds request. Everywhere else this is a thin std::thread wrapper.
class HostThread
{
public:
    HostThread() = default;

    template <typename Fn>
    explicit HostThread(Fn &&fn)
    {
#if defined(__APPLE__)
        auto *task = new std::function<void()>(std::forward<Fn>(fn));
        pthread_attr_t attributes;
        if (pthread_attr_init(&attributes) != 0)
        {
            delete task;
            return;
        }
        pthread_attr_setstacksize(&attributes, 16u * 1024u * 1024u);
        if (pthread_create(&m_thread, &attributes, &HostThread::trampoline, task) == 0)
            m_joinable = true;
        else
            delete task;
        pthread_attr_destroy(&attributes);
#else
        m_thread = std::thread(std::forward<Fn>(fn));
#endif
    }

    ~HostThread()
    {
#if defined(__APPLE__)
        if (m_joinable)
            std::terminate();
#else
        if (m_thread.joinable())
            std::terminate();
#endif
    }

    HostThread(const HostThread &) = delete;
    HostThread &operator=(const HostThread &) = delete;

    HostThread(HostThread &&other) noexcept
    {
#if defined(__APPLE__)
        m_thread = other.m_thread;
        m_joinable = other.m_joinable;
        other.m_joinable = false;
#else
        m_thread = std::move(other.m_thread);
#endif
    }

    HostThread &operator=(HostThread &&other) noexcept
    {
        if (this != &other)
        {
            if (joinable())
                std::terminate();
#if defined(__APPLE__)
            m_thread = other.m_thread;
            m_joinable = other.m_joinable;
            other.m_joinable = false;
#else
            m_thread = std::move(other.m_thread);
#endif
        }
        return *this;
    }

    bool joinable() const
    {
#if defined(__APPLE__)
        return m_joinable;
#else
        return m_thread.joinable();
#endif
    }

    void join()
    {
#if defined(__APPLE__)
        if (m_joinable)
        {
            pthread_join(m_thread, nullptr);
            m_joinable = false;
        }
#else
        if (m_thread.joinable())
            m_thread.join();
#endif
    }

private:
#if defined(__APPLE__)
    static void *trampoline(void *arg)
    {
        std::unique_ptr<std::function<void()>> task(static_cast<std::function<void()> *>(arg));
        (*task)();
        return nullptr;
    }

    pthread_t m_thread{};
    bool m_joinable = false;
#else
    std::thread m_thread;
#endif
};
