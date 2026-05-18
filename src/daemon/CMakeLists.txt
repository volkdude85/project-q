cmake_minimum_required(VERSION 3.20)
project(project-qd VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)
find_package(Threads REQUIRED)

set(SOURCES
    main.cpp
    config.cpp
    protocol.cpp
    coordinator.cpp
    worker.cpp
    taskrunner.cpp
    queue_sync.cpp
)

set(HEADERS
    config.h
    protocol.h
    coordinator.h
    worker.h
    taskrunner.h
    queue_sync.h
)

add_executable(project-qd ${SOURCES} ${HEADERS})

target_link_libraries(project-qd PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Threads::Threads
)

target_include_directories(project-qd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Install targets for systemd service
install(TARGETS project-qd RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
