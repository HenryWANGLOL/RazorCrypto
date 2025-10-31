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
#include <thread>
#include <vector>
#include <dlfcn.h>
#include <csignal>
#include <mutex>
#include <condition_variable>

#include <boost/algorithm/string.hpp>
#include <chrono>

// 项目头文件
#include "RT_Misc.h"
#include "RT_CryptoMDBase.h"
#include "RT_Common.h"  // 假设提供logInfo/logError等日志函数
#include "RT_MQ.h"
#include "csv.h"

// 宏定义：带消息的断言（ Release模式建议替换为日志+返回 ）
#define assertm(exp, msg) do { \
    if (!(exp)) { \
        logError("断言失败: {}", msg); \
        assert(false); \
    } \
} while(0)

using CreateMDLibFunc = std::unique_ptr<mht_rt::RT_CryptoMDBase>(*)(rapidjson::Document&);
using namespace rapidjson;
using namespace mht_rt;
using namespace std;

// 全局状态：控制程序退出
static std::atomic<bool> g_should_exit(false);
static std::mutex g_exit_mutex;
static std::condition_variable g_exit_cv;


// 新增：日志函数实现（替代未定义的logError/logInfo/logWarn）
#include <ctime>
#include <string>

// 获取当前时间字符串（用于日志）
std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&now_time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

// 错误日志
void logError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::cout << "[" << get_current_time() << "] [ERROR] ";
    vprintf(format, args);
    std::cout << std::endl;
    va_end(args);
}

// 信息日志
void logInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::cout << "[" << get_current_time() << "] [INFO] ";
    vprintf(format, args);
    std::cout << std::endl;
    va_end(args);
}

// 警告日志
void logWarn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::cout << "[" << get_current_time() << "] [WARN] ";
    vprintf(format, args);
    std::cout << std::endl;
    va_end(args);
}
// 信号处理：捕获Ctrl+C等退出信号
void signal_handler(int signum) {
    logWarn("收到退出信号({})，准备停止程序...", signum);
    g_should_exit = true;
    g_exit_cv.notify_all();  // 唤醒等待的主线程
}

struct MDResource {
    void* handle = nullptr;
    std::unique_ptr<RT_CryptoMDBase> md_instance;

    // 禁止拷贝（必须显式删除，避免编译器自动生成）
    MDResource(const MDResource&) = delete;
    MDResource& operator=(const MDResource&) = delete;

    // 允许移动（显式定义移动构造和移动赋值）
    MDResource(MDResource&& other) noexcept
        : handle(other.handle), md_instance(std::move(other.md_instance)) {
        other.handle = nullptr; // 避免被移动的对象析构时释放资源
    }

    MDResource& operator=(MDResource&& other) noexcept {
        if (this != &other) {
            handle = other.handle;
            md_instance = std::move(other.md_instance);
            other.handle = nullptr; // 避免被移动的对象析构时释放资源
        }
        return *this;
    }

    // 默认构造函数
    MDResource() = default;

    // 析构时释放资源
    ~MDResource() {
        if (md_instance) {
            try {
                md_instance->stop();
                logInfo("MD实例已停止");
            } catch (const std::exception& e) {
                logError("停止MD实例失败: %s", e.what());
            }
        }
        if (handle) {
            dlclose(handle);
            logInfo("动态库已关闭");
        }
    }
};

int main(int argc, char* argv[]) {
    // 注册信号处理（捕获Ctrl+C、kill等信号）
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    // 检查命令行参数
    if (argc < 2) {
        logError("用法: {} <config.json>", argv[0]);
        return 1;
    }

    // 加载配置文件
    std::string json_path = argv[1];
    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        logError("无法打开配置文件: {}", json_path);
        return 1;
    }

    // 读取配置内容
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json_str = buffer.str();
    ifs.close();

    // 解析JSON配置
    Document json_config;
    ParseResult ok = json_config.Parse(json_str.c_str());
    if (!ok) {
        logError("JSON解析错误: {} (offset {})", GetParseError_En(ok.Code()), ok.Offset());
        return 1;
    }

    // 提取connections数组
    if (!json_config.HasMember("connections") || !json_config["connections"].IsArray()) {
        logError("配置文件中未找到有效的\"connections\"数组");
        return 1;
    }
    const Value& connections_arr = json_config["connections"];
    if (connections_arr.Size() == 0) {
        logWarn("配置文件中\"connections\"数组为空，无需加载任何实例");
        return 0;
    }

    // 管理所有MD资源（动态库句柄+实例）
    std::vector<MDResource> md_resources;

    // 循环加载每个连接配置
    for (SizeType i = 0; i < connections_arr.Size(); ++i) {
        const Value& conn = connections_arr[i];
        try {
            // 检查配置是否包含lib_path
            if (!conn.HasMember("lib_path") || !conn["lib_path"].IsString()) {
                logError("第{}个连接配置缺少有效的\"lib_path\"", i);
                continue;  // 跳过无效配置，继续处理下一个
            }
            std::string lib_path = conn["lib_path"].GetString();
            printf("开始加载第%d个连接: %s \n", i, lib_path.c_str());

            // 复制子配置（避免引用失效）
            Document sub_doc;
            sub_doc.CopyFrom(conn, sub_doc.GetAllocator());

            // 加载动态库
            void* handle = dlopen(lib_path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
            if (!handle) {
                logError("第{}个连接加载动态库失败: {}", i, dlerror());
                continue;
            }


            CreateMDLibFunc create_mdlib = (CreateMDLibFunc)dlsym(handle, "create_mdlib");
            if (!create_mdlib) {
                logError("第{}个连接获取create_mdlib函数失败: {}", i, dlerror());
                dlclose(handle);  // 释放已打开的句柄
                continue;
            }
            printf("md lib ok, 创建create lib\n");
            // 创建MD实例
            std::unique_ptr<RT_CryptoMDBase> md = create_mdlib(sub_doc);
            printf("create lib ok, initing shared mem\n");
            if (!md) {
                logError("第{}个连接创建MD实例失败", i);
                dlclose(handle);
                continue;
            }
        
            // 初始化实例
            md->init_shm();    // 初始化共享内存
            md->initialize();  // 通用初始化
            md->start();       // 启动行情接收

            // 保存资源
            MDResource res;
            res.handle = handle;
            res.md_instance = std::move(md);
            md_resources.emplace_back(std::move(res));

            printf("第%d个连接加载成功 \n", i);

        } catch (const std::exception& e) {
            printf("第%d个连接处理异常: %s \n", i, e.what());
            continue;  // 单个连接失败不影响其他
        }
    }

    logInfo("所有连接加载完成，共成功加载{}个实例", md_resources.size());

    // 主线程阻塞等待退出信号
    std::unique_lock<std::mutex> lock(g_exit_mutex);
    g_exit_cv.wait(lock, []{ return g_should_exit.load(); });

    // 程序退出前：资源会通过MDResource的析构函数自动释放
    logInfo("程序开始退出...");

    return 0;
}