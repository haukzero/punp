cmake_minimum_required(VERSION 3.20)
project(punp VERSION 3.0.6 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
    @ONLY
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/algorithm/ac_automaton.cpp
    src/base/thread_pool/thread_pool.cpp
    src/config/argument_parser.cpp
    src/config/config_manager.cpp
    src/config/parser/lexer.cpp
    src/config/parser/parser.cpp
    src/core/file_finder.cpp
    src/core/file_processor.cpp
    src/updater/updater.cpp
)

target_include_directories(${PROJECT_NAME} 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Compiler options
target_compile_options(${PROJECT_NAME} 
    PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:
            -O3
            -march=native
            -mtune=native
            -funroll-loops
            -ffunction-sections
            -fdata-sections
        >
        $<$<CONFIG:Debug>:-g -O0>
)

# Link options
target_link_options(${PROJECT_NAME}
    PRIVATE
        $<$<CONFIG:Release>:
            -Wl,--gc-sections
            -Wl,-O3
        >
)

# If using Clang, can use `-DLUSE_LLD=ON` to enable lld linker and ICF
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    option(USE_LLD "Use lld linker for better performance" OFF)
    if(USE_LLD)
        target_link_options(${PROJECT_NAME} PRIVATE -fuse-ld=lld)
        target_link_options(${PROJECT_NAME} PRIVATE 
            $<$<CONFIG:Release>:-Wl,--icf=all>
        )
    endif()
endif()

# LTO for Release builds
set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local/" CACHE PATH "Install prefix" FORCE)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES .prules DESTINATION share/${PROJECT_NAME})
