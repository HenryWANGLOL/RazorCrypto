cmake_minimum_required(VERSION 3.10)
project(binance_md)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
add_compile_options(-fPIC -O2)

# ===== 找 OpenSSL =====
find_package(OpenSSL REQUIRED)

# ===== 头文件路径 =====
include_directories(
    /home/opp/RazorCrypto/mht/include
    /home/opp/RazorCrypto/mht/include/rapidjson
)

# ===== 源文件 =====
set(SOURCE_FILES
    src/binance_md.cpp
)

# ===== 链接库路径 =====
link_directories(/home/opp/RazorCrypto/lib)

# ===== 生成共享库 =====
add_library(binance_md SHARED ${SOURCE_FILES})

# ===== 链接依赖 =====
target_link_libraries(binance_md PRIVATE
    mht_base
    rt
    pthread
    dl
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ===== 输出目录（可选）=====
set_target_properties(binance_md PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BinReleaseFolder}
)
