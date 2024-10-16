set(CMAKE_INSTALL_PREFIX "/usr")

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

cmake_minimum_required(VERSION 3.10)
project(RGBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(Freetype REQUIRED)

# Добавьте эту строку после project(RGBench)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Измените строку add_executable на:
add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME}
    OpenGL::GL
    GLEW::GLEW
    glfw
    glm::glm
    ${FREETYPE_LIBRARIES}
)

# Устанавливаем имя исполняемого файла
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "rgbench")

# Добавьте эту строку перед install
file(COPY ${CMAKE_SOURCE_DIR}/include/ico.png DESTINATION ${CMAKE_BINARY_DIR})

# Добавьте эту строку в секцию install
install(FILES ${CMAKE_SOURCE_DIR}/include/ico.png DESTINATION share/rgbench)
