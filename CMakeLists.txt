cmake_minimum_required(VERSION 3.24)

project(order_cache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORDER_CACHE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(order_cache_enable_warnings target)
    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4 /WX /permissive-)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            "${target}"
            PRIVATE -Wall
                    -Wextra
                    -Wpedantic
                    -Werror
                    -Wcast-align
                    -Wconversion
                    -Wdouble-promotion
                    -Wformat=2
                    -Wnull-dereference
                    -Woverloaded-virtual
                    -Wshadow
                    -Wsign-conversion
        )
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options("${target}" PRIVATE -Wduplicated-branches -Wduplicated-cond -Wlogical-op -Wuseless-cast)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options("${target}" PRIVATE -Wextra-semi -Wimplicit-fallthrough -Wnewline-eof)
    endif()
endfunction()

function(order_cache_enable_sanitizers target)
    if(NOT ORDER_CACHE_ENABLE_SANITIZERS)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "Sanitizers are supported only with Clang and GCC")
    endif()

    target_compile_options("${target}" PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options("${target}" PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endfunction()

add_library(order_cache src/OrderCache.cpp)
add_library(order_cache::order_cache ALIAS order_cache)
target_include_directories(order_cache PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
order_cache_enable_warnings(order_cache)
order_cache_enable_sanitizers(order_cache)

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)

    set(BUILD_GMOCK
        OFF
        CACHE BOOL "" FORCE
    )
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE
    )
    FetchContent_Declare(
        googletest URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip DOWNLOAD_EXTRACT_TIMESTAMP
                       FALSE
    )
    FetchContent_MakeAvailable(googletest)

    # Keep third-party diagnostics from obscuring warnings in project code.
    foreach(gtest_target IN ITEMS gtest gtest_main)
        if(MSVC)
            target_compile_options("${gtest_target}" PRIVATE /w)
        else()
            target_compile_options("${gtest_target}" PRIVATE -w)
        endif()
    endforeach()

    add_executable(order_cache_tests tests/OrderCacheTests.cpp)
    target_link_libraries(order_cache_tests PRIVATE order_cache::order_cache GTest::gtest_main)
    order_cache_enable_warnings(order_cache_tests)
    order_cache_enable_sanitizers(order_cache_tests)

    include(GoogleTest)
    gtest_discover_tests(order_cache_tests)
endif()
