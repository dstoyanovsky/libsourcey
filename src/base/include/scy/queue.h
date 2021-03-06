///
//
// LibSourcey
// Copyright (c) 2005, Sourcey <https://sourcey.com>
//
// SPDX-License-Identifier: LGPL-2.1+
//
/// @addtogroup base
/// @{


#ifndef SCY_Queue_H
#define SCY_Queue_H


#include "scy/base.h"
#include "scy/datetime.h"
#include "scy/interface.h"
#include "scy/platform.h"
#include "scy/synchronizer.h"
#include "scy/thread.h"
#include <queue>


namespace scy {


/// Thread-safe queue container.
template <typename T> class Queue
{
public:
    void push(const T& data)
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _queue.push_back(data);
    }

    bool empty() const
    {
        return _queue.empty();
    }

    T& front()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _queue.front();
    }

    T const& front() const
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _queue.front();
    }

    T& back()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _queue.back();
    }

    T const& back() const
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _queue.back();
    }

    void pop()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _queue.pop_front();
    }

    template <typename Compare>
    void sort()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        std::sort(_queue.begin(), _queue.end(), Compare());
    }

    size_t size()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _queue.size();
    }

    std::deque<T>& queue()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _queue;
    }

protected:
    std::deque<T> _queue;
    mutable std::mutex _mutex;
};


//
// Runnable Queue
//


template <class T>
class RunnableQueue : public Queue<T*>, public basic::Runnable
{
public:
    /// The default dispatch function.
    /// Must be set before the queue is running.
    std::function<void(T&)> ondispatch;

    RunnableQueue(int limit = 2048, int timeout = 0)
        : _limit(limit)
        , _timeout(timeout)
    {
    }

    virtual ~RunnableQueue()
    {
        clear();
    }

    /// Push an item onto the queue.
    /// The queue takes ownership of the item pointer.
    virtual void push(T* item)
    {
        std::lock_guard<std::mutex> guard(_mutex);

        while (_limit > 0 && static_cast<int>(Queue<T*>::size()) >= _limit) {
            LWarn("Purging: ", Queue<T*>::size())
            delete Queue<T*>::front();
            Queue<T*>::pop();
        }

        Queue<T*>::push(reinterpret_cast<T*>(item));
    }

    /// Flush all outgoing items.
    virtual void flush()
    {
        while (!Queue<T*>::empty()) {
            auto next = Queue<T*>::front();
            dispatch(*next);
            Queue<T*>::pop();
            delete next;
        }
    }

    // Clear all queued items.
    void clear()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        while (!Queue<T*>::empty()) {
            delete Queue<T*>::front();
            Queue<T*>::pop();
        }
    }

    /// Called asynchronously to dispatch queued items.
    /// If not timeout is set this method blocks until cancel() is called,
    /// otherwise runTimeout() will be called.
    /// Pseudo protected for std::bind compatability.
    virtual void run()
    {
        if (_timeout) {
            runTimeout();
        } else {
            while (!cancelled()) {
                dispatchNext();
                scy::sleep(1);
                // scy::sleep(dispatchNext() ? 1 : 50);
            }
        }
    }

    /// Called asynchronously to dispatch queued items
    /// until the queue is empty or the timeout expires.
    /// Pseudo protected for std::bind compatability.
    virtual void runTimeout()
    {
        Stopwatch sw;
        sw.start();
        do {
            // scy::sleep(1);
        } while (!cancelled() && sw.elapsedMilliseconds() < _timeout && dispatchNext());
    }

    /// Dispatch a single item to listeners.
    virtual void dispatch(T& item)
    {
        if (ondispatch)
            ondispatch(item);
    }

    int timeout()
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _timeout;
    }

    void setTimeout(int milliseconds)
    {
        std::lock_guard<std::mutex> guard(_mutex);
        assert(Queue<T*>::empty() && "queue must not be active");
        _timeout = milliseconds;
    }

protected:
    RunnableQueue(const RunnableQueue&) = delete;
    RunnableQueue& operator=(const RunnableQueue&) = delete;

    /// Pops the next waiting item.
    virtual T* popNext()
    {
        T* next;
        {
            std::lock_guard<std::mutex> guard(_mutex);
            if (Queue<T*>::empty())
                return nullptr;

            next = Queue<T*>::front();
            Queue<T*>::pop();
        }
        return next;
    }

    /// Pops and dispatches the next waiting item.
    virtual bool dispatchNext()
    {
        T* next = popNext();
        if (next) {
            dispatch(*next);
            delete next;
            return true;
        }
        return false;
    }

    int _limit;
    int _timeout;
    mutable std::mutex _mutex;
};


//
// Synchronization Queue
//


/// SyncQueue extends Synchronizer to implement a synchronized FIFO
/// queue which receives T objects from any thread and synchronizes
/// them for safe consumption by the associated event loop.
template <class T>
class SyncQueue : public RunnableQueue<T>
{
public:
    typedef RunnableQueue<T> Queue;

    SyncQueue(uv::Loop* loop, int limit = 2048, int timeout = 20)
        : Queue(limit, timeout)
        , _sync(std::bind(&SyncQueue::run, this), loop)
    {
    }

    /// Destruction is deferred to allow enough
    /// time for all callbacks to return.
    virtual ~SyncQueue()
    {
    }

    /// Pushes an item onto the queue.
    /// Item pointers are now managed by the SyncQueue.
    virtual void push(T* item)
    {
        Queue::push(item);
        _sync.post();
    }

    virtual void cancel()
    {
        Queue::cancel();
        _sync.cancel(); // Call uv_close on the handle if calling from
                        // the event loop thread or we deadlock.
        if (_sync.tid() == Thread::currentID())
            _sync.close();
    }

    Synchronizer& sync() { return _sync; }

protected:
    Synchronizer _sync;
};


//
// Asynchronous Queue
//


/// AsyncQueue is a thread-based queue which receives packets
/// from any thread source and dispatches them asynchronously.
///
/// This queue is useful for deferring load from operation
/// critical system devices before performing long running tasks.
///
/// The thread will call the RunnableQueue's run() method to
/// constantly flush outgoing packets until cancel() is called.
template <class T>
class AsyncQueue : public RunnableQueue<T>
{
public:
    typedef RunnableQueue<T> Queue;

    AsyncQueue(int limit = 2048)
        : Queue(limit)
        , _thread(std::bind(&Queue::run, this))
    {
    }

    virtual void cancel()
    {
        Queue::cancel();
        _thread.cancel();
    }

protected:
    virtual ~AsyncQueue()
    {
    }

    Thread _thread;
};


} // namespace scy


#endif // SCY_Queue_H


/// @\}
