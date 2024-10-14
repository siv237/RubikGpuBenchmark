set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

cmake_minimum_required(VERSION 3.10)
project(GPUTest)

set(CMAKE_CXX_STANDARD 11)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

add_executable(GPUTest main.cpp)

target_link_libraries(GPUTest
    OpenGL::GL
    GLEW::GLEW
    glfw
    glm::glm
)
