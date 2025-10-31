#include "RT_CryptoMDBase.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <thread>
#include <iostream>
#include <chrono>
using namespace std;
using namespace mht_rt;
using namespace rapidjson;


RT_CryptoMDBase::RT_CryptoMDBase(Document& json_config) : RTModuleBase(json_config) {
    logInfo("======================SUNNY TRADE MD INIT =========================");
}

RT_CryptoMDBase::~RT_CryptoMDBase() {
    // 析构时自动停止，确保资源释放
    stop();
}

int RT_CryptoMDBase::initialize() {
    // 调用子类的具体连接逻辑
    int ret = request_connect();
    if (ret != 0) {
        logError("行情连接初始化失败");
        return ret;
    }

    // 等待连接完成
    while (!m_bDoneConnect) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 缩短等待间隔，提高响应速度
    }
    logInfo("行情连接初始化完成");
    return 0;
}

void RT_CryptoMDBase::start() {
    if (m_running) {
        logWarn("行情已在运行中，无需重复启动");
        return;
    }

    m_running = true;
    // 启动独立线程运行行情循环，避免阻塞主线程
    m_thread = std::make_unique<std::thread>([this]() {
        logInfo("行情接收线程启动");
        while (m_running) {  // 由m_running控制循环退出
            // 空循环仅为示例，实际可添加行情心跳检测等逻辑
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        logInfo("行情接收线程退出");
    });
}

void RT_CryptoMDBase::stop() {
    if (!m_running) {
        logWarn("行情已停止，无需重复调用");
        return;
    }

    logInfo("开始停止行情模块...");
    // 1. 标记循环退出
    m_running = false;

    // 2. 等待线程结束（最多等待1秒）
    if (m_thread && m_thread->joinable()) {
        const auto start_time = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(1);
        bool joined = false;

        // 循环等待，每次检查间隔10ms
        while (std::chrono::steady_clock::now() - start_time < timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!m_thread->joinable()) {  // 线程已执行完毕
                joined = true;
                break;
            }
        }

        // 最终尝试join（无论是否超时）
        if (m_thread->joinable()) {
            try {
                m_thread->join();
                joined = true;
            } catch (const std::exception& e) {
                logError("线程join失败: {}", e.what());
            }
        }

        if (joined) {
            logInfo("行情线程已正常停止");
        } else {
            logWarn("行情线程停止超时（超过1秒）");
        }
        m_thread.reset();  // 释放线程指针
    }


    on_stop();

    logInfo("行情模块已完全停止");
}