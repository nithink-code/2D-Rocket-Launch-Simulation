#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <cstdio>

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

const int WINDOW_WIDTH = 1000;
const int WINDOW_HEIGHT = 800;
const float PI = 3.1415926535f;

struct Vertex { float x, y, r, g, b, a; };
struct Particle { float x, y, vx, vy, life, r, g, b; };

float worldX = 0.0f;
float worldY = 0.0f;
float rocketX = 0.0f;
float rocketY = -0.7f;
float launchHeight = -0.7f;
float velocityX = 0.0f;
float velocityY = 0.0f;
float launchAngleDeg = 90.0f;
float baseThrust = 1.9f;
float fuel = 6.0f;
bool boostActive = false;

const float gravity = -0.95f;
const float groundY = -0.7f;
const float maxLaunchHeight = 0.2f;
const float minAngle = 20.0f;
const float maxAngle = 160.0f;
const float maxThrust = 3.5f;
const float minThrust = 1.0f;

bool launching = false;
bool paused = false;
bool exploded = false;

std::vector<Particle> particles;
std::vector<Vertex> starVertices;
std::vector<Vertex> hudVertices;

unsigned int shaderProgram;
unsigned int rocketVAO, rocketVBO;
unsigned int particleVAO, particleVBO;
unsigned int starVAO, starVBO;
unsigned int groundVAO, groundVBO;
unsigned int hudVAO, hudVBO;

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

float toRadians(float degrees) {
    return degrees * PI / 180.0f;
}

void appendQuad(std::vector<Vertex>& verts, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    verts.push_back({x0, y0, r, g, b, a});
    verts.push_back({x1, y0, r, g, b, a});
    verts.push_back({x1, y1, r, g, b, a});
    verts.push_back({x0, y0, r, g, b, a});
    verts.push_back({x1, y1, r, g, b, a});
    verts.push_back({x0, y1, r, g, b, a});
}

void appendLineQuad(std::vector<Vertex>& verts, float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.00001f) return;

    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;

    verts.push_back({x0 - nx, y0 - ny, r, g, b, a});
    verts.push_back({x0 + nx, y0 + ny, r, g, b, a});
    verts.push_back({x1 + nx, y1 + ny, r, g, b, a});
    verts.push_back({x0 - nx, y0 - ny, r, g, b, a});
    verts.push_back({x1 + nx, y1 + ny, r, g, b, a});
    verts.push_back({x1 - nx, y1 - ny, r, g, b, a});
}

void appendSevenSegmentDigit(std::vector<Vertex>& verts, int digit, float x, float y, float w, float h, float t, float r, float g, float b, float a) {
    static const int masks[10] = {
        0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
        0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
    };

    if (digit < 0 || digit > 9) return;
    int m = masks[digit];

    float xL = x;
    float xR = x + w;
    float yB = y;
    float yM = y + h * 0.5f;
    float yT = y + h;

    if (m & (1 << 6)) appendQuad(verts, xL, yT - t, xR, yT, r, g, b, a);
    if (m & (1 << 5)) appendQuad(verts, xR - t, yM, xR, yT, r, g, b, a);
    if (m & (1 << 4)) appendQuad(verts, xR - t, yB, xR, yM, r, g, b, a);
    if (m & (1 << 3)) appendQuad(verts, xL, yB, xR, yB + t, r, g, b, a);
    if (m & (1 << 2)) appendQuad(verts, xL, yB, xL + t, yM, r, g, b, a);
    if (m & (1 << 1)) appendQuad(verts, xL, yM, xL + t, yT, r, g, b, a);
    if (m & (1 << 0)) appendQuad(verts, xL, yM - t * 0.5f, xR, yM + t * 0.5f, r, g, b, a);
}

void appendAngleDisplay(std::vector<Vertex>& verts, float angleDeg) {
    int angleInt = (int)std::round(clampf(angleDeg, minAngle, maxAngle));
    int hundreds = angleInt / 100;
    int tens = (angleInt / 10) % 10;
    int ones = angleInt % 10;

    appendQuad(verts, -0.72f, 0.615f, -0.59f, 0.675f, 0.03f, 0.06f, 0.09f, 0.95f);

    float digitW = 0.027f;
    float digitH = 0.042f;
    float digitT = 0.0048f;
    float y = 0.624f;

    if (hundreds > 0) {
        appendSevenSegmentDigit(verts, hundreds, -0.712f, y, digitW, digitH, digitT, 0.85f, 0.98f, 0.52f, 0.98f);
    }
    appendSevenSegmentDigit(verts, tens, -0.682f, y, digitW, digitH, digitT, 0.85f, 0.98f, 0.52f, 0.98f);
    appendSevenSegmentDigit(verts, ones, -0.652f, y, digitW, digitH, digitT, 0.85f, 0.98f, 0.52f, 0.98f);
    appendQuad(verts, -0.622f, 0.654f, -0.615f, 0.661f, 0.85f, 0.98f, 0.52f, 0.98f);
}

void buildHUDOverlay() {
    hudVertices.clear();

    float panelLeft = -0.98f;
    float panelRight = -0.58f;
    float panelBottom = 0.60f;
    float panelTop = 0.98f;
    appendQuad(hudVertices, panelLeft, panelBottom, panelRight, panelTop, 0.02f, 0.04f, 0.07f, 0.82f);
    appendQuad(hudVertices, panelLeft + 0.01f, panelTop - 0.055f, panelRight - 0.01f, panelTop - 0.02f, 0.08f, 0.25f, 0.35f, 0.95f);

    auto addMeter = [&](float yCenter, float normalized, float r, float g, float b) {
        float trackLeft = panelLeft + 0.05f;
        float trackRight = panelRight - 0.05f;
        float trackHalf = 0.017f;
        normalized = clampf(normalized, 0.0f, 1.0f);

        appendQuad(hudVertices, trackLeft, yCenter - trackHalf, trackRight, yCenter + trackHalf, 0.11f, 0.14f, 0.16f, 0.9f);
        appendQuad(hudVertices, trackLeft, yCenter - trackHalf, trackLeft + (trackRight - trackLeft) * normalized, yCenter + trackHalf, r, g, b, 0.95f);
    };

    float fuelNorm = clampf(fuel / 6.0f, 0.0f, 1.0f);
    float thrustNorm = clampf((baseThrust - minThrust) / (maxThrust - minThrust), 0.0f, 1.0f);
    float angleNorm = clampf((launchAngleDeg - minAngle) / (maxAngle - minAngle), 0.0f, 1.0f);
    float heightNorm = clampf((launchHeight - groundY) / (maxLaunchHeight - groundY), 0.0f, 1.0f);

    addMeter(0.865f, fuelNorm, 0.95f, 0.45f, 0.15f);
    addMeter(0.805f, thrustNorm, 0.95f, 0.9f, 0.2f);
    addMeter(0.745f, angleNorm, 0.55f, 1.0f, 0.4f);
    addMeter(0.685f, heightNorm, 0.35f, 0.8f, 1.0f);

    float dialX = -0.78f;
    float dialY = 0.665f;
    float dialR = 0.055f;
    appendQuad(hudVertices, dialX - dialR - 0.01f, dialY - dialR - 0.01f, dialX + dialR + 0.01f, dialY + dialR + 0.01f, 0.05f, 0.08f, 0.12f, 0.95f);
    appendLineQuad(hudVertices, dialX, dialY, dialX + std::cos(toRadians(launchAngleDeg)) * dialR, dialY + std::sin(toRadians(launchAngleDeg)) * dialR, 0.012f, 0.65f, 1.0f, 0.4f, 1.0f);

    float modeR = launching ? 0.15f : 0.08f;
    float modeG = launching ? 0.78f : 0.6f;
    float modeB = launching ? 0.3f : 0.9f;
    if (paused) { modeR = 0.95f; modeG = 0.85f; modeB = 0.25f; }
    if (exploded) { modeR = 1.0f; modeG = 0.25f; modeB = 0.2f; }
    appendQuad(hudVertices, -0.64f, 0.635f, -0.60f, 0.675f, modeR, modeG, modeB, 0.95f);

    appendAngleDisplay(hudVertices, launchAngleDeg);

    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
    glBufferData(GL_ARRAY_BUFFER, hudVertices.size() * sizeof(Vertex), hudVertices.data(), GL_STREAM_DRAW);
}

void updateWindowTitle(GLFWwindow* window) {
    char title[200];
    std::snprintf(
        title,
        sizeof(title),
        "Rocket Launch | Angle: %.1f deg | Height: %.2f | Thrust: %.2f | Fuel: %.1f",
        launchAngleDeg,
        launchHeight,
        baseThrust,
        fuel
    );
    glfwSetWindowTitle(window, title);
}

void resetRocketState() {
    rocketX = 0.0f;
    launchHeight = groundY;
    rocketY = launchHeight;
    velocityX = 0.0f;
    velocityY = 0.0f;
    worldX = 0.0f;
    worldY = 0.0f;
    launchAngleDeg = 90.0f;
    baseThrust = 1.9f;
    fuel = 6.0f;
    boostActive = false;
    launching = false;
    paused = false;
    exploded = false;
    particles.clear();
}

unsigned int createShaderProgram() {
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void initBuffers() {
    for (int i = 0; i < 400; i++) {
        starVertices.push_back({(rand() % 4000 - 2000) / 1000.0f, (rand() % 8000 - 4000) / 1000.0f, 1, 1, 1, (float)(rand() % 100) / 100.0f});
    }
    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);
    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, starVertices.size() * sizeof(Vertex), starVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    std::vector<Vertex> rocketGeom = {
        {-0.06f, 0.0f, 0.8f, 0.8f, 0.8f, 1.0f}, {0.06f, 0.0f, 0.8f, 0.8f, 0.8f, 1.0f}, {0.06f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f},
        {-0.06f, 0.0f, 0.8f, 0.8f, 0.8f, 1.0f}, {0.06f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f}, {-0.06f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f},
        {-0.06f, 0.1f, 0.3f, 0.3f, 0.3f, 1.0f}, {-0.15f, -0.05f, 0.2f, 0.2f, 0.2f, 1.0f}, {-0.06f, 0.3f, 0.3f, 0.3f, 0.3f, 1.0f},
        {0.06f, 0.1f, 0.3f, 0.3f, 0.3f, 1.0f}, {0.15f, -0.05f, 0.2f, 0.2f, 0.2f, 1.0f}, {0.06f, 0.3f, 0.3f, 0.3f, 0.3f, 1.0f},
        {-0.06f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f}, {0.06f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.75f, 0.6f, 0.0f, 0.0f, 1.0f}
    };
    glGenVertexArrays(1, &rocketVAO);
    glGenBuffers(1, &rocketVBO);
    glBindVertexArray(rocketVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rocketVBO);
    glBufferData(GL_ARRAY_BUFFER, rocketGeom.size() * sizeof(Vertex), rocketGeom.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    std::vector<Vertex> groundGeom = {
        {-1.5f, -1.0f, 0.1f, 0.1f, 0.1f, 1.0f}, {1.5f, -1.0f, 0.1f, 0.1f, 0.1f, 1.0f}, {1.5f, -0.85f, 0.2f, 0.2f, 0.2f, 1.0f},
        {-1.5f, -1.0f, 0.1f, 0.1f, 0.1f, 1.0f}, {1.5f, -0.85f, 0.2f, 0.2f, 0.2f, 1.0f}, {-1.5f, -0.85f, 0.2f, 0.2f, 0.2f, 1.0f}
    };
    glGenVertexArrays(1, &groundVAO);
    glGenBuffers(1, &groundVBO);
    glBindVertexArray(groundVAO);
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, groundGeom.size() * sizeof(Vertex), groundGeom.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &particleVAO);
    glGenBuffers(1, &particleVBO);

    glGenVertexArrays(1, &hudVAO);
    glGenBuffers(1, &hudVBO);
    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
    glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void processContinuousInput(GLFWwindow* window, float dt) {
    if (paused) return;

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        launchAngleDeg -= 80.0f * dt;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        launchAngleDeg += 80.0f * dt;
    }
    launchAngleDeg = clampf(launchAngleDeg, minAngle, maxAngle);

    if (!launching) {
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) launchHeight += 0.75f * dt;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) launchHeight -= 0.75f * dt;
        launchHeight = clampf(launchHeight, groundY, maxLaunchHeight);

        rocketY = launchHeight;
        velocityX = 0.0f;
        velocityY = 0.0f;

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) baseThrust += 1.2f * dt;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) baseThrust -= 1.2f * dt;
    }

    baseThrust = clampf(baseThrust, minThrust, maxThrust);
    boostActive = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
}

void update(float dt) {
    if (paused) return;

    if (launching && !exploded) {
        float angle = toRadians(launchAngleDeg);
        float dirX = std::cos(angle);
        float dirY = std::sin(angle);

        float thrustNow = 0.0f;
        if (fuel > 0.0f) {
            thrustNow = baseThrust;
            fuel = std::max(0.0f, fuel - dt);
            if (boostActive) thrustNow *= 1.35f;
        }

        velocityX += dirX * thrustNow * dt;
        velocityY += (dirY * thrustNow + gravity) * dt;
        rocketX += velocityX * dt;
        rocketY += velocityY * dt;

        float nozzleX = rocketX - dirX * 0.08f;
        float nozzleY = rocketY - dirY * 0.08f;
        for (int i = 0; i < 13; i++) {
            float spread = ((rand() % 100) - 50) / 500.0f;
            float speed = 0.35f + (rand() % 80) / 100.0f;
            particles.push_back({
                nozzleX + spread,
                nozzleY + spread,
                -dirX * speed + ((rand() % 30) - 15) / 100.0f,
                -dirY * speed + ((rand() % 30) - 15) / 100.0f,
                0.95f,
                1.0f,
                0.35f + (rand() % 50) / 100.0f,
                0.0f
            });
        }

        if (rocketY <= groundY && velocityY < 0.0f) {
            bool hardImpact = std::fabs(velocityY) > 1.25f || std::fabs(launchAngleDeg - 90.0f) > 36.0f;
            if (hardImpact) {
                exploded = true;
                for (int i = 0; i < 220; i++) {
                    float theta = (rand() % 628) / 100.0f;
                    float blastSpeed = 0.2f + (rand() % 220) / 100.0f;
                    particles.push_back({
                        rocketX,
                        groundY,
                        std::cos(theta) * blastSpeed,
                        std::sin(theta) * blastSpeed,
                        1.1f,
                        1.0f,
                        0.2f + (rand() % 70) / 100.0f,
                        0.05f
                    });
                }
            }
            launching = false;
            launchHeight = groundY;
            rocketY = groundY;
            velocityX *= 0.25f;
            velocityY = 0.0f;
            fuel = 0.0f;
        }
    }

    float desiredWorldX = worldX;
    float desiredWorldY = worldY;
    float screenX = rocketX + worldX;
    float screenY = rocketY + worldY;

    if (screenX > 0.62f) desiredWorldX -= (screenX - 0.62f);
    if (screenX < -0.62f) desiredWorldX -= (screenX + 0.62f);
    if (screenY > 0.55f) desiredWorldY -= (screenY - 0.55f);
    if (screenY < -0.40f) desiredWorldY -= (screenY + 0.40f);

    float follow = clampf(5.5f * dt, 0.0f, 1.0f);
    worldX += (desiredWorldX - worldX) * follow;
    worldY += (desiredWorldY - worldY) * follow;

    for (auto& p : particles) {
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += gravity * 0.22f * dt;
        p.life -= 1.6f * dt;
    }

    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) {
        return p.life <= 0.0f;
    }), particles.end());
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;

    if (key == GLFW_KEY_S && action == GLFW_PRESS && !launching && !exploded) {
        launching = true;
        float angle = toRadians(launchAngleDeg);
        velocityX = std::cos(angle) * 0.30f;
        velocityY = std::sin(angle) * 0.30f;
    }
    if (key == GLFW_KEY_P && action == GLFW_PRESS) paused = !paused;
    if (key == GLFW_KEY_R && action == GLFW_PRESS) resetRocketState();
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Rocket Launch", NULL, NULL);
    if (!window) return -1;

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    initBuffers();
    shaderProgram = createShaderProgram();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    std::cout << "Controls:\n"
              << "S = Launch, R = Reset, P = Pause\n"
              << "Left/Right or A/D = Change launch angle\n"
              << "Up/Down = Set projection height before launch\n"
              << "Q/E = Change thrust before launch\n"
              << "Hold Left Shift = Temporary boost\n";

    resetRocketState();
    float lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float now = glfwGetTime();
        float dt = now - lastTime;
        lastTime = now;
        dt = clampf(dt, 0.0f, 0.03f);

        processContinuousInput(window, dt);
        update(dt);
        buildHUDOverlay();
        updateWindowTitle(window);

        float altitudeTint = clampf((rocketY - groundY) / 8.0f, 0.0f, 1.0f);
        glClearColor(0.01f, 0.01f + altitudeTint * 0.05f, 0.04f + altitudeTint * 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);

        float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, worldX,worldY,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, view);

        float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, identity);

        glPointSize(2.0f);
        glBindVertexArray(starVAO);
        glDrawArrays(GL_POINTS, 0, (GLsizei)starVertices.size());

        glBindVertexArray(groundVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (!particles.empty()) {
            std::vector<Vertex> pVerts;
            pVerts.reserve(particles.size());
            for (const auto& p : particles) {
                pVerts.push_back({p.x, p.y, p.r, p.g, p.b, p.life});
            }
            glBindVertexArray(particleVAO);
            glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
            glBufferData(GL_ARRAY_BUFFER, pVerts.size() * sizeof(Vertex), pVerts.data(), GL_STREAM_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glPointSize(5.0f);
            glDrawArrays(GL_POINTS, 0, (GLsizei)pVerts.size());
        }

        float shakeX = (launching && fuel > 0.0f) ? ((rand() % 10) - 5) / 1200.0f : 0.0f;
        float shakeY = (launching && fuel > 0.0f) ? ((rand() % 10) - 5) / 1200.0f : 0.0f;
        float angle = toRadians(launchAngleDeg - 90.0f);
        float c = std::cos(angle);
        float s = std::sin(angle);
        float rocketModel[16] = {c,s,0,0, -s,c,0,0, 0,0,1,0, rocketX + shakeX,rocketY + shakeY,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, rocketModel);

        if (!exploded) {
            glBindVertexArray(rocketVAO);
            glDrawArrays(GL_TRIANGLES, 0, 15);
        }

        float uiView[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, uiView);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, identity);
        glBindVertexArray(hudVAO);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)hudVertices.size());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
