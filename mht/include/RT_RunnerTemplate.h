#include <iostream>
#include <unistd.h>  // 包含 getop
#include <yaml-cpp/yaml.h>
#include "ST_StrategyEngine.h"
#include "ST_StgInterface.h"


template <typename MyStrategy>
int Stg_Runner(int argc, char* argv[]) {
    int opt;
    std::string configFile = ""; 
    // 解析命令行参数
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
            case 'c':
                configFile = optarg;
                break;
            case '?':
                std::cerr << "未知参数: " << optopt << std::endl;
                return 1;
            default:
                abort();
        }
    }

    // 从 YAML 文件加载配置
    YAML::Node config = YAML::LoadFile(configFile);
    // 这里需要确保同时传递 stg_creator 和 config
    StrategyEngine::start_with_creator(
        [&config](std::shared_ptr<StrategyEngine> se, const YAML::Node& yml) {
            return std::unique_ptr<StgInterface>(new MyStrategy(se, yml)); // 使用 yml
        },
        config
    );

    return 0;
}