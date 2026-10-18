cmake_minimum_required(VERSION 3.20)
project(wireless LANGUAGES CXX)

set(COPT_HOME "C:/Program Files/copt80" CACHE PATH "COPT install directory")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4)
endif()

add_executable(wireless
    src/algorithm/lifted_ils.cpp
    src/algorithm/lifted_two_phase.cpp
    src/algorithm/natural_ils.cpp
    src/algorithm/route_relaxation.cpp
    src/algorithm/scenario_relaxation.cpp
    src/app/main.cpp
    src/config/run_config.cpp
    src/config/solver_config.cpp
    src/domain/wireless_data.cpp
    src/model/extensive_form.cpp
    src/model/model_helpers.cpp
    src/model/scenario_model.cpp
    src/solver/copt_solver.cpp
)

target_include_directories(wireless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${COPT_HOME}/include
)
target_link_libraries(wireless PRIVATE
    "${COPT_HOME}/lib/copt_cpp.lib"
    "${COPT_HOME}/lib/copt.lib"
    ws2_32.lib
)
set_target_properties(wireless PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out)

add_custom_command(TARGET wireless POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${COPT_HOME}/bin/copt.dll"
        "${COPT_HOME}/bin/copt_cpp.dll"
        "$<TARGET_FILE_DIR:wireless>"
)
