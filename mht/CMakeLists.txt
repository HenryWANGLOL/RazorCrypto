cmake_minimum_required(VERSION 3.10)
project(mht_base)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(${CoreServiceInclude})
message(STATUS "include folder ${CoreServiceInclude}")

include_directories(${CoreServiceInclude}/rapidjson)

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
message(STATUS "当前SOURCE_DIR路径: ${SOURCE_DIR}")

file(GLOB_RECURSE SOURCES
    "${SOURCE_DIR}/RT_Exception.cpp"
    "${SOURCE_DIR}/RT_ModuleBase.cpp"
    "${SOURCE_DIR}/RT_CryptoMDBase.cpp"
)

add_library(mht_base STATIC ${SOURCES})

target_link_libraries(mht_base PUBLIC
    rt
    pthread
    dl
)

target_include_directories(mht_base PUBLIC
    ${CoreServiceInclude}
)

target_compile_options(mht_base PUBLIC -Wall -Wextra -g)

set_target_properties(mht_base PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CoreServiceLib}
    LIBRARY_OUTPUT_DIRECTORY ${CoreServiceLib}
)

install(TARGETS mht_base
    ARCHIVE DESTINATION ${CoreServiceLib}
)
