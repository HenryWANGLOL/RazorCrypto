
cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(K4TD)
if(NOT MSVC) 
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format")
endif()

get_filename_component(ProjectFolder ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

add_subdirectory(binance_md)

