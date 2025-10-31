/*************************************************************************************
K4_Export.h
定义如何使用创建so文件
Created by YufengWang, 20241226
*****************************************************************************/

// Created by Yufeng Wang
#pragma once
#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    #ifdef XTRADE_EXPORTS
        #define DLL_EXPORT __declspec(dllexport)
    #else
        #define DLL_EXPORT __declspec(dllimport)
    #endif
#else
    #define DLL_EXPORT
#endif

// 修正后的宏定义，确保最后一个\后面有内容
#define K4TRADE_CREATE_INSTANCE(classname)  \
extern "C" { \
    classname * DLL_EXPORT create_module(YAML::Node &config) { \
        return new classname(config); \
    } \
}