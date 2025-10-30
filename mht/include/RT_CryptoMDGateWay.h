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
#include <boost/algorithm/string.hpp>
#include <chrono>
#include "RT_Misc.h"
#include "RT_CryptoMDBase.h"
#include "RT_Common.h"
#include "RT_MQ.h"
#include "csv.h"
#define assertm(exp, msg) assert(((void)msg, exp))
using CreateMDLibFunc = std::unique_ptr<RT_CryptoMDBase>(*)(rapidjson::Document&);

class RT_CryptoGateWay {
public:
    RT_CryptoGateWay(rapidjson::Document json_config) {

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

        
    }

    ~RT_CryptoGateWay() {

    }

    int start() {
        for (int i = 0; i < connections.size(); ++i) {
            rapidjson::Document& conn = connections[i];
            printf("connecting to %s\n", conn["lib_path"].GetString());
            void* handle = dlopen(conn["lib_path"].GetString(), RTLD_LAZY);
            assertm(handle, dlerror());
            if (!handle) {
                std::cerr << "无法加载动态库: " << dlerror() << std::endl;
                return 1;
            }

            CreateMDLibFunc create_mdlib = (CreateMDLibFunc)dlsym(handle, "create_mdlib");
            if (!create_mdlib) {
                std::cerr << "无法获取函数 create_mdlib: " << dlerror() << std::endl;
                dlclose(handle);
                return 1;
            }

            std::unique_ptr<RT_CryptoMDBase> md = create_mdlib(conn);
            if (!md) {
                std::cerr << "创建 mdlib 实例失败！" << std::endl;
                dlclose(handle);
                return 1;
            }

            // md->init_shm();
            md->initialize();
            md->start();
        }
        return 1;
    }

private:
    std::vector<rapidjson::Document> connections;
};