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
#include <set>
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
| checked for its return. Entries survive rescans. Each     |
| check reports the identity of the hardware it found, or   |
| an empty string. One identity is given to one entry per   |
| round, preferring the entry it belongs to, so a single    |
| returning device cannot resolve two missing ones.         |
|                                                           |
| An entry is reported after two consecutive sightings, so  |
| the OS can finish enumerating every interface first, and  |
| stays until Forget() confirms recovery with its own key   |
| or the identity it was last seen as. Further reports need |
| 12, then 60 sightings, so a scan that keeps failing is    |
| retried with backoff instead of in a tight loop.          |
\*---------------------------------------------------------*/
class HardwareReturnTracker
{
public:
    void Watch(const std::string& key, std::function<std::string()> find)
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries[key] = Entry{std::move(find)};
    }

    void Forget(const std::string& identity)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for(std::map<std::string, Entry>::iterator entry = entries.begin(); entry != entries.end();)
        {
            if(entry->first == identity || entry->second.last_seen == identity)
            {
                entry = entries.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
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
    | the keys whose hardware is back                       |
    \*-----------------------------------------------------*/
    std::vector<std::string> Check()
    {
        std::vector<std::pair<std::string, std::function<std::string()>>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for(const std::pair<const std::string, Entry>& entry : entries)
            {
                pending.emplace_back(entry.first, entry.second.find);
            }
        }

        std::vector<std::pair<std::string, std::string>> found;
        for(const std::pair<std::string, std::function<std::string()>>& entry : pending)
        {
            found.emplace_back(entry.first, entry.second());
        }

        std::set<std::string> claimed;
        std::map<std::string, std::string> sighted;
        for(int pass = 0; pass < 2; pass++)
        {
            for(const std::pair<std::string, std::string>& entry : found)
            {
                const bool own = entry.second == entry.first;
                if(entry.second.empty() || own != (pass == 0) || claimed.count(entry.second))
                {
                    continue;
                }
                claimed.insert(entry.second);
                sighted[entry.first] = entry.second;
            }
        }

        std::vector<std::string> returned;
        std::lock_guard<std::mutex> lock(mutex);
        for(const std::pair<std::string, std::string>& entry : found)
        {
            std::map<std::string, Entry>::iterator tracked = entries.find(entry.first);
            if(tracked == entries.end())
            {
                continue;
            }
            std::map<std::string, std::string>::const_iterator sighting = sighted.find(entry.first);
            if(sighting == sighted.end())
            {
                tracked->second.sightings = 0;
                continue;
            }
            tracked->second.last_seen = sighting->second;
            const int needed = tracked->second.reports == 0 ? 2 : (tracked->second.reports == 1 ? 12 : 60);
            if(++tracked->second.sightings >= needed)
            {
                returned.push_back(entry.first);
                tracked->second.sightings = 0;
                tracked->second.reports++;
            }
        }
        return(returned);
    }

private:
    struct Entry
    {
        std::function<std::string()>    find;
        int                             sightings   = 0;
        int                             reports     = 0;
        std::string                     last_seen;
    };

    std::mutex                      mutex;
    std::map<std::string, Entry>    entries;
};
