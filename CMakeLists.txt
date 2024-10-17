set(CMAKE_INSTALL_PREFIX "/usr")

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

cmake_minimum_required(VERSION 3.10)
project(RGBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Добавим флаги компилятора для поиска системных заголовочных файлов
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I/usr/include/c++/9 -I/usr/include/x86_64-linux-gnu/c++/9")

# Установим предпочтение GLVND для OpenGL
set(OpenGL_GL_PREFERENCE GLVND)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(Freetype REQUIRED)

# Добавим пути для поиска заголовочных файлов
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
    /usr/include/freetype2  # Добавим путь к заголовочным файлам FreeType
)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    glfw
    ${GLM_LIBRARIES}
    ${FREETYPE_LIBRARIES}
)

# Устанавливаем имя исполняемого файла
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "rgbench")

# Копируем иконку в директорию сборки
file(COPY ${CMAKE_SOURCE_DIR}/include/ico.png DESTINATION ${CMAKE_BINARY_DIR})

# Устанавливаем иконку
install(FILES ${CMAKE_SOURCE_DIR}/include/ico.png DESTINATION share/rgbench)
