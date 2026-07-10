cmake_minimum_required(VERSION 3.10)
project(wireless)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GUROBI_DIR "C:/gurobi1203/win64" CACHE PATH "Gurobi install directory")

if(NOT EXISTS ${GUROBI_DIR})
    message(FATAL_ERROR "Gurobi directory does not exist: ${GUROBI_DIR}")
endif()

if(MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_VS_GLOBALS "VSLANG=1033")
endif()

set(GUROBI_INCLUDE_DIR "${GUROBI_DIR}/include")
set(GUROBI_LIB_DIR "${GUROBI_DIR}/lib")
set(GUROBI_CXX_LIB_RELEASE "${GUROBI_LIB_DIR}/gurobi_c++md2017.lib")
set(GUROBI_CXX_LIB_DEBUG "${GUROBI_LIB_DIR}/gurobi_c++mdd2017.lib")
set(GUROBI_CORE_LIB "${GUROBI_LIB_DIR}/gurobi120.lib")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/problem/wireless
    ${CMAKE_CURRENT_SOURCE_DIR}/src/problem/wireless_reform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/algorithm
    ${GUROBI_INCLUDE_DIR}
)

add_executable(wireless
    src/main.cpp
    src/utils/solver_config.cpp
    src/problem/wireless/wireless.cpp
    src/problem/wireless_reform/wireless_reform.cpp
    src/problem/wireless_reform/wireless_reform_route.cpp
    src/algorithm/using_solver.cpp
    src/algorithm/benders_core.cpp
    src/algorithm/barp_s_integer_l_shaped.cpp
    src/algorithm/lifted_ils.cpp
    src/algorithm/lifted_two_phase_ils.cpp
    src/algorithm/lshape_relaxed.cpp
    src/algorithm/lshape_relaxed_station.cpp
)

set_target_properties(wireless PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out
)

target_link_libraries(wireless PRIVATE
    $<$<CONFIG:Debug>:${GUROBI_CXX_LIB_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${GUROBI_CXX_LIB_RELEASE}>
    ${GUROBI_CORE_LIB}
    ws2_32.lib
)
