cmake_minimum_required(VERSION 3.10)
project(MDRunner)

set(CMAKE_BUILD_TYPE Release)
# 对于多配置生成器（如Visual Studio），也可设置全局优化
set(CMAKE_CONFIGURATION_TYPES Release)

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY BinReleaseFolder)
# 如果需要，设置静态库输出目录
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# 如果需要，设置共享库输出目录
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用链接时优化（LTO），跨文件优化，显著提升性能
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

# 编译定义：确保C++ ABI兼容（保持原配置）
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

include_directories(
    ${CoreServiceInclude}
)

find_package(OpenSSL REQUIRED)

# 添加可执行文件目标
add_executable(MDRunner src/mdrunner.cpp)

# 链接依赖库
target_link_libraries(MDRunner 
    PRIVATE 
    dl        # 动态链接库（用于dlopen等函数）
    rt
    pthread
    ${CoreServiceLib}/libmht_base.a
    OpenSSL::SSL
    OpenSSL::Crypto

)

# 可选：设置编译选项（根据需要添加）
target_compile_options(MDRunner PRIVATE -Wall -Wextra -g)

set_target_properties(MDRunner PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${BinReleaseFolder}
    LIBRARY_OUTPUT_DIRECTORY ${BinReleaseFolder}
    RUNTIME_OUTPUT_DIRECTORY ${BinReleaseFolder}
)