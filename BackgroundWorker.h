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
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

/*---------------------------------------------------------*\
| Hardware that disappeared, such as an unplugged receiver, |
| checked for its return. Entries survive rescans. An entry |
| returns after two consecutive sightings, so the OS can    |
| finish enumerating every interface first.                 |
\*---------------------------------------------------------*/
class HardwareReturnTracker
{
public:
    void Watch(const std::string& key, std::function<bool()> present)
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries[key] = Entry{std::move(present), 0};
    }

    void Forget(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(key);
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries.clear();
    }

    bool Empty()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return(entries.empty());
    }

    /*-----------------------------------------------------*\
    | Runs each check without holding the lock and returns |
    | the keys seen twice in a row, which are removed       |
    \*-----------------------------------------------------*/
    std::vector<std::string> Check()
    {
        std::vector<std::pair<std::string, std::function<bool()>>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for(const std::pair<const std::string, Entry>& entry : entries)
            {
                pending.emplace_back(entry.first, entry.second.present);
            }
        }

        std::vector<std::pair<std::string, bool>> seen;
        for(const std::pair<std::string, std::function<bool()>>& entry : pending)
        {
            seen.emplace_back(entry.first, entry.second());
        }

        std::vector<std::string> returned;
        std::lock_guard<std::mutex> lock(mutex);
        for(const std::pair<std::string, bool>& sighting : seen)
        {
            std::map<std::string, Entry>::iterator entry = entries.find(sighting.first);
            if(entry == entries.end())
            {
                continue;
            }
            if(!sighting.second)
            {
                entry->second.sightings = 0;
            }
            else if(++entry->second.sightings >= 2)
            {
                returned.push_back(sighting.first);
                entries.erase(entry);
            }
        }
        return(returned);
    }

private:
    struct Entry
    {
        std::function<bool()>   present;
        int                     sightings;
    };

    std::mutex                      mutex;
    std::map<std::string, Entry>    entries;
};
