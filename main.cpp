#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <limits>
#include <cmath>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <filesystem>
#include <fstream>
#include <string_view>
#include <openssl/md5.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int currentGraphIndex = 0;
float minFps = std::numeric_limits<float>::max();
float maxFps = 0.0f;
unsigned int shaderProgram;
unsigned int lineVAO, lineVBO;
unsigned int lineShaderProgram;

// Заменяем объявление programVersion
#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION "unknown"
#endif

// Убираем const, чтобы можно было изменять значение
std::string programVersion = PROGRAM_VERSION;

struct Character {
    unsigned int TextureID;
    glm::ivec2   Size;
    glm::ivec2   Bearing;
    unsigned int Advance;
};

std::map<char, Character> Characters;
unsigned int textVAO, textVBO;
unsigned int textShaderProgram;

std::string_view vertexShaderSource = R"(
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

std::string_view fragmentShaderSource = R"(
    #version 330 core
    in vec3 ourColor;
    out vec4 FragColor;
    void main()
    {
        FragColor = vec4(ourColor, 1.0);
    }
)";

std::string_view textVertexShaderSource = R"(
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

std::string_view textFragmentShaderSource = R"(
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

// шейдеры для линий
std::string_view lineVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    uniform mat4 projection;
    void main()
    {
        gl_Position = projection * vec4(aPos.x, aPos.y, 0.0, 1.0);
    }
)";

std::string_view lineFragmentShaderSource = R"(
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


void checkOpenGLError(const char* stmt, const char* fname, int line)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        printf("OpenGL error %08x, at %s:%i - for %s\n", err, fname, line, stmt);
        abort();
    }
}

// Использу эо  после каждой важной перации OpenGL
#define GL_CHECK(stmt) do { \
        stmt; \
        checkOpenGLError(#stmt, __FILE__, __LINE__); \
    } while (0)

[[nodiscard]] std::string getGPUName() {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    if (renderer) {
        return std::string(reinterpret_cast<const char*>(renderer));
    }
    return "Неизвестная видеоката";
}

float getTextWidth(const std::string& text, float scale) {
    float width = 0.0f;
    for (char c : text) {
        Character ch = Characters[c];
        width += (ch.Advance >> 6) * scale;
    }
    return width;
}

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

// константы для рафика
const int GRAPH_WIDTH = 550;
const int GRAPH_HEIGHT = 100;
const int GRAPH_BOTTOM = 50;  // Увеличим отступ снизу
const int GRAPH_LEFT = 50;    // Добавим отступ слева
int currentGraphX = 0;
std::vector<float> fpsHistory(GRAPH_WIDTH, -1.0f);
std::vector<float> avgFpsHistory(GRAPH_WIDTH, -1.0f);
float graphMin = 0.0f;
float graphMax = 5000.0f; // Начальное максимальное значение

double lastGraphUpdateTime = 0.0;
bool isFirstValidMeasurement = true;

std::string getMonitorInfo(GLFWwindow* window) {
    GLFWmonitor* monitor = glfwGetWindowMonitor(window);
    if (!monitor) {
        monitor = glfwGetPrimaryMonitor();
    }
    
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int width = mode->width;
    int height = mode->height;
    int refreshRate = mode->refreshRate;
    
    const char* monitorName = glfwGetMonitorName(monitor);
    
    std::stringstream ss;
    ss << monitorName << " - " << width << "x" << height << " @ " << refreshRate << "Hz";
    return ss.str();
}

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

[[nodiscard]] std::string getVRAMInfo() {
    std::string result;
    try {
        result = exec("glxinfo | grep 'Video memory'");
        
        // Удаляем лишние пробелы и переносы строк
        result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
        result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
        
        // Извлекаем только объем памяти
        size_t colonPos = result.find(':');
        if (colonPos != std::string::npos) {
            result = result.substr(colonPos + 1);
            // Удаляем начальные пробелы
            result.erase(0, result.find_first_not_of(' '));
        }
        
        // Если результат пустой, возвращаем "Unknown"
        if (result.empty()) {
            result = "Unknown";
        }
    } catch (std::exception& e) {
        result = "Unknown";
    }
    
    return "VRAM: " + result;
}

std::string getCPUInfo() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::string cpu_model;
    while (std::getline(cpuinfo, line)) {
        if (line.substr(0, 10) == "model name") {
            cpu_model = line.substr(line.find(":") + 2);
            break;
        }
    }
    return "CPU: " + (cpu_model.empty() ? "Unknown" : cpu_model);
}

std::string getRAMInfo() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    long total_ram = 0;
    while (std::getline(meminfo, line)) {
        if (line.substr(0, 8) == "MemTotal") {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            total_ram = std::stol(value);
            break;
        }
    }
    double ram_gb = total_ram / (1024.0 * 1024.0);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << ram_gb << " GB";
    return "RAM: " + ss.str();
}

GLFWimage createTransparentIcon(const char* filename, int targetSize) {
    GLFWimage icon = {};
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);

    if (!data) {
        return icon;
    }

    // Создаем новое изображение с заданным размером и прозрачным фоном
    icon.width = targetSize;
    icon.height = targetSize;
    std::unique_ptr<unsigned char[]> pixels(new unsigned char[targetSize * targetSize * 4]);
    memset(pixels.get(), 0, targetSize * targetSize * 4);

    // Вычисляем коэффициент масштабирования
    float scale = std::min((float)targetSize / width, (float)targetSize / height);
    int newWidth = static_cast<int>(width * scale);
    int newHeight = static_cast<int>(height * scale);

    // Вычисляем отступы для центрирования
    int offsetX = (targetSize - newWidth) / 2;
    int offsetY = (targetSize - newHeight) / 2;

    // Копируем и масштабируем изображение
    for (int y = 0; y < newHeight; ++y) {
        for (int x = 0; x < newWidth; ++x) {
            int srcX = static_cast<int>(x / scale);
            int srcY = static_cast<int>(y / scale);
            int srcIndex = (srcY * width + srcX) * 4;
            int dstIndex = ((y + offsetY) * targetSize + (x + offsetX)) * 4;

            for (int c = 0; c < 4; ++c) {
                pixels[dstIndex + c] = data[srcIndex + c];
            }
        }
    }

    stbi_image_free(data);
    icon.pixels = pixels.release();
    return icon;
}

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 800;
const float TEXT_SCALE = 0.4f;
const float LINE_SPACING = 25.0f;
constexpr int FPS_HISTORY_SIZE = 200;
constexpr float MIN_DISTANCE = 3.0f;
constexpr float MAX_DISTANCE = 7.0f;
constexpr float ZOOM_SPEED = 0.5f;

std::string calculateMD5(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return "Unknown";
    }

    MD5_CTX md5Context;
    MD5_Init(&md5Context);

    char buf[1024 * 16];
    while (file.good()) {
        file.read(buf, sizeof(buf));
        MD5_Update(&md5Context, buf, file.gcount());
    }

    unsigned char result[MD5_DIGEST_LENGTH];
    MD5_Final(result, &md5Context);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<unsigned>(result[i]);
    }

    return ss.str().substr(0, 8); // Возвращаем первые 8 символов хеша
}

