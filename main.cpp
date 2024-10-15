#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <iomanip> // Для форматирования вывода
#include <sstream>
#include <vector>
#include <utility>
#include <cmath>
#include <map>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <chrono>
#include <ctime>
#include <algorithm> // Добавьте эту строку в начало файла

// Добавьте эти определения в начало файла, после включения библиотек
// const int FPS_HISTORY_SIZE = 200;
// std::vector<float> fpsHistory(FPS_HISTORY_SIZE, 0.0f);
// std::vector<float> avgFpsHistory(FPS_HISTORY_SIZE, 0.0f);

// Добавьте эту глобальную переменную
int currentGraphIndex = 0;

// Добавьте эти глобальные переменные
float minFps = std::numeric_limits<float>::max();
float maxFps = 0.0f;

// Объявите shaderProgram глобально
unsigned int shaderProgram;

// Добавьте эту глобальную переменную в начало файла
unsigned int lineVAO, lineVBO;
unsigned int lineShaderProgram;

struct Character {
    unsigned int TextureID;
    glm::ivec2   Size;
    glm::ivec2   Bearing;
    unsigned int Advance;
};

std::map<char, Character> Characters;
unsigned int textVAO, textVBO;
unsigned int textShaderProgram;

const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aColor;
    out vec3 ourColor;
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    void main()
    {
        gl_Position = projection * view * model * vec4(aPos, 1.0);
        ourColor = aColor;
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    in vec3 ourColor;
    out vec4 FragColor;
    void main()
    {
        FragColor = vec4(ourColor, 1.0);
    }
)";

const char* textVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>
    out vec2 TexCoords;
    uniform mat4 projection;
    void main()
    {
        gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
        TexCoords = vertex.zw;
    }
)";

const char* textFragmentShaderSource = R"(
    #version 330 core
    in vec2 TexCoords;
    out vec4 color;
    uniform sampler2D text;
    uniform vec3 textColor;
    void main()
    {    
        vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
        color = vec4(textColor, 1.0) * sampled;
    }
)";

// Добавьте эти шейдеры для линий
const char* lineVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    uniform mat4 projection;
    void main()
    {
        gl_Position = projection * vec4(aPos.x, aPos.y, 0.0, 1.0);
    }
)";

const char* lineFragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec3 color;
    void main()
    {
        FragColor = vec4(color, 1.0);
    }
)";

// Функция для фильтра Калмана
double kalmanFilter(double measurement, double& estimate, double& errorEstimate, double processNoise, double measurementNoise) {
    // Предсказание
    double predictedEstimate = estimate;
    double predictedErrorEstimate = errorEstimate + processNoise;

    // Обновление
    double kalmanGain = predictedErrorEstimate / (predictedErrorEstimate + measurementNoise);
    estimate = predictedEstimate + kalmanGain * (measurement - predictedEstimate);
    errorEstimate = (1 - kalmanGain) * predictedErrorEstimate;

    return estimate;
}

void loadFont()
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        std::cout << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 0, &face))
    {
        std::cout << "ERROR::FREETYPE: Failed to load font" << std::endl;
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, 48); // Увеличено с 24 до 48

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (unsigned char c = 0; c < 128; c++)
    {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            std::cout << "ERROR::FREETYTPE: Failed to load Glyph" << std::endl;
            continue;
        }

        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)
        };
        Characters.insert(std::pair<char, Character>(c, character));

        // Удалите или закомментируйте эту строку
        // std::cout << "Loaded character " << c << " with size " << face->glyph->bitmap.width << "x" << face->glyph->bitmap.rows << std::endl;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

