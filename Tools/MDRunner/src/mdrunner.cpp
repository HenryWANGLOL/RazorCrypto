#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <ctime>
#include <cmath> 
#include <iomanip>
#include <sstream>
#include <cstring>
#include <memory>
#include <thread> // 添加线程库
#include <memory> // 确保包含智能指针支持
#include <dlfcn.h>

#include <boost/algorithm/string.hpp>
#include <chrono>
#include "RT_Misc.h"
#include "RT_CryptoMDBase.h"
#include "RT_Common.h"
#include "RT_MQ.h"
#include "csv.h"
#define assertm(exp, msg) assert(((void)msg, exp))
using CreateMDLibFunc = std::unique_ptr<RT_CryptoMDBase>(*)(rapidjson::Document&);
using namespace rapidjson;
using namespace mht_rt;
using namespace std;


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <config.json>" << std::endl;
        return 1;
    }

    std::string json_path = argv[1];
    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        std::cerr << "无法打开配置文件: " << json_path << std::endl;
        return 1;
    }

    std::vector<rapidjson::Document> connections;
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json_str = buffer.str();


    std::string lib_path = "/home/op/RazorCrypto/release/libbinance_md.so";
    void* handle = dlopen(lib_path.c_str(), RTLD_LAZY);
    assertm(handle, dlerror());
    if (!handle) {
        std::cerr << "无法加载动态库: " << dlerror() << std::endl;
        return 1;
    }
    printf("加载成功 \n");


    Document json_config;
    ParseResult ok = json_config.Parse(json_str.c_str());
    if (!ok) {
        std::cerr << "JSON 解析错误: " << GetParseError_En(ok.Code())
                  << " (offset " << ok.Offset() << ")" << std::endl;
        return 1;
    }

    if (json_config.HasMember("connections") && json_config["connections"].IsArray()) {
        for (const auto& conn : json_config["connections"].GetArray()) {
            rapidjson::Document subDoc;
            subDoc.CopyFrom(conn, subDoc.GetAllocator()); 
            connections.emplace_back(std::move(subDoc));
        }
    }
    else {
        std::cout << "no connections in the config" << std::endl;
        exit(0);
    }

    for (int i = 0; i < connections.size(); ++i) {
        rapidjson::Document& conn = connections[i];
        printf("connecting to %s\n", conn["lib_path"].GetString());
        void* handle = dlopen(lib_path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        assertm(handle, dlerror());
        if (!handle) {
            std::cerr << "无法加载动态库: " << dlerror() << std::endl;
            return 1;
        }
        printf("加载成功 x2\n");

        CreateMDLibFunc create_mdlib = (CreateMDLibFunc)dlsym(handle, "create_mdlib");
        if (!create_mdlib) {
            std::cerr << "无法获取函数 create_mdlib: " << dlerror() << std::endl;
            dlclose(handle);
            return 1;
        }
        printf("加载成功 x3\n");
        std::unique_ptr<RT_CryptoMDBase> md = create_mdlib(conn);
        if (!md) {
            std::cerr << "创建 mdlib 实例失败！" << std::endl;
            dlclose(handle);
            return 1;
        }
        printf("加载成功 x4\n");
        // md->init_shm();
        md->initialize();
        md->start();
    }




    return 0;
}