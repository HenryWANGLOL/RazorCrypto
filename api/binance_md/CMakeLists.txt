cmake_minimum_required(VERSION 3.10)
project(binance_md)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项，生成共享库
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
add_compile_options(-fPIC)

include_directories(
    ${CoreServiceInclude}
)

find_package(OpenSSL REQUIRED)

# 添加源文件
set(SOURCE_FILES src/binance_md.cpp) 

# 生成共享库
add_library(binance_md SHARED ${SOURCE_FILES})


# 链接依赖库
target_link_libraries(binance_md PRIVATE 
    ${CoreServiceLib}/libmht_base.a
    rt
    pthread
    dl
    OpenSSL::SSL
    OpenSSL::Crypto

)

# 设置库的输出目录(可选)
set_target_properties(binance_md PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BinReleaseFolder}
)