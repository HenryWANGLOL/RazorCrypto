#include "RT_ModuleBase.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include <cstdio>

// 静态成员初始化
namespace mht_rt {

std::shared_ptr<spdlog::logger> RTModuleBase::logger_ = nullptr;

RTModuleBase::RTModuleBase(rapidjson::Document &json_config)
    : m_config(&json_config) {
    // ========== 1. 获取模块名 ==========
    if (json_config.HasMember("RazorTrade") && json_config["RazorTrade"].IsObject()) {
        const auto &razor = json_config["RazorTrade"];
        m_module_name = razor.HasMember("module_name") && razor["module_name"].IsString()
                            ? razor["module_name"].GetString()
                            : "UnnamedModule";

        std::string log_path = razor.HasMember("log_path") && razor["log_path"].IsString()
                                   ? razor["log_path"].GetString()
                                   : "weird";
        makeDirIfNotExist(log_path);
        log_path = log_path + "/" + m_module_name + ".log";
        initLogger(log_path);
    } else {
        m_module_name = "UnnamedModule";
        initLogger("weird.log");
    }

    // ========== 2. CPU亲和性配置 ==========
    if (!configureCpuAffinityFromJson()) {
        logger_->warn("CPU 绑核配置失败，模块将在任意 CPU 上运行");
    }

    // ========== 3. 读取其他配置 ==========
    m_id = json_config.HasMember("id") && json_config["id"].IsInt() ? json_config["id"].GetInt() : 1;
    m_bRunInNewThread = json_config.HasMember("run_in_new_thread") && json_config["run_in_new_thread"].IsBool()
                            ? json_config["run_in_new_thread"].GetBool()
                            : false;
    m_bDisabled = json_config.HasMember("disabled") && json_config["disabled"].IsBool()
                      ? json_config["disabled"].GetBool()
                      : false;

    logger_->info("基类模块初始化完成: {}", m_module_name);
}

RTModuleBase::~RTModuleBase() {
    if (logger_) {
        logger_->info("模块销毁: {}", m_module_name);
    }
}

void RTModuleBase::set_id(const int &id) {
    m_id = id;
    logger_->info("模块 ID 设置为: {}", id);
}

const std::string RTModuleBase::getModuleName() const { return m_module_name; }

const std::shared_ptr<spdlog::logger>& RTModuleBase::getLogger() const {
    return logger_;
}

std::string RTModuleBase::get_compile_date_and_time() const {
    return std::string(__DATE__ " " __TIME__);
}

bool RTModuleBase::runInNewThread() const { return m_bRunInNewThread; }
bool RTModuleBase::isDisabled() const { return m_bDisabled; }

void RTModuleBase::initLogger(const std::string& log_path) {
    if (!logger_) {
        if (get_main_logger()->name().empty()) {
            setup_log(log_path, "info");
        }
        logger_ = get_main_logger()->clone(log_path);
    }
}

// 全局配置（这里保持空实现）
const rapidjson::Document* RTModuleBase::getGlobalConfig() {
    static rapidjson::Document global_config;
    static bool initialized = false;
    if (!initialized) {
        global_config.SetObject();
        initialized = true;
    }
    return &global_config;
}

bool RTModuleBase::setCpuAffinity(const std::vector<int>& cpu_cores) {
    if (cpu_cores.empty()) {
        logger_->info("未指定 CPU 核心，不进行绑核");
        return true;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int core : cpu_cores) {
        if (core >= 0 && core < CPU_SETSIZE) {
            CPU_SET(core, &cpuset);
            logger_->info("将模块绑定到 CPU 核心: {}", core);
        } else {
            logger_->error("无效的 CPU 核心编号: {}", core);
            return false;
        }
    }

    pid_t pid = 0;  // 0 表示当前线程
    if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) == -1) {
        logger_->error("设置 CPU 亲和性失败: {}", strerror(errno));
        return false;
    }

    if (!verifyCpuAffinity(cpuset)) {
        logger_->warn("验证 CPU 亲和性失败，可能未生效");
    }
    logger_->info("绑核成功");
    return true;
}

// JSON版 CPU亲和性配置
bool RTModuleBase::configureCpuAffinityFromJson() {
    if (!m_config || !m_config->HasMember("cpu_affinity")) {
        return true;  // 未配置则不绑核
    }

    const auto &cpu_config = (*m_config)["cpu_affinity"];
    if (!cpu_config.IsArray()) {
        logger_->error("cpu_affinity 必须是数组类型");
        return false;
    }

    std::vector<int> cpu_cores;
    try {
        for (auto &core : cpu_config.GetArray()) {
            if (core.IsInt())
                cpu_cores.push_back(core.GetInt());
        }
        return setCpuAffinity(cpu_cores);
    } catch (const std::exception &e) {
        logger_->error("解析 CPU 亲和性配置失败: {}", e.what());
        return false;
    }
}

void RTModuleBase::on_bold() {}

bool RTModuleBase::verifyCpuAffinity(const cpu_set_t& cpuset) {
    cpu_set_t current_cpuset;
    CPU_ZERO(&current_cpuset);

    if (sched_getaffinity(0, sizeof(cpu_set_t), &current_cpuset) == -1) {
        logger_->error("获取当前 CPU 亲和性失败: {}", strerror(errno));
        return false;
    }

    for (int i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &cpuset) != CPU_ISSET(i, &current_cpuset)) {
            return false;
        }
    }
    return true;
}

} // namespace mht_rt
