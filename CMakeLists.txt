set(CMAKE_INSTALL_PREFIX "/usr")

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

cmake_minimum_required(VERSION 3.10)
project(RGBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Добавим флаги компилятора для поиска системных заголовочных файлов
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I/usr/include/c++/9 -I/usr/include/x86_64-linux-gnu/c++/9 -Wno-deprecated-declarations")

# Установим предпочтение GLVND для OpenGL
set(OpenGL_GL_PREFERENCE GLVND)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(Freetype REQUIRED)
find_package(OpenSSL REQUIRED)

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
    /usr/include/freetype2
)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    glfw
    ${GLM_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    OpenSSL::Crypto
)

# Устанавливаем имя исполняемого файла
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "rgbench")

# Определяем переменную для пути к иконке
set(ICON_PATH "${CMAKE_SOURCE_DIR}/include/ico.png")

# Проверяем существование файла иконки
if(EXISTS ${ICON_PATH})
    # Копируем иконку в директорию сборки
    configure_file(${ICON_PATH} ${CMAKE_BINARY_DIR}/ico.png COPYONLY)

    # Устанавливаем иконку
    install(FILES ${CMAKE_BINARY_DIR}/ico.png DESTINATION share/rgbench)

    # Добавляем определение пути к иконке для использования в коде
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        ICON_PATH="${CMAKE_BINARY_DIR}/ico.png"
    )
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE ICON_PATH="")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# После определения ICON_PATH
file(COPY ${ICON_PATH} DESTINATION ${CMAKE_BINARY_DIR})
