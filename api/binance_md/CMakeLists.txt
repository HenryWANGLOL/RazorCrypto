cmake_minimum_required(VERSION 3.10)
project(binance_md)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项，生成共享库
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
add_compile_options(-fPIC -O2)

include_directories(
    ${CoreServiceInclude}
)

include_directories(
    /home/op/RazorCrypto/mht/include/rapidjson
)

link_libraries(mht_base)

# 链接依赖库
target_link_libraries(binance_md PRIVATE 
    rt
    pthread
    dl
    OpenSSL::SSL
    OpenSSL::Crypto
)


find_package(OpenSSL REQUIRED)

# 添加源文件
set(SOURCE_FILES src/binance_md.cpp) 

# 生成共享库
add_library(binance_md SHARED ${SOURCE_FILES})

link_directories(/home/op/RazorCrypto/lib/)


# 设置库的输出目录(可选)
set_target_properties(binance_md PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${BinReleaseFolder}
)