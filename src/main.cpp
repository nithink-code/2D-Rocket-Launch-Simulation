#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>

// --- Shaders ---
const char* vertexShaderSource = R"glsl(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec4 aColor;
    out vec4 ourColor;
    uniform mat4 model;
    uniform mat4 view;
    void main() {
        gl_Position = view * model * vec4(aPos, 0.0, 1.0);
        ourColor = aColor;
    }
)glsl";

const char* fragmentShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;
    in vec4 ourColor;
    void main() {
        FragColor = ourColor;
    }
)glsl";

// --- Configuration ---
const int WINDOW_WIDTH = 1000;
const int WINDOW_HEIGHT = 800;

struct Vertex { float x, y, r, g, b, a; };
struct Particle { float x, y, vx, vy, life, r, g, b; };

// --- State ---
float worldY = 0.0f;          // Camera/World offset
float rocketY = -0.7f;        // Rocket's world position
float rocketScreenY = -0.7f;  // Rocket's screen position
float velocity = 0.0f;
float acceleration = 0.0001f;
bool launching = false;
std::vector<Particle> particles;
std::vector<Vertex> starVertices;

unsigned int shaderProgram;
unsigned int rocketVAO, rocketVBO;
unsigned int particleVAO, particleVBO;
unsigned int starVAO, starVBO;
unsigned int groundVAO, groundVBO;

// --- Helper Functions ---
unsigned int createShaderProgram() {
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

void initBuffers() {
    // 1. Stars (Static in world space)
    for (int i = 0; i < 400; i++) {
        starVertices.push_back({(rand()%4000-2000)/1000.0f, (rand()%8000-4000)/1000.0f, 1,1,1, (float)(rand()%100)/100.0f});
    }
    glGenVertexArrays(1, &starVAO); glGenBuffers(1, &starVBO);
    glBindVertexArray(starVAO); glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, starVertices.size()*sizeof(Vertex), starVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2*sizeof(float))); glEnableVertexAttribArray(1);

    // 2. Rocket
    std::vector<Vertex> rocketGeom = {
        {-0.06f, 0.0f, 0.8f, 0.8f, 0.8f, 1.0f}, {0.06f, 0.0f, 0.8f, 0.8f, 0.8f, 1.0f}, {0.06f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f},
        {-0.06f, 0.0f, 0.8f, 0.8f, 0.8f, 1.0f}, {0.06f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f}, {-0.06f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f},
        {-0.06f, 0.1f, 0.3f, 0.3f, 0.3f, 1.0f}, {-0.15f, -0.05f, 0.2f, 0.2f, 0.2f, 1.0f}, {-0.06f, 0.3f, 0.3f, 0.3f, 1.0f},
        {0.06f, 0.1f, 0.3f, 0.3f, 0.3f, 1.0f}, {0.15f, -0.05f, 0.2f, 0.2f, 0.2f, 1.0f}, {0.06f, 0.3f, 0.3f, 0.3f, 1.0f},
        {-0.06f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f}, {0.06f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.75f, 0.6f, 0.0f, 0.0f, 1.0f}
    };
    glGenVertexArrays(1, &rocketVAO); glGenBuffers(1, &rocketVBO);
    glBindVertexArray(rocketVAO); glBindBuffer(GL_ARRAY_BUFFER, rocketVBO);
    glBufferData(GL_ARRAY_BUFFER, rocketGeom.size()*sizeof(Vertex), rocketGeom.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2*sizeof(float))); glEnableVertexAttribArray(1);

    // 3. Ground
    std::vector<Vertex> groundGeom = { {-1.5f, -1.0f, 0.1f,0.1f,0.1f,1}, {1.5f, -1.0f, 0.1f,0.1f,0.1f,1}, {1.5f, -0.85f, 0.2f,0.2f,0.2f,1},
                                     {-1.5f, -1.0f, 0.1f,0.1f,0.1f,1}, {1.5f, -0.85f, 0.2f,0.2f,0.2f,1}, {-1.5f, -0.85f, 0.2f,0.2f,0.2f,1} };
    glGenVertexArrays(1, &groundVAO); glGenBuffers(1, &groundVBO);
    glBindVertexArray(groundVAO); glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, groundGeom.size()*sizeof(Vertex), groundGeom.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2*sizeof(float))); glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &particleVAO); glGenBuffers(1, &particleVBO);
}

void update(float dt) {
    if (launching) {
        velocity += acceleration;
        rocketY += velocity;
        
        // Camera logic: Rocket stays centered after a certain height
        if (rocketY > 0.0f) {
            worldY = -rocketY;
            rocketScreenY = 0.0f;
        } else {
            rocketScreenY = rocketY;
        }

        // Add flame/smoke
        for (int i = 0; i < 15; i++) {
            particles.push_back({((rand()%60)-30)/1000.0f, rocketY-0.05f, ((rand()%40)-20)/1000.0f, -0.01f - (rand()%15)/1000.0f, 1.0f, 1.0f, (float)(rand()%100)/100.0f, 0.0f});
        }
    }

    for (int i = 0; i < (int)particles.size(); i++) {
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].life -= 1.5f * dt;
        if (particles[i].life <= 0) { particles.erase(particles.begin() + i); i--; }
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_S && action == GLFW_PRESS) launching = true;
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        rocketY = -0.7f; rocketScreenY = -0.7f; worldY = 0.0f; velocity = 0; launching = false; particles.clear();
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Infinite Rocket Launch", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    initBuffers(); shaderProgram = createShaderProgram();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float dt = glfwGetTime() - lastTime; lastTime = glfwGetTime();
        update(dt);
        glClearColor(0.01f, 0.01f, 0.04f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); glUseProgram(shaderProgram);

        // View Matrix (The Camera)
        float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,worldY,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, view);
        
        // Background and World Items
        float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, identity);
        glBindVertexArray(starVAO); glDrawArrays(GL_POINTS, 0, 400);
        glBindVertexArray(groundVAO); glDrawArrays(GL_TRIANGLES, 0, 6);

        // Particles
        if (!particles.empty()) {
            std::vector<Vertex> pVerts;
            for(auto& p : particles) pVerts.push_back({p.x, p.y, p.r, p.g, p.b, p.life});
            glBindVertexArray(particleVAO); glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
            glBufferData(GL_ARRAY_BUFFER, pVerts.size()*sizeof(Vertex), pVerts.data(), GL_STREAM_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2*sizeof(float))); glEnableVertexAttribArray(1);
            glDrawArrays(GL_POINTS, 0, pVerts.size());
        }

        // Rocket (Screen-Stabilized)
        float shake = (launching && rocketY < 3.0f) ? ((rand()%10)-5)/1000.0f : 0.0f;
        float rocketModel[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, shake,rocketY,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, rocketModel);
        glBindVertexArray(rocketVAO); glDrawArrays(GL_TRIANGLES, 0, 15);

        glfwSwapBuffers(window); glfwPollEvents();
    }
    glfwTerminate(); return 0;
}
