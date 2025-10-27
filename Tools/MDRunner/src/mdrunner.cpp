#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

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
#include "RT_CryptoMDGateWay.h"
#include "csv.h"

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

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json_str = buffer.str();

    Document config;
    ParseResult ok = config.Parse(json_str.c_str());
    if (!ok) {
        std::cerr << "JSON 解析错误: " << GetParseError_En(ok.Code())
                  << " (offset " << ok.Offset() << ")" << std::endl;
        return 1;
    }

    RT_CryptoGateWay gateway(std::move(config));
    gateway.start();

    return 0;
}