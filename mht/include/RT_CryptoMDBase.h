#ifndef RT_CRYPTO_MD_BASE_H
#define RT_CRYPTO_MD_BASE_H

#include "rapidjson/document.h"
#include "RT_ModuleBase.h"  // 假设RTModuleBase来自这里
#include <atomic>
#include <thread>
#include <memory>

namespace mht_rt {

class RT_CryptoMDBase : public RTModuleBase {
public:
    // 构造函数
    explicit RT_CryptoMDBase(rapidjson::Document& json_config);

    // 析构函数（确保子类资源被释放）
    ~RT_CryptoMDBase() override;

    // 初始化（连接行情源）
    int initialize();

    // 启动行情接收（在独立线程中运行）
    void start();

    // 停止行情接收（核心新增功能）
    void stop();

    virtual void init_shm() = 0;

protected:
    // 纯虚函数：子类实现具体的连接逻辑
    virtual int request_connect() = 0;

    // 虚函数：子类重写以释放自身资源（如WebSocket连接、共享内存等）
    virtual void on_stop() {}

    std::atomic<bool> m_bDoneConnect{false};  // 连接是否完成
    std::atomic<bool> m_running{false};       // 控制行情循环的原子变量
    std::unique_ptr<std::thread> m_thread;    // 运行行情循环的线程
};

}  // namespace mht_rt

#endif  // RT_CRYPTO_MD_BASE_H