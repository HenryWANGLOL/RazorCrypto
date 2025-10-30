/*****************************************************************************
K4_modulebase.h
K4Trader 所有模块的基类
Created by Yufeng Wang
20250625
*****************************************************************************/
#pragma once

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <sched.h>
#include <cerrno>
#include <cstring>
#include "RT_LoggerBase.h"  // 日志库头文件
#include "RT_Common.h"
namespace mht_rt {
class RTModuleBase {
public:
    RTModuleBase(rapidjson::Document &json_config);
    virtual ~RTModuleBase();

    // 设定模块 ID
    void set_id(const int &id);


    const std::string getModuleName() const;

    // 日志访问接口
    const std::shared_ptr<spdlog::logger>& getLogger() const;

    // 日志便捷方法
    template<typename... Args>
    void logDebug(const char* fmt, const Args&... args) const {
        if (logger_) logger_->debug(fmt, args...);
    }

    template<typename... Args>
    void logInfo(const char* fmt, const Args&... args) const {
        if (logger_) logger_->info(fmt, args...);
    }

    template<typename... Args>
    void logWarn(const char* fmt, const Args&... args) const {
        if (logger_) logger_->warn(fmt, args...);
    }

    template<typename... Args>
    void logError(const char* fmt, const Args&... args) const {
        if (logger_) logger_->error(fmt, args...);
    }

    template<typename... Args>
    void logCritical(const char* fmt, const Args&... args) const {
        if (logger_) logger_->critical(fmt, args...);
    }

    std::string get_compile_date_and_time() const;

    bool runInNewThread() const;
    bool isDisabled() const;

protected:

    rapidjson::Document *m_config;
    static std::shared_ptr<spdlog::logger> logger_;

    static void initLogger(const std::string& log_path);

    static const rapidjson::Document* getGlobalConfig();

    bool setCpuAffinity(const std::vector<int>& cpu_cores);

    bool configureCpuAffinityFromJson();

    void on_bold();

    int m_id;

private:
    bool verifyCpuAffinity(const cpu_set_t& cpuset);

    // 成员变量
    std::string m_module_name;
    bool m_bRunInNewThread;    // 是否在新线程中运行
    bool m_bDisabled;          // 是否被禁用

};
};