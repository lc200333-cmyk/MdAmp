cmake_minimum_required(VERSION 3.24)
project(MdAmp VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "MdAmp currently targets Windows only.")
endif()

add_library(mdamp_options INTERFACE)
target_compile_features(mdamp_options INTERFACE cxx_std_17)
target_compile_definitions(mdamp_options INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(mdamp_options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(mdamp_options INTERFACE /W4 /WX /utf-8 /permissive-)
endif()

add_executable(MdAmp WIN32 src/main.cpp src/MdAmp.manifest)
target_link_libraries(MdAmp PRIVATE mdamp_options user32 gdi32 shell32)
set_target_properties(MdAmp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CTest)
if(BUILD_TESTING)
    add_executable(mdamp_unit_tests tests/unit_tests.cpp)
    target_link_libraries(mdamp_unit_tests PRIVATE mdamp_options)
    add_test(NAME mdamp.unit COMMAND mdamp_unit_tests)
    add_test(NAME mdamp.window_smoke COMMAND MdAmp --smoke-test)
    set_tests_properties(mdamp.window_smoke PROPERTIES TIMEOUT 15)
endif()

install(TARGETS MdAmp RUNTIME DESTINATION .)
