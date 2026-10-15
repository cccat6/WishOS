#pragma once
#include <cstddef>
#include <atomic>
#include <thread>
namespace fs {struct LittleFSFS {
 std::atomic<bool>*hold=nullptr;bool failBegin=false;unsigned begins=0,ends=0;
 size_t totalBytes(){return 1000000;}
 size_t usedBytes(){while(hold&&hold->load())std::this_thread::yield();return 0;}
 bool begin(bool,const char*,unsigned,const char*){++begins;return !failBegin;}
 void end(){++ends;}
};}
