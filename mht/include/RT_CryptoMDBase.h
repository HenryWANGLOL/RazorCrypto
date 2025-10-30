/*************************************************************************************
ST_MddApiInterface.h

包含了行情接口的一些定义和接口函数

Created by YufengWang, 20241226
Copyright (c) 2021 Trigma,Inc. All rights reserved.
*****************************************************************************/

#pragma once
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
// #include "RT_TradeDataStruct.h"
#include "RT_MQ.h"
#include "RT_ShmMdPlus.h"
#include "RT_ModuleBase.h"
#include "K4_WsSslClient.h"


#include <iostream>
#include <cassert>
using namespace std;

class RT_CryptoMDBase : public RTModuleBase
{
public:
    // 构造函数
    RT_CryptoMDBase() = default;

    // 带配置参数的构造函数
    RT_CryptoMDBase(rapidjson::Document &json_config);

    // 析构函数
    ~RT_CryptoMDBase() = default;

    // 请求连接行情数据源，纯虚函数，子类必须实现
    virtual int request_connect() = 0;

    // 启动行情接口
    void start();

    // 初始化行情接口，返回初始化结果
    int initialize();



protected:

    int init_shm();
    std::string mdmqname;
    uint64_t default_md_size;
    bool m_bDoneConnect = false;
};