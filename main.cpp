#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <iomanip> // Для форматирования вывода
#include <sstream>

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

    // Создание окна
    GLFWwindow* window = glfwCreateWindow(800, 800, "GPU Test - Rotating Cube", NULL, NULL);
    if (window == NULL)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // Инициализация GLEW
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // Отключаем VSync
    glfwSwapInterval(0);

    // Добавляем переменные для подсчета FPS и фильтра Калмана
    double lastTime = glfwGetTime();
    int nbFrames = 0;
    double fpsEstimate = 0.0;
    double fpsErrorEstimate = 1.0;
    const double processNoise = 0.01;
    const double measurementNoise = 200.0;
    bool isFirstMeasurement = true;

    // Компиляция шейдеров
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

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

    // Главный цикл рендеринга
    while (!glfwWindowShouldClose(window))
    {
        // Измеряем FPS
        double currentTime = glfwGetTime();
        nbFrames++;
        if (currentTime - lastTime >= 1.0) { // Если прошла 1 секунда
            double fps = static_cast<double>(nbFrames) / (currentTime - lastTime);
            
            if (isFirstMeasurement) {
                fpsEstimate = fps;
                isFirstMeasurement = false;
            } else {
                // Применяем фильтр Калмана
                fpsEstimate = kalmanFilter(fps, fpsEstimate, fpsErrorEstimate, processNoise, measurementNoise);
            }

            // Форматируем строку с текущим и сглаженным FPS
            std::stringstream ss;
            ss << "GPU Test - Rotating Cube - FPS: " << std::fixed << std::setprecision(2) << fps
               << " Avg FPS: " << std::fixed << std::setprecision(2) << fpsEstimate;
            glfwSetWindowTitle(window, ss.str().c_str());

            nbFrames = 0;
            lastTime = currentTime;
        }

        // Очистка буфера цвета и глубины
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Активация шейдерной программы
        glUseProgram(shaderProgram);

        // Создание матриц преобразования
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);

        // Настройка матриц вида и проекции
        view = glm::translate(view, glm::vec3(0.0f, 0.0f, -5.0f));
        projection = glm::perspective(glm::radians(45.0f), 800.0f / 800.0f, 0.1f, 100.0f);

        // Вращение всего кубика Рубика
        glm::mat4 rubiksCubeRotation = glm::rotate(glm::mat4(1.0f), (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));

        // Передача матриц вида и проекции в шейдер
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

        // Обмен буферов и обработка событий GLFW
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Выводим сглаженное значение FPS в консоль перед завершением программы
    std::cout << "Smoothed Average FPS: " << std::fixed << std::setprecision(2) << fpsEstimate << std::endl;

    // Очистка ресурсов
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}