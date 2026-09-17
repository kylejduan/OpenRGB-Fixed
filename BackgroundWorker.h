/*---------------------------------------------------------*\
| BackgroundWorker.h                                        |
|                                                           |
|   Long-lived helper threads owned by ResourceManager      |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

class BackgroundWorker
{
public:
    virtual ~BackgroundWorker() = default;

    /*-----------------------------------------------------*\
    | Blocks until the worker thread has exited             |
    \*-----------------------------------------------------*/
    virtual void Stop() = 0;
};

/*---------------------------------------------------------*\
| Runs fn while holding state_mutex. Returns false without  |
| running fn when stop_requested() becomes true first, so   |
| an owner holding the mutex while stopping the worker      |
| cannot deadlock.                                          |
\*---------------------------------------------------------*/
template<class Mutex>
bool RunWhenIdle
    (
    Mutex&                          state_mutex,
    const std::function<bool()>&    stop_requested,
    const std::function<void()>&    fn,
    std::chrono::milliseconds       poll = std::chrono::milliseconds(50)
    )
{
    while(!state_mutex.try_lock())
    {
        if(stop_requested())
        {
            return(false);
        }
        std::this_thread::sleep_for(poll);
    }

    std::lock_guard<Mutex> guard(state_mutex, std::adopt_lock);

    if(stop_requested())
    {
        return(false);
    }

    fn();
    return(true);
}
