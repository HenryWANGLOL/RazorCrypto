/*
Logger Base
*/
#pragma once
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <mutex>
#include <vector>

#define DEFAULT_LOG_PATTERN "[%H:%M:%S.%f][%l]%v"
namespace mht_rt
{

    static std::shared_ptr<spdlog::logger> error_warn_console_stdout;

    static spdlog::level::level_enum get_env_log_level(const std::string &str)
    {
        try
        {
            return spdlog::level::from_str(str);
        }
        catch (...)
        {
            return spdlog::level::info; // 默认 fallback
        }
    }

    static std::shared_ptr<spdlog::logger> get_main_logger()
    {
        return spdlog::default_logger();
    }

    static void setup_log(const std::string &filename, const std::string &log_lv)
    {
        static std::once_flag log_init_flag;
        std::call_once(log_init_flag, [&]()
                        {
        // 初始化异步线程池：队列大小 8192，线程数 1
        spdlog::init_thread_pool(8192, 1);

        // 控制台输出（仅输出 warn 及以上）
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::warn);

        // 每日文件输出 sink
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(filename, 0, 0);

        // 所有 sink 汇总
        std::vector<spdlog::sink_ptr> sinks{ file_sink, console_sink };

        // 创建异步 logger
        auto logger = std::make_shared<spdlog::async_logger>(
            "async_file_logger",
            sinks.begin(), sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );

        // 设置日志输出格式
        logger->set_pattern(DEFAULT_LOG_PATTERN);

        // 设置日志等级
        spdlog::level::level_enum env_log_level = get_env_log_level(log_lv);
        logger->set_level(env_log_level);

        // 设置为默认 logger
        spdlog::set_default_logger(logger);

        // 控制台 logger 单独保留（只输出 warn 及以上）
        spdlog::sinks_init_list log_sinks = { console_sink };
        error_warn_console_stdout = std::make_shared<spdlog::logger>("console", log_sinks);

        // debug 级别以上立即刷新日志（如写文件）
        spdlog::flush_on(spdlog::level::debug);

        // 每 2 秒强制刷新日志
        spdlog::flush_every(std::chrono::seconds(2)); });
    }

    inline void copy_log_settings(const std::string &name, const std::string log_lv)
    {
        if (get_main_logger()->name().empty())
        {
            setup_log(name, log_lv);
        }
        auto logger_cloned = get_main_logger()->clone(name);
        spdlog::set_default_logger(logger_cloned);
    }

} // namespace mht