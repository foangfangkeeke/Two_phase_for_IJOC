cmake_minimum_required(VERSION 3.20)
project(wireless LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4)
endif()

add_executable(wireless
    src/app/main.cpp
    src/config/run_config.cpp
    src/config/solver_config.cpp
    src/domain/wireless_data.cpp
)

target_include_directories(wireless PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(wireless PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out)
