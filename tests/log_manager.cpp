// SPDX-License-Identifier: GPL-2.0-or-later
// Verify real logger output/retention contracts around the suppressed-message path.
#include "LogManager.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

static void dialog(void* context,PLogMessage message)
{
    assert(message->level==LL_DIALOG);
    ++*static_cast<int*>(context);
}
int main(int argc,char**argv)
{
    assert(argc==2);
    const std::string test=argv[1];
    if(test=="singleton")
    {
        std::atomic<bool> ready{false};
        std::vector<LogManager*> instances(16);
        std::vector<std::thread> threads;
        for(size_t i=0;i<instances.size();++i)threads.emplace_back([&,i]{while(!ready.load())std::this_thread::yield();instances[i]=LogManager::get();});
        ready=true;
        for(auto& thread:threads)thread.join();
        for(auto* instance:instances)assert(instance==instances[0]);
        return 0;
    }
    LogManager* logger=LogManager::get();
    logger->setVerbosity(LL_FATAL);
    const std::string file="logger-"+test+".log";
    if(test=="buffered")LOG_DEBUG("early retained sentinel");
    const bool disabled=test=="disabled" || test=="disabled_console";
    const bool failed_file=test=="failed_file" || test=="failed_file_console";
    const bool unavailable=disabled || failed_file;
    const bool console=test=="console" || test=="disabled_console" || test=="failed_file_console";
    std::string first_file=file;
    if(failed_file)
    {
        // An existing directory cannot be opened as an output file on any OS.
        const auto target=filesystem::current_path() / ("unwritable-"+test);
        filesystem::create_directories(target);
        first_file=target.u8string();
    }
    if(unavailable)LOG_DEBUG("discarded startup sentinel");
    logger->configure(json{{"logfile",first_file},{"loglevel",test=="buffered"?LL_DEBUG:LL_INFO},{"log_console",console},{"log_file",!disabled}},filesystem::current_path());
    logger->clearMessages();
    if(unavailable)
    {
        LOG_DEBUG("disabled hidden sentinel");
        LOG_INFO("disabled info sentinel");
        if(console)
        {
            const auto messages=logger->messages();
            assert(messages.size()==2);
            assert(messages[0]->buffer=="disabled hidden sentinel");
            assert(messages[1]->buffer=="disabled info sentinel");
        }
        std::ostringstream capture;
        auto* previous=std::cout.rdbuf(capture.rdbuf());
        logger->setVerbosity(LL_INFO);
        LOG_INFO("unavailable stdout sentinel");
        int dialogs=0;
        logger->RegisterDialogShowCallback(dialog,&dialogs);
        LOG_DIALOG("unavailable dialog sentinel");
        LOG_FATAL("unavailable fatal sentinel");
        LOG_DEBUG("unavailable escalation sentinel");
        logger->UnregisterDialogShowCallback(dialog,&dialogs);
        std::cout.rdbuf(previous);
        assert(dialogs==1);
        for(const char* expected:{"unavailable stdout sentinel","unavailable dialog sentinel","unavailable fatal sentinel","unavailable escalation sentinel"})
            assert(capture.str().find(expected)!=std::string::npos);
        logger->setVerbosity(LL_FATAL);
        logger->configure(json{{"logfile",file},{"loglevel",LL_DEBUG},{"log_file",true}},filesystem::current_path());
        LOG_INFO("enabled sentinel");
    }
    else if(test=="filtered")
    {
        LOG_DEBUG("hidden sentinel");
        LOG_WARNING("visible warning sentinel");
        logger->setLoglevel(LL_DEBUG);
        LOG_DEBUG("later visible sentinel");
    }
    else if(test=="console")
    {
        LOG_DEBUG("console retained sentinel");
        auto messages=logger->messages();
        assert(messages.size()==1 && messages[0]->buffer=="console retained sentinel");
    }
    else if(test=="stdout")
    {
        logger->setVerbosity(LL_DEBUG);
        std::ostringstream capture;
        auto* previous=std::cout.rdbuf(capture.rdbuf());
        LOG_DEBUG("stdout retained sentinel");
        std::cout.rdbuf(previous);
        assert(capture.str().find("stdout retained sentinel")!=std::string::npos);
    }
    else if(test=="critical")
    {
        int dialogs=0;logger->RegisterDialogShowCallback(dialog,&dialogs);
        LOG_DIALOG("dialog retained sentinel");
        assert(dialogs==1);
        LOG_FATAL("fatal retained sentinel");
        LOG_DEBUG("after fatal sentinel");
    }
    else assert(test=="buffered");
    logger->flush();
    std::ifstream input(filesystem::path("logs") / file);
    const std::string contents((std::istreambuf_iterator<char>(input)),{});
    if(unavailable)
    {
        assert(contents.find("disabled hidden sentinel")==std::string::npos);
        assert(contents.find("disabled info sentinel")==std::string::npos);
        assert(contents.find("discarded startup sentinel")==std::string::npos);
        assert(contents.find("unavailable ")==std::string::npos);
        assert(contents.find("enabled sentinel")!=std::string::npos);
    }
    if(test=="buffered")assert(contents.find("early retained sentinel")!=std::string::npos);
    if(test=="filtered")
    {
        assert(contents.find("hidden sentinel")==std::string::npos);
        assert(contents.find("visible warning sentinel")!=std::string::npos);
        assert(contents.find("later visible sentinel")!=std::string::npos);
    }
    if(test=="critical")
    {
        assert(contents.find("dialog retained sentinel")!=std::string::npos);
        assert(contents.find("fatal retained sentinel")!=std::string::npos);
        assert(contents.find("after fatal sentinel")!=std::string::npos);
    }
}
