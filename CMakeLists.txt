cmake_minimum_required(VERSION 3.10)

project(
        libhbc
        VERSION 0.1.0
        DESCRIPTION "Fast C++ parser and IR for Hermes Bytecode"
        LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# C++ standard
# We require C++17 for std::optional, std::string_view, and structured bindings.
# REQUIRED means CMake will error out rather than silently fall back to an older
# standard if the compiler doesn't support C++17.
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Use -std=c++17, not -std=gnu++17

# -----------------------------------------------------------------------------
# Compiler warnings
# These are applied only to our own targets, not to dependencies.
# -----------------------------------------------------------------------------
if(MSVC)
    set(HBC_WARNING_FLAGS /W4 /WX)
else()
    set(HBC_WARNING_FLAGS
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter   # common during early development
    )
endif()

# -----------------------------------------------------------------------------
# The library target
#
# STATIC means we produce a .a (Linux/macOS) or .lib (Windows) archive.
# This is the right choice for an embedded library — the Python shell will
# link against it when building the pybind11 extension.
#
# We list source files explicitly rather than using GLOB so CMake detects
# new files only when you re-run cmake, not silently at build time.
# -----------------------------------------------------------------------------
add_library(hbc STATIC
        src/.placeholder.cpp

        # src/ files will be added here as we create them, e.g.:
        # src/loader.cpp
        # src/parser.cpp
        # src/indexer.cpp
        # src/query.cpp
        # src/serializer.cpp
        # src/version_registry.cpp
)

# Tell the compiler where to find our headers.
# PUBLIC means: both libhbc itself AND any target that links against libhbc
# will have this directory on their include path.
# So consumers can write:  #include "hbc_types.h"  without any path prefix.
target_include_directories(hbc
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Apply warning flags only to our library, not to anything that links against us
target_compile_options(hbc PRIVATE ${HBC_WARNING_FLAGS})