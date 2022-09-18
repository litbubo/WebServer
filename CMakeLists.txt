cmake_minimum_required(VERSION 3.14)

project(WebServer)

set(CMAKE_CXX_STANDARD 11)

set (CMAKE_C_COMPILER "/usr/bin/gcc")

set (CMAKE_CXX_COMPILER "/usr/bin/g++")

add_compile_options(-O3 -Wall -m64)

# 指定执行程序的输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)

# 添加寻找FindSFML.cmake文件的路径
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmakeModule/;${CMAKE_MODULE_PATH};")

# 查找第三方库
find_package(Threads REQUIRED)
find_package(MySQL REQUIRED)

aux_source_directory(${PROJECT_SOURCE_DIR}/codes/src DIR_FILE)

add_executable(WebServer ${DIR_FILE} ${PROJECT_SOURCE_DIR}/codes/main.cpp)

# 添加头文件所在路径，这时候cpp中就可以直接引用，而不用管路径了
target_include_directories(WebServer PUBLIC ${PROJECT_SOURCE_DIR}/codes/inc ${MYSQL_INCLUDE_DIR})

# 添加pthread,mysql支持
target_link_libraries(WebServer PUBLIC Threads::Threads ${MYSQL_LIB})