int main()
{
    std::vector<std::string> iconPaths = {
        "include/ico.png",
        "../include/ico.png",
        "ico.png",
        "../ico.png"
    };

    std::string iconPath;
    for (const auto& path : iconPaths) {
        if (std::filesystem::exists(path)) {
            iconPath = path;
            break;
        }
    }

    if (iconPath.empty()) {
        std::cerr << "Предупреждение: файл иконки не найден. Программа продолжит работу бе иконки." << std::endl;
    }

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
    
    // запретить изменение размера ока
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Создание окна
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Rubik GPU Benchmark", nullptr, nullptr);
    if (window == nullptr)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Добавьте эту проверку
    const char* error_description;
    int error_code = glfwGetError(&error_description);
    if (error_code != GLFW_NO_ERROR) {
        std::cerr << "GLFW Error (" << error_code << "): " << error_description << std::endl;
    }

    // Инициизация GLEW
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // Отключаем VSync
    glfwSwapInterval(0);

    // Добавляем переменные для подсчета FPS и фильтра Калмна
    auto lastTime = std::chrono::steady_clock::now();
    int nbFrames = 0;
    double fpsEstimate = 0.0;
    double fpsErrorEstimate = 1000.0;
    const double processNoise = 0.000001;
    const double measurementNoise = 36.0;
    bool isFirstMeasurement = true;

    // Компиляция шейдеров
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vertexShaderSourcePtr = vertexShaderSource.data();
    glShaderSource(vertexShader, 1, &vertexShaderSourcePtr, NULL);
    glCompileShader(vertexShader);
    checkShaderCompileErrors(vertexShader, "VERTEX");

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentShaderSourcePtr = fragmentShaderSource.data();
    glShaderSource(fragmentShader, 1, &fragmentShaderSourcePtr, NULL);
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
    const char* textVertexShaderSourcePtr = textVertexShaderSource.data();
    glShaderSource(textVertexShader, 1, &textVertexShaderSourcePtr, NULL);
    glCompileShader(textVertexShader);
    checkShaderCompileErrors(textVertexShader, "TEXT_VERTEX");

    unsigned int textFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* textFragmentShaderSourcePtr = textFragmentShaderSource.data();
    glShaderSource(textFragmentShader, 1, &textFragmentShaderSourcePtr, NULL);
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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), static_cast<void*>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Компиляция шейдеров для линий
    unsigned int lineVertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* lineVertexShaderSourcePtr = lineVertexShaderSource.data();
    glShaderSource(lineVertexShader, 1, &lineVertexShaderSourcePtr, NULL);
    glCompileShader(lineVertexShader);
    checkShaderCompileErrors(lineVertexShader, "LINE_VERTEX");

    unsigned int lineFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* lineFragmentShaderSourcePtr = lineFragmentShaderSource.data();
    glShaderSource(lineFragmentShader, 1, &lineFragmentShaderSourcePtr, NULL);
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), static_cast<void*>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Вершины куба с цветами
    std::array<float, 216> vertices = {
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
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);

    std::string fpsText = "FPS: 0";
    std::string avgFpsText = "Avg: 0";

    double fps = 0.0;

    std::string gpuName = getGPUName();

    float cameraDistance = 5.0f;
    float minDistance = 3.0f;
    float maxDistance = 7.0f;
    float zoomSpeed = 0.5f;

    auto startTime = std::chrono::steady_clock::now();

    std::string monitorInfo = getMonitorInfo(window);
    std::string vramInfo = getVRAMInfo();
    std::string cpuInfo = getCPUInfo();
    std::string ramInfo = getRAMInfo();

    // Используем уже определенную переменную iconPath
    if (!iconPath.empty()) {
        GLFWimage icon = createTransparentIcon(iconPath.c_str(), 32);
        if (icon.pixels) {
            glfwSetWindowIcon(window, 1, &icon);
            stbi_image_free(icon.pixels);
        } else {
            std::cerr << "Не удалось загрузить иконку" << std::endl;
        }
    }

    auto lastFPSUpdateTime = std::chrono::steady_clock::now();

    // Теперь версия программы устанавливается через cmake
    // Убираем эту строку, так как версия уже установлена через define
    // programVersion = calculateMD5(__FILE__);

    // Главный цикл рендеринга
    while (!glfwWindowShouldClose(window))
    {
        // Измеряем FPS
        auto currentTime = std::chrono::steady_clock::now();
        nbFrames++;
        
        auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::duration<double>>(currentTime - lastFPSUpdateTime).count();
        
        if (timeSinceLastUpdate >= 1.0) { // Если пошла 1 секунда
            fps = static_cast<double>(nbFrames) / timeSinceLastUpdate;
            
            if (fps > 0) {
                if (isFirstValidMeasurement) {
                    fpsEstimate = fps;
                    isFirstValidMeasurement = false;
                } else {
                    // Применяем фильтр Калмана
                    fpsEstimate = kalmanFilter(fps, fpsEstimate, fpsErrorEstimate, processNoise, measurementNoise);
                }

                // Обновляем данные графика
                fpsHistory[currentGraphX] = static_cast<float>(fps);
                avgFpsHistory[currentGraphX] = static_cast<float>(fpsEstimate);
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
                auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();

                // Изменяем формат вывода в консоль, оставляем только информацию о FPS
                std::cout << "Время: " << std::setw(4) << elapsedSeconds << "с | FPS: " 
                          << std::setw(7) << std::fixed << std::setprecision(2) << fps 
                          << " | Среднее FPS: " << std::setw(7) << std::fixed << std::setprecision(2) << fpsEstimate 
                          << std::endl;
            }

            nbFrames = 0;
            lastFPSUpdateTime = currentTime;
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

        // Активация шейдерной прграммы
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
        projection = glm::perspective(glm::radians(45.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 100.0f);

        // Вращение вего кубика Рубика
        glm::mat4 rubiksCubeRotation = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));

        // Передача матриц ида и проекции в шейдер
        unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
        unsigned int projectionLoc = glGetUniformLocation(shaderProgram, "projection");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        if (viewLoc == GL_INVALID_INDEX) {
            std::cerr << "Failed to get uniform location for 'view'" << std::endl;
            // Обработка ошибки
        }

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
                    model = rubiksCubeRotation * model; // Примеяем вращение ко всему кубику Рубика
                    model = glm::translate(model, glm::vec3(x * totalSize, y * totalSize, z * totalSize));
                    model = glm::scale(model, glm::vec3(cubeSize, cubeSize, cubeSize));

                    unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
                    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }
            }
        }

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

        // Рисем текущи FPS (красные токи)
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

        float textScale = TEXT_SCALE;
        float textX = 10.0f; // Отступ слева
        float textY = 780.0f; // Начальная позиция сверху
        float lineSpacing = LINE_SPACING; // Расстояние между строками

        // Рендеринг имени GPU
        renderText(gpuName, textX, textY, textScale, glm::vec3(1.0f, 1.0f, 0.0f)); // Желтый цвет
        textY -= lineSpacing;

        // Рендеринг VRAM
        renderText(vramInfo, textX, textY, textScale, glm::vec3(0.7f, 0.7f, 1.0f)); // Светло-голубой цвет
        textY -= lineSpacing;

        // Рендеринг CPU
        renderText(cpuInfo, textX, textY, textScale, glm::vec3(1.0f, 0.7f, 0.7f)); // Светло-красный цвет
        textY -= lineSpacing;

        // Рендеринг RAM
        renderText(ramInfo, textX, textY, textScale, glm::vec3(0.7f, 1.0f, 0.7f)); // Светло-зеленый цвт
        textY -= lineSpacing;

        // Рендеринг информации о мониторе
        renderText(monitorInfo, textX, textY, textScale, glm::vec3(0.7f, 0.7f, 0.7f)); // Светло-серый цвет

        // Рендеринг FPS и AVG FPS рядом с графиком
        fpsStream.str("");
        avgFpsStream.str("");
        fpsStream << std::fixed << std::setprecision(2) << fps;
        avgFpsStream << std::fixed << std::setprecision(2) << fpsEstimate;

        std::string fpsText = "FPS: " + fpsStream.str();
        std::string avgFpsText = "Avg: " + avgFpsStream.str();

        renderText(fpsText, GRAPH_LEFT, GRAPH_BOTTOM - 30, textScale, glm::vec3(1.0f, 0.0f, 0.0f)); // Красный цвет
        renderText(avgFpsText, GRAPH_LEFT + 150, GRAPH_BOTTOM - 30, textScale, glm::vec3(0.0f, 1.0f, 0.0f)); // Зеленый цвет

        // Добвяем подписи к гафику
        std::string maxFpsLabel = "Max: " + std::to_string(static_cast<int>(graphMax));
        std::string minFpsLabel = "Min: " + std::to_string(static_cast<int>(graphMin));
        renderText(maxFpsLabel, GRAPH_LEFT + GRAPH_WIDTH + 5, GRAPH_BOTTOM + GRAPH_HEIGHT - 20, 0.4f, glm::vec3(1.0f, 1.0f, 1.0f));
        renderText(minFpsLabel, GRAPH_LEFT + GRAPH_WIDTH + 5, GRAPH_BOTTOM, 0.4f, glm::vec3(1.0f, 1.0f, 1.0f));

        // Рендеринг версии программы в правом нижнем углу
        std::string versionText = "Version: " + programVersion;
        float versionTextWidth = getTextWidth(versionText, textScale);
        renderText(versionText, WINDOW_WIDTH - versionTextWidth - 10, 10, textScale, glm::vec3(1.0f, 1.0f, 1.0f)); // Белый цвет

        glEnable(GL_DEPTH_TEST);

        // Обмен буферов и обрабтка событий GLFW
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Выводим сглаженное значение FPS в консоль перед завершением программы
    // Очистка ресурсов
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();

    // После выхода из главного цикла
    std::cout << "\nТест завершен.\n" << std::endl;
    std::cout << "Версия программы: " << programVersion << std::endl;
    std::cout << "Итоговые результаты:" << std::endl;
    std::cout << "Минимальное FPS: " << std::fixed << std::setprecision(2) << minFps << std::endl;
    std::cout << "Максимальное FPS: " << std::fixed << std::setprecision(2) << maxFps << std::endl;
    std::cout << "Среднее FPS: " << std::fixed << std::setprecision(2) << fpsEstimate << std::endl;

    return 0;
}

