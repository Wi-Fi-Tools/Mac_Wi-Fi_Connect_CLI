cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(wifi_direct VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
set(SOURCES
    src/main.cpp
    src/wifi_manager.cpp
    src/command_handler.cpp
)

# Executable
add_executable(wifi_direct ${SOURCES})

# Include directories
target_include_directories(wifi_direct PRIVATE ${CMAKE_SOURCE_DIR}/include)

# macOS specific: need _NSGetExecutablePath from mach-o/dyld.h
if(APPLE)
    target_compile_definitions(wifi_direct PRIVATE PLATFORM_MACOS)
endif()

# Copy Swift scan script to build output directory
file(COPY ${CMAKE_SOURCE_DIR}/scripts/scan_wifi.swift
     DESTINATION ${CMAKE_BINARY_DIR}/scripts)

# Install targets
install(TARGETS wifi_direct DESTINATION bin)
install(FILES scripts/scan_wifi.swift DESTINATION scripts)