void renderText(const std::string& text, float x, float y, float scale, glm::vec3 color)
{
    glUseProgram(textShaderProgram);
    glUniform3f(glGetUniformLocation(textShaderProgram, "textColor"), color.x, color.y, color.z);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(textVAO);

    for (char c : text)
    {
        Character ch = Characters[c];
        
        float xpos = x + ch.Bearing.x * scale;
        float ypos = y - (ch.Size.y - ch.Bearing.y) * scale;

        float w = ch.Size.x * scale;
        float h = ch.Size.y * scale;

        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.Advance >> 6) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void checkShaderCompileErrors(unsigned int shader, std::string type)
{
    int success;
    char infoLog[1024];
    if (type != "PROGRAM")
    {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
    else
    {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
}

// Добавьте эту функцию перед main():
void checkOpenGLError(const char* stmt, const char* fname, int line)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        printf("OpenGL error %08x, at %s:%i - for %s\n", err, fname, line, stmt);
        abort();
    }
}

// Используйте этот макрос после каждой важной операции OpenGL
#define GL_CHECK(stmt) do { \
        stmt; \
        checkOpenGLError(#stmt, __FILE__, __LINE__); \
    } while (0)

// Добавьте эту функцию перед функцией main()
std::string getGPUName() {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    if (renderer) {
        return std::string(reinterpret_cast<const char*>(renderer));
    }
    return "Неизвестная видеокарта";
}

// Добавьте ту функцию перед main():
float getTextWidth(const std::string& text, float scale) {
    float width = 0.0f;
    for (char c : text) {
        Character ch = Characters[c];
        width += (ch.Advance >> 6) * scale;
    }
    return width;
}

// Измените функцию drawLine следующим образом:
void drawLine(float x1, float y1, float x2, float y2, glm::vec3 color, unsigned int program) {
    glUseProgram(program);
    
    float vertices[] = {
        x1, y1, 0.0f,
        x2, y2, 0.0f
    };
    
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 0.0f, 800.0f);
    glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(program, "view"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    glUniformMatrix4fv(glGetUniformLocation(program, "model"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    
    glUniform3fv(glGetUniformLocation(program, "ourColor"), 1, glm::value_ptr(color));
    
    glDrawArrays(GL_LINES, 0, 2);
}

// В начале файла обновите константы для графика
const int GRAPH_WIDTH = 550;
const int GRAPH_HEIGHT = 100;
const int GRAPH_BOTTOM = 50;  // Увеличим отступ снизу
const int GRAPH_LEFT = 50;    // Добавим отступ слева
int currentGraphX = 0;
std::vector<float> fpsHistory(GRAPH_WIDTH, -1.0f);
std::vector<float> avgFpsHistory(GRAPH_WIDTH, -1.0f);
float graphMin = 0.0f;
float graphMax = 5000.0f; // Начальное максимальное значение

// Добавьте эту переменную в начало файла
double lastGraphUpdateTime = 0.0;

// Добавьте эту переменную в начало файла, где объявлены другие глобальные переменные
bool isFirstValidMeasurement = true;

int main()
{
    // Инициализация GLFW
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Настройка GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // Добавьте эту строку, чтобы запретить изменение размера ока
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Создание окна
    GLFWwindow* window = glfwCreateWindow(800, 800, "Rubik GPU Benchmark", NULL, NULL);
    if (window == NULL)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Инициизация GLEW
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // Отключаем VSync
    glfwSwapInterval(0);

    // Добавляем переменные для подсчета FPS и фильтра Калмна
    double lastTime = glfwGetTime();
    int nbFrames = 0;
    double fpsEstimate = 0.0;
    double fpsErrorEstimate = 1000.0;
    const double processNoise = 0.000001;
    const double measurementNoise = 36.0;
    bool isFirstMeasurement = true;

    // Компиляция шейдеров
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    checkShaderCompileErrors(vertexShader, "VERTEX");

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    checkShaderCompileErrors(fragmentShader, "FRAGMENT");

    // После компиляции шейдеров
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    checkShaderCompileErrors(shaderProgram, "PROGRAM");

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    loadFont();

    unsigned int textVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(textVertexShader, 1, &textVertexShaderSource, NULL);
    glCompileShader(textVertexShader);
    checkShaderCompileErrors(textVertexShader, "TEXT_VERTEX");

    unsigned int textFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(textFragmentShader, 1, &textFragmentShaderSource, NULL);
    glCompileShader(textFragmentShader);
    checkShaderCompileErrors(textFragmentShader, "TEXT_FRAGMENT");

    textShaderProgram = glCreateProgram();
    glAttachShader(textShaderProgram, textVertexShader);
    glAttachShader(textShaderProgram, textFragmentShader);
    glLinkProgram(textShaderProgram);
    checkShaderCompileErrors(textShaderProgram, "TEXT_PROGRAM");

    glDeleteShader(textVertexShader);
    glDeleteShader(textFragmentShader);

    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Добавьте эту строку в функцию main() после инициализации OpenGL
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // В функции main(), после инициализации OpenGL, добавьте:
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Компиляция шейдеро для линий
    unsigned int lineVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(lineVertexShader, 1, &lineVertexShaderSource, NULL);
    glCompileShader(lineVertexShader);
    checkShaderCompileErrors(lineVertexShader, "LINE_VERTEX");

    unsigned int lineFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(lineFragmentShader, 1, &lineFragmentShaderSource, NULL);
    glCompileShader(lineFragmentShader);
    checkShaderCompileErrors(lineFragmentShader, "LINE_FRAGMENT");

    lineShaderProgram = glCreateProgram();
    glAttachShader(lineShaderProgram, lineVertexShader);
    glAttachShader(lineShaderProgram, lineFragmentShader);
    glLinkProgram(lineShaderProgram);
    checkShaderCompileErrors(lineShaderProgram, "LINE_PROGRAM");

    glDeleteShader(lineVertexShader);
    glDeleteShader(lineFragmentShader);

    // Создание VAO и VBO для линий
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * GRAPH_WIDTH * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Вершины куба с цветами
    float vertices[] = {
        // позиции          // цвета
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,

        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,

         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 1.0f,

        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 1.0f
    };

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);

    std::string fpsText = "FPS: 0";
    std::string avgFpsText = "Avg: 0";

    // Дбавьте эту переменную перед циклом рендеринга
    double fps = 0.0;

    // В функции main() после инициализации OpenGL добавьте:
    std::string gpuName = getGPUName();

    // Добавьте эти переменные в начало функции main(), после инициализации GLFW и OpenGL:
    float cameraDistance = 5.0f;
    float minDistance = 3.0f;
    float maxDistance = 7.0f;
    float zoomSpeed = 0.5f;

    // Добавьте эту строку в начало функции main(), после инициализации GLFW
    auto startTime = std::chrono::steady_clock::now();

    // Главный цикл рендеринга
    while (!glfwWindowShouldClose(window))
    {
        // Измеряем FPS
        double currentTime = glfwGetTime();
        nbFrames++;
        if (currentTime - lastTime >= 1.0) { // Если прошла 1 секунда
            fps = static_cast<double>(nbFrames) / (currentTime - lastTime);
            
            if (fps > 0) { // Проверяем, что FPS больше 0
                if (isFirstValidMeasurement) {
                    fpsEstimate = fps;
                    isFirstValidMeasurement = false;
                } else {
                    // Применяем фильтр Калмана
                    fpsEstimate = kalmanFilter(fps, fpsEstimate, fpsErrorEstimate, processNoise, measurementNoise);
                }

                // Обновляем данные графика
                fpsHistory[currentGraphX] = fps;
                avgFpsHistory[currentGraphX] = fpsEstimate;
                currentGraphX = (currentGraphX + 1) % GRAPH_WIDTH;

                // Находим минимальное и максимальное значения FPS в истории
                minFps = std::numeric_limits<float>::max();
                maxFps = std::numeric_limits<float>::min();
                for (float f : fpsHistory) {
                    if (f > 0) {
                        minFps = std::min(minFps, f);
                        maxFps = std::max(maxFps, f);
                    }
                }
                for (float avg : avgFpsHistory) {
                    if (avg > 0) {
                        minFps = std::min(minFps, avg);
                        maxFps = std::max(maxFps, avg);
                    }
                }

                // Устанавливаем диапазон графика только если у нас есть действительные данные
                if (minFps != std::numeric_limits<float>::max() && maxFps != std::numeric_limits<float>::min()) {
                    float fpsRange = maxFps - minFps;
                    graphMin = std::max(0.0f, minFps - fpsRange * 0.1f); // 10% запас снизу, но не меньше 0
                    graphMax = maxFps + fpsRange * 0.1f; // 10% запас сверху
                }

                // Форматируем строку с текущим и сглаженным FPS
                std::stringstream ss;
                ss << "Rubik GPU Benchmark - FPS: " << std::fixed << std::setprecision(2) << fps
                   << " Avg FPS: " << std::fixed << std::setprecision(2) << fpsEstimate;
                glfwSetWindowTitle(window, ss.str().c_str());

                // Рассчитываем время от старта программы
                auto currentTimePoint = std::chrono::steady_clock::now();
                auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTimePoint - startTime).count();

                // Изменяем формат вывода в консоль
                std::cout << "Time: " << elapsedSeconds << "; FPS: " << std::fixed << std::setprecision(2) << fps
                          << "; AVG: " << std::fixed << std::setprecision(2) << fpsEstimate << std::endl;
            }

            nbFrames = 0;
            lastTime = currentTime;
        }

        // Обновляем текст FPS каждый кадр
        std::stringstream fpsStream, avgFpsStream;
        fpsStream << std::fixed << std::setprecision(2) << fps;
        avgFpsStream << std::fixed << std::setprecision(2) << fpsEstimate;
        fpsText = "FPS: " + fpsStream.str();
        avgFpsText = "Avg: " + avgFpsStream.str();

        // Очистка буфера цвета и глубины
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Активация шейдерной программы
        glUseProgram(shaderProgram);

        // Обновление расстояния камеры
        cameraDistance = 5.0f + 2.0f * sin(glfwGetTime() * zoomSpeed);
        cameraDistance = glm::clamp(cameraDistance, minDistance, maxDistance);

        // Создае матриц преобразования
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);

        // Настройка матриц вида и проекции
        view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, cameraDistance),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        projection = glm::perspective(glm::radians(45.0f), 800.0f / 800.0f, 0.1f, 100.0f);

        // Вращение вего кубика Рубика
        glm::mat4 rubiksCubeRotation = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));

        // Передача матриц ида и проекции в шейдер
        unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
        unsigned int projectionLoc = glGetUniformLocation(shaderProgram, "projection");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        // Определение размеров и зазоров
        float cubeSize = 0.3f;
        float gap = 0.01f;
        float totalSize = cubeSize + gap;

        // Отрисовка кубиков
        glBindVertexArray(VAO);
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                for (int z = -1; z <= 1; z++) {
                    glm::mat4 model = glm::mat4(1.0f);
                    model = rubiksCubeRotation * model; // Применяем вращение ко всему кубику Рубика
                    model = glm::translate(model, glm::vec3(x * totalSize, y * totalSize, z * totalSize));
                    model = glm::scale(model, glm::vec3(cubeSize, cubeSize, cubeSize));

                    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
                    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }
            }
        }

        // В главном цикле рендеринга замените код отрисовки графика и текста на следующий:

        // Отрисовка графика
        glDisable(GL_DEPTH_TEST);
        glUseProgram(lineShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(glm::ortho(0.0f, 800.0f, 0.0f, 800.0f)));

        glBindVertexArray(lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);

        glPointSize(2.0f); // Увеличиваем размер точек для лучшей видимости

        // Рисуем рамку графика
        glUniform3f(glGetUniformLocation(lineShaderProgram, "color"), 1.0f, 1.0f, 1.0f); // Белый цвет
        float frameVertices[] = {
            GRAPH_LEFT, GRAPH_BOTTOM, GRAPH_LEFT + GRAPH_WIDTH, GRAPH_BOTTOM,
            GRAPH_LEFT + GRAPH_WIDTH, GRAPH_BOTTOM, GRAPH_LEFT + GRAPH_WIDTH, GRAPH_BOTTOM + GRAPH_HEIGHT,
            GRAPH_LEFT + GRAPH_WIDTH, GRAPH_BOTTOM + GRAPH_HEIGHT, GRAPH_LEFT, GRAPH_BOTTOM + GRAPH_HEIGHT,
            GRAPH_LEFT, GRAPH_BOTTOM + GRAPH_HEIGHT, GRAPH_LEFT, GRAPH_BOTTOM
        };
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(frameVertices), frameVertices);
        glDrawArrays(GL_LINES, 0, 8);

        // Рисуем текущий FPS (красные точки)
        glUniform3f(glGetUniformLocation(lineShaderProgram, "color"), 1.0f, 0.0f, 0.0f); // Красный цвет
        std::vector<float> pointVertices;
        for (int i = 0; i < GRAPH_WIDTH; i++) {
            int index = (currentGraphX - GRAPH_WIDTH + i + GRAPH_WIDTH) % GRAPH_WIDTH;
            if (fpsHistory[index] >= 0) {
                float x = GRAPH_LEFT + static_cast<float>(i);
                float y = GRAPH_BOTTOM + ((fpsHistory[index] - graphMin) / (graphMax - graphMin)) * GRAPH_HEIGHT;
                pointVertices.push_back(x);
                pointVertices.push_back(y);
            }
        }
        if (!pointVertices.empty()) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, pointVertices.size() * sizeof(float), pointVertices.data());
            glDrawArrays(GL_POINTS, 0, pointVertices.size() / 2);
        }

        // Рисуем средний FPS (зеленые точки)
        glUniform3f(glGetUniformLocation(lineShaderProgram, "color"), 0.0f, 1.0f, 0.0f); // Зеленый цвет
        pointVertices.clear();
        for (int i = 0; i < GRAPH_WIDTH; i++) {
            int index = (currentGraphX - GRAPH_WIDTH + i + GRAPH_WIDTH) % GRAPH_WIDTH;
            if (avgFpsHistory[index] >= 0) {
                float x = GRAPH_LEFT + static_cast<float>(i);
                float y = GRAPH_BOTTOM + ((avgFpsHistory[index] - graphMin) / (graphMax - graphMin)) * GRAPH_HEIGHT;
                pointVertices.push_back(x);
                pointVertices.push_back(y);
            }
        }
        if (!pointVertices.empty()) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, pointVertices.size() * sizeof(float), pointVertices.data());
            glDrawArrays(GL_POINTS, 0, pointVertices.size() / 2);
        }

        glBindVertexArray(0);

        // Рендеринг текста
        glUseProgram(textShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(textShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(glm::ortho(0.0f, 800.0f, 0.0f, 800.0f)));

        // В функции main(), замените код рендеринга FPS и AVG FPS на следующий:

        // Рендеринг FPS и AVG FPS в левом верхнем углу над графиком
        float fpsScale = 0.5f;
        float labelWidth = 60.0f; // Увеличим ширину для меток
        float valueWidth = 100.0f; // Фиксированная ширина для значений

        std::string fpsLabel = "FPS:";
        std::string avgLabel = "Avg:";

        renderText(fpsLabel, GRAPH_LEFT, GRAPH_BOTTOM + GRAPH_HEIGHT + 30, fpsScale, glm::vec3(1.0f, 1.0f, 1.0f)); // Белый цвет
        renderText(avgLabel, GRAPH_LEFT, GRAPH_BOTTOM + GRAPH_HEIGHT + 10, fpsScale, glm::vec3(1.0f, 1.0f, 1.0f)); // Белый цвет

        // Обновляем значения FPS и AVG FPS
        fpsStream.str("");
        avgFpsStream.str("");
        fpsStream << std::fixed << std::setprecision(2) << fps;
        avgFpsStream << std::fixed << std::setprecision(2) << fpsEstimate;

        renderText(fpsStream.str(), GRAPH_LEFT + labelWidth, GRAPH_BOTTOM + GRAPH_HEIGHT + 30, fpsScale, glm::vec3(1.0f, 0.0f, 0.0f)); // Красный цвет
        renderText(avgFpsStream.str(), GRAPH_LEFT + labelWidth, GRAPH_BOTTOM + GRAPH_HEIGHT + 10, fpsScale, glm::vec3(0.0f, 1.0f, 0.0f)); // Зеленый цвет

        // Добавляем подписи к графику
        std::string maxFpsLabel = "Max: " + std::to_string(static_cast<int>(graphMax));
        std::string minFpsLabel = "Min: " + std::to_string(static_cast<int>(graphMin));
        renderText(maxFpsLabel, GRAPH_LEFT + GRAPH_WIDTH + 5, GRAPH_BOTTOM + GRAPH_HEIGHT - 20, 0.4f, glm::vec3(1.0f, 1.0f, 1.0f));
        renderText(minFpsLabel, GRAPH_LEFT + GRAPH_WIDTH + 5, GRAPH_BOTTOM, 0.4f, glm::vec3(1.0f, 1.0f, 1.0f));

        // Рендеринг имени GPU вверху по центру
        float gpuScale = 0.5f;
        float maxWidth = 780.0f; // Оставляем небольшой отступ по краям
        while (getTextWidth(gpuName, gpuScale) > maxWidth) {
            gpuScale *= 0.9f;
        }
        float gpuNameWidth = getTextWidth(gpuName, gpuScale);
        float gpuNameX = (800.0f - gpuNameWidth) / 2.0f;
        renderText(gpuName, gpuNameX, 770, gpuScale, glm::vec3(1.0f, 1.0f, 0.0f)); // Желтый цвет

        glEnable(GL_DEPTH_TEST);

        // Обмен буферов и обрабтка событий GLFW
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Выводим сглаженное значение FPS в консоль перед завершением программы
    // Удлите или закомментируйте эту строку
    // std::cout << "Smoothed Average FPS: " << std::fixed << std::setprecision(2) << fpsEstimate << std::endl;

    // Очистка ресурсов
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}