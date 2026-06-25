cmake_minimum_required(VERSION 3.10)
project(TinyWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项：Debug 为默认，可用 cmake -DCMAKE_BUILD_TYPE=Release 切换
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 查找 pthread
find_package(Threads REQUIRED)

# 查找 MySQL
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MYSQL QUIET mysqlclient)
endif()

if(NOT MYSQL_FOUND)
    # 回退：使用 mysql_config
    find_program(MYSQL_CONFIG_EXECUTABLE mysql_config)
    if(MYSQL_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND ${MYSQL_CONFIG_EXECUTABLE} --include
            OUTPUT_VARIABLE MYSQL_INCLUDE_DIRS
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REPLACE "-I" "" MYSQL_INCLUDE_DIRS "${MYSQL_INCLUDE_DIRS}")

        execute_process(
            COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs
            OUTPUT_VARIABLE MYSQL_LIBRARIES
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # 拆分 libs 字符串为列表
        separate_arguments(MYSQL_LIBRARIES UNIX_COMMAND "${MYSQL_LIBRARIES}")
    endif()
endif()

if(NOT MYSQL_INCLUDE_DIRS OR NOT MYSQL_LIBRARIES)
    message(FATAL_ERROR "找不到 MySQL 客户端库，请安装 libmysqlclient-dev")
endif()

message(STATUS "MySQL include: ${MYSQL_INCLUDE_DIRS}")
message(STATUS "MySQL libs:    ${MYSQL_LIBRARIES}")

# 源文件
set(SOURCES
    main.cpp
    config.cpp
    webserver.cpp
    timer/lst_timer.cpp
    http/http_conn.cpp
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
)

# 可执行文件
add_executable(server ${SOURCES})

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MYSQL_INCLUDE_DIRS}
)

target_link_libraries(server PRIVATE
    Threads::Threads
    ${MYSQL_LIBRARIES}
)
