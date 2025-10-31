/*************************************************************************************
K4_Export.h
定义如何使用创建so文件
Created by YufengWang, 20241226
*****************************************************************************/

// Created by Yufeng Wang
#pragma once
#include "rapidjson/document.h"

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
    classname * DLL_EXPORT create_mdlib(const rapidjson::Document& json_config) { \
        return new classname(config); \
    } \
}