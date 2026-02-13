cmake_minimum_required(VERSION 3.10)
project(LinuxTaskManager)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Include directories
include_directories(${GTK3_INCLUDE_DIRS})

# Source files
set(SOURCES
        src/main.cpp
        src/task_manager.cpp
        src/proc_parser.cpp
        src/systemd_manager.cpp
        src/ipc_server.cpp
)

set(HEADERS
        src/task_manager.h
        src/proc_parser.h
        src/systemd_manager.h
        src/ipc_server.h
        src/debug.h
)

# Create executable
add_executable(taskmgr ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(taskmgr
        ${GTK3_LIBRARIES}
        pthread
)

# Compiler flags
target_compile_options(taskmgr PRIVATE
        -Wall -Wextra -O3
)

# Install target
install(TARGETS taskmgr DESTINATION bin)