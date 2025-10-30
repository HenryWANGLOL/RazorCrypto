
/*****************************************************************************
ST_Misc.h
本文件定义了一些不需要的杂乱的方法

Created by YufengWang, 20241225
Copyright (c) 2021 Trigma,Inc. All rights reserved.
*****************************************************************************/
#pragma once
#include <iostream>
#include <yaml-cpp/yaml.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iconv.h>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <chrono>


#include <iostream>
using namespace std;
namespace sunny_utils
{

    inline unsigned long long getCPUTick()
    {
        unsigned int lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return ((unsigned long long)hi << 32) | lo;
    }
    void printYAML(const YAML::Node &node, int indent);
    bool makedirs(const std::string &folder_path);
    std::string rename_YYYYMMDDfile(const std::string &src);
    std::string getParentFolder(const std::string &filename);


        // 假设 StrategyEngine 和 StgInterface 是已经定义的类

} // namespace my_utils
