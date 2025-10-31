cmake_minimum_required(VERSION 3.10)
project(BN_MD)

# 1. 基础配置：C++标准、PIC（生成共享库必需）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # 自动为共享库添加 -fPIC，无需重复写


# 3. 编译选项：移除冗余 -fPIC，处理 ABI（建议与依赖库保持一致）
# 注：若依赖库（如 libmht_base.a）用旧版 ABI，保留 _GLIBCXX_USE_CXX11_ABI=0；否则删除此宏（用默认新版 ABI）
# add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)  # 根据依赖库 ABI 决定是否保留
add_compile_options(-O2 -Wall -Wextra)  # 增加警告选项，提前发现代码问题

# 4. 头文件搜索路径（确保变量已初始化）
include_directories(
    ${CoreServiceInclude}
    ${OPENSSL_INCLUDE_DIR}  # 显式添加 OpenSSL 头文件路径（find_package 会自动设置）
)

# 5. 查找依赖：OpenSSL（确保系统已安装 libssl-dev 或 openssl-devel）
find_package(OpenSSL REQUIRED)
if(NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL 未找到，请安装 OpenSSL 开发包（如：sudo apt install libssl-dev）")
endif()

# 6. 源文件配置
set(SOURCE_FILES src/binance_md.cpp)  # 确保 src/binance_md.cpp 路径正确

# 7. 创建共享库目标（关键：先创建目标，再链接）
add_library(BN_MD SHARED ${SOURCE_FILES})

# 8. 链接依赖库（移除冗余 rt 库，顺序：目标 -> 依赖）
target_link_libraries(BN_MD PRIVATE 
    pthread  # 线程库（必需，若代码用了线程）
    dl       # 动态链接库（若代码用了 dlopen 等）
    OpenSSL::SSL
    OpenSSL::Crypto
    /root/RazorCrypto/lib/libmht_base.a  # 静态依赖库（确保路径正确）
)

# 9. 设置库输出目录（确保变量已初始化）
set_target_properties(BN_MD PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BinReleaseFolder}  # 共享库输出到指定目录
)

# 可选：打印配置信息，方便调试
message(STATUS "==================== 配置 summary ====================")
message(STATUS "C++ 标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "头文件路径: ${CoreServiceInclude}")
message(STATUS "库输出路径: ${BinReleaseFolder}")
message(STATUS "OpenSSL 版本: ${OPENSSL_VERSION}")
message(STATUS "=======================================================")