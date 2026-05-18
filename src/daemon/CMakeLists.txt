# Project Q Daemon — Persistent C++ Farm Mesh
# Replaces SSH polling with UDP heartbeat + TCP command channel
cmake_minimum_required(VERSION 3.20)
project(project-qd VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SQLite3 — bundled via CPPSQLite or system package
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 sqlite3)
endif()
if(NOT SQLITE3_FOUND)
    find_library(SQLITE3_LIBRARIES sqlite3)
    find_path(SQLITE3_INCLUDE_DIRS sqlite3.h)
endif()

add_executable(project-qd
    main.cpp
    config.cpp
    protocol.cpp
    coordinator.cpp
    worker.cpp
    taskrunner.cpp
    queue_sync.cpp
)

target_include_directories(project-qd PRIVATE ${SQLITE3_INCLUDE_DIRS})
target_link_libraries(project-qd PRIVATE ${SQLITE3_LIBRARIES} pthread)

# Install
install(TARGETS project-qd RUNTIME DESTINATION bin)
install(FILES project-qd-coordinator.service project-qd@.service DESTINATION lib/systemd/system)
