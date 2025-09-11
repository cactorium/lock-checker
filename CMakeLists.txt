cmake_minimum_required(VERSION 3.10)

project(lock_checker)

add_library(lock_checker SHARED
    my_plugin.cc
)

execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=plugin
    OUTPUT_VARIABLE plugin_path_unstripped
)
string(STRIP ${plugin_path_unstripped} plugin_path)

target_include_directories(lock_checker PRIVATE
    ${plugin_path}/include
)
target_compile_options(lock_checker PRIVATE
    -Wall
    -fno-rtti
)

add_custom_target(check
    COMMAND ${CMAKE_CXX_COMPILER} -fplugin=$<TARGET_FILE:lock_checker> -c ${CMAKE_SOURCE_DIR}/test.cc -o /dev/null -fdump-tree-gimple
)

find_package(GTest REQUIRED)

add_executable(test_file_checker
    test_file_checker.cc
)
target_link_libraries(test_file_checker
    GTest::gtest
    GTest::gtest_main
)
