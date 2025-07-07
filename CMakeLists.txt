cmake_minimum_required(VERSION 3.12)
project(PunctuationProcessor LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3")

# Include directories
include_directories(include)

# Source files
file(GLOB SRCS src/*.cpp)

# Add source files to the executable
add_executable(punp ${SRCS})

# Install rules
set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local/")
install(TARGETS punp DESTINATION bin)
install(FILES .prules DESTINATION share/punp)
