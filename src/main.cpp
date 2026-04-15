#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <cstdio>
#include <cstring>

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
const int WINDOW_HEIGHT = 1000;
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

float launchStartX = 0.0f;
float launchStartY = groundY;
float telemetryHeightCovered = 0.0f;
float telemetryMaxHeightCovered = 0.0f;
float telemetryHorizontalDistance = 0.0f;
float telemetrySpeed = 0.0f;
float telemetryFlightAngleDeg = 90.0f;
float flightTime = 0.0f;

bool cinematicCamera = true;
int cameraPresetIndex = 1;

struct CameraPreset {
    float follow;
    float deadZoneX;
    float deadZoneY;
    float lookAhead;
    float verticalBias;
};

const CameraPreset CAMERA_PRESETS[3] = {
    {3.8f, 0.26f, 0.18f, 0.06f, -0.01f},
    {5.8f, 0.20f, 0.14f, 0.10f, -0.03f},
    {8.6f, 0.14f, 0.10f, 0.14f, -0.05f}
};

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

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    glViewport(0, 0, width, height);
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

void appendSevenSegmentMinus(std::vector<Vertex>& verts, float x, float y, float w, float h, float t, float r, float g, float b, float a) {
    float yM = y + h * 0.5f;
    appendQuad(verts, x, yM - t * 0.5f, x + w, yM + t * 0.5f, r, g, b, a);
}

void appendSevenSegmentPlus(std::vector<Vertex>& verts, float x, float y, float w, float h, float t, float r, float g, float b, float a) {
    float yM = y + h * 0.5f;
    float xM = x + w * 0.5f;
    appendQuad(verts, x, yM - t * 0.5f, x + w, yM + t * 0.5f, r, g, b, a);
    appendQuad(verts, xM - t * 0.5f, y + h * 0.2f, xM + t * 0.5f, y + h * 0.8f, r, g, b, a);
}

void appendSevenSegmentDot(std::vector<Vertex>& verts, float x, float y, float w, float t, float r, float g, float b, float a) {
    float pad = w * 0.35f;
    appendQuad(verts, x + pad, y, x + w - pad, y + t, r, g, b, a);
}

void appendSegmentText(std::vector<Vertex>& verts, const char* text, float x, float y, float w, float h, float t, float gap, float r, float g, float b, float a) {
    float cursor = x;
    for (size_t i = 0; i < std::strlen(text); i++) {
        char ch = text[i];
        if (ch >= '0' && ch <= '9') {
            appendSevenSegmentDigit(verts, ch - '0', cursor, y, w, h, t, r, g, b, a);
            cursor += w + gap;
        } else if (ch == '-') {
            appendSevenSegmentMinus(verts, cursor, y, w, h, t, r, g, b, a);
            cursor += w + gap;
        } else if (ch == '+') {
            appendSevenSegmentPlus(verts, cursor, y, w, h, t, r, g, b, a);
            cursor += w + gap;
        } else if (ch == '.') {
            appendSevenSegmentDot(verts, cursor, y, w, t, r, g, b, a);
            cursor += w * 0.65f + gap;
        } else {
            cursor += w * 0.5f + gap;
        }
    }
}

float getSegmentTextWidth(const char* text, float w, float gap) {
    float width = 0.0f;
    for (size_t i = 0; i < std::strlen(text); i++) {
        char ch = text[i];
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+') {
            width += w + gap;
        } else if (ch == '.') {
            width += w * 0.65f + gap;
        } else {
            width += w * 0.5f + gap;
        }
    }
    return width;
}

void appendMetricCard(std::vector<Vertex>& verts, float x0, float y0, float x1, float y1, float accentR, float accentG, float accentB) {
    appendQuad(verts, x0, y0, x1, y1, 0.03f, 0.06f, 0.10f, 0.90f);
    appendQuad(verts, x0, y1 - 0.010f, x1, y1, accentR, accentG, accentB, 0.95f);
    appendQuad(verts, x0 + 0.008f, y0 + 0.008f, x1 - 0.008f, y1 - 0.015f, 0.01f, 0.03f, 0.06f, 0.78f);
}

const char** getGlyphPattern(char c) {
    static const char* A[7] = {"01110", "10001", "10001", "11111", "10001", "10001", "10001"};
    static const char* C[7] = {"01110", "10001", "10000", "10000", "10000", "10001", "01110"};
    static const char* D[7] = {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
    static const char* E[7] = {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
    static const char* F[7] = {"11111", "10000", "10000", "11110", "10000", "10000", "10000"};
    static const char* G[7] = {"01110", "10001", "10000", "10111", "10001", "10001", "01110"};
    static const char* H[7] = {"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
    static const char* I[7] = {"11111", "00100", "00100", "00100", "00100", "00100", "11111"};
    static const char* L[7] = {"10000", "10000", "10000", "10000", "10000", "10000", "11111"};
    static const char* M[7] = {"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
    static const char* N[7] = {"10001", "11001", "10101", "10011", "10001", "10001", "10001"};
    static const char* O[7] = {"01110", "10001", "10001", "10001", "10001", "10001", "01110"};
    static const char* P[7] = {"11110", "10001", "10001", "11110", "10000", "10000", "10000"};
    static const char* R[7] = {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
    static const char* S[7] = {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
    static const char* T[7] = {"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
    static const char* U[7] = {"10001", "10001", "10001", "10001", "10001", "10001", "01110"};
    static const char* V[7] = {"10001", "10001", "10001", "10001", "10001", "01010", "00100"};
    static const char* X[7] = {"10001", "10001", "01010", "00100", "01010", "10001", "10001"};
    static const char* Y[7] = {"10001", "10001", "01010", "00100", "00100", "00100", "00100"};

    switch (c) {
        case 'A': return A;
        case 'C': return C;
        case 'D': return D;
        case 'E': return E;
        case 'F': return F;
        case 'G': return G;
        case 'H': return H;
        case 'I': return I;
        case 'L': return L;
        case 'M': return M;
        case 'N': return N;
        case 'O': return O;
        case 'P': return P;
        case 'R': return R;
        case 'S': return S;
        case 'T': return T;
        case 'U': return U;
        case 'V': return V;
        case 'X': return X;
        case 'Y': return Y;
        default: return nullptr;
    }
}

void appendBitmapText(std::vector<Vertex>& verts, const char* text, float x, float y, float pixel, float r, float g, float b, float a) {
    float cursorX = x;
    const float glyphWidth = 5.0f * pixel;
    const float glyphAdvance = 6.2f * pixel;

    for (size_t i = 0; i < std::strlen(text); i++) {
        char ch = text[i];
        if (ch == ' ') {
            cursorX += glyphAdvance;
            continue;
        }

        const char** glyph = getGlyphPattern(ch);
        if (!glyph) {
            cursorX += glyphAdvance;
            continue;
        }

        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row][col] == '1') {
                    float px0 = cursorX + col * pixel;
                    float py0 = y - row * pixel;
                    appendQuad(verts, px0, py0 - pixel, px0 + pixel, py0, r, g, b, a);
                }
            }
        }

        cursorX += glyphAdvance;
        if (cursorX > x + 400.0f * pixel + glyphWidth) {
            break;
        }
    }
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
    float panelRight = -0.20f; // Widened panel
    float panelBottom = -0.995f;
    float panelTop = -0.05f; // Higher top
    appendQuad(hudVertices, panelLeft, panelBottom, panelRight, panelTop, 0.015f, 0.03f, 0.055f, 0.90f);
    appendQuad(hudVertices, panelLeft + 0.012f, panelTop - 0.065f, panelRight - 0.012f, panelTop - 0.020f, 0.07f, 0.32f, 0.45f, 0.97f);

    float sectionSplit = -0.48f; // Adjusted split for more bar space
    appendQuad(hudVertices, panelLeft + 0.014f, sectionSplit - 0.003f, panelRight - 0.014f, sectionSplit + 0.003f, 0.11f, 0.20f, 0.28f, 0.95f);

    auto addMeter = [&](float yCenter, float normalized, float r, float g, float b) {
        float trackLeft = panelLeft + 0.22f; // More horizontal spacing
        float trackRight = panelRight - 0.04f;
        float trackHalf = 0.0105f;
        normalized = clampf(normalized, 0.0f, 1.0f);

        appendQuad(hudVertices, trackLeft, yCenter - trackHalf, trackRight, yCenter + trackHalf, 0.11f, 0.14f, 0.16f, 0.9f);
        appendQuad(hudVertices, trackLeft, yCenter - trackHalf, trackLeft + (trackRight - trackLeft) * normalized, yCenter + trackHalf, r, g, b, 0.95f);
    };

    const float meterLabelPixel = 0.0038f;
    float labelX = panelLeft + 0.035f; 
    float barYStart = -0.120f; // Start higher
    float barGap = 0.062f; // More gap

    appendBitmapText(hudVertices, "FUEL", labelX, barYStart + 0.005f, meterLabelPixel, 0.96f, 0.88f, 0.78f, 0.98f);
    appendBitmapText(hudVertices, "THRUST", labelX, barYStart - barGap + 0.005f, meterLabelPixel, 0.96f, 0.88f, 0.78f, 0.98f);
    appendBitmapText(hudVertices, "ANGLE", labelX, barYStart - 2 * barGap + 0.005f, meterLabelPixel, 0.96f, 0.88f, 0.78f, 0.98f);
    appendBitmapText(hudVertices, "HEIGHT", labelX, barYStart - 3 * barGap + 0.005f, meterLabelPixel, 0.96f, 0.88f, 0.78f, 0.98f);
    appendBitmapText(hudVertices, "SPEED", labelX, barYStart - 4 * barGap + 0.005f, meterLabelPixel, 0.96f, 0.88f, 0.78f, 0.98f);
    appendBitmapText(hudVertices, "DIST", labelX, barYStart - 5 * barGap + 0.005f, meterLabelPixel, 0.96f, 0.88f, 0.78f, 0.98f);

    float fuelNorm = clampf(fuel / 6.0f, 0.0f, 1.0f);
    float thrustNorm = clampf((baseThrust - minThrust) / (maxThrust - minThrust), 0.0f, 1.0f);
    float angleNorm = clampf((launchAngleDeg - minAngle) / (maxAngle - minAngle), 0.0f, 1.0f);
    float heightNorm = clampf(telemetryHeightCovered / 2.2f, 0.0f, 1.0f);
    float speedNorm = clampf(telemetrySpeed / 2.8f, 0.0f, 1.0f);
    float horizontalNorm = clampf(telemetryHorizontalDistance / 2.8f, 0.0f, 1.0f);

    addMeter(barYStart, fuelNorm, 0.95f, 0.45f, 0.15f);
    addMeter(barYStart - barGap, thrustNorm, 0.95f, 0.90f, 0.2f);
    addMeter(barYStart - 2 * barGap, angleNorm, 0.55f, 1.0f, 0.4f);
    addMeter(barYStart - 3 * barGap, heightNorm, 0.35f, 0.8f, 1.0f);
    addMeter(barYStart - 4 * barGap, speedNorm, 1.0f, 0.55f, 0.25f);
    addMeter(barYStart - 5 * barGap, horizontalNorm, 0.8f, 0.85f, 1.0f);

    float dialX = panelLeft + 0.105f;
    float dialY = -0.84f; 
    float dialR = 0.055f;
    appendQuad(hudVertices, dialX - dialR - 0.01f, dialY - dialR - 0.01f, dialX + dialR + 0.01f, dialY + dialR + 0.01f, 0.05f, 0.08f, 0.12f, 0.95f);
    appendLineQuad(hudVertices, dialX, dialY, dialX + std::cos(toRadians(launchAngleDeg)) * dialR, dialY + std::sin(toRadians(launchAngleDeg)) * dialR, 0.011f, 0.65f, 1.0f, 0.4f, 1.0f);

    float velDialX = panelLeft + 0.255f;
    float velDialY = -0.84f;
    float velDialR = 0.055f;
    appendQuad(hudVertices, velDialX - velDialR - 0.01f, velDialY - velDialR - 0.01f, velDialX + velDialR + 0.01f, velDialY + velDialR + 0.01f, 0.05f, 0.08f, 0.12f, 0.95f);
    float velNeedleLen = velDialR * (0.25f + speedNorm * 0.75f);
    appendLineQuad(hudVertices, velDialX, velDialY, velDialX + std::cos(toRadians(telemetryFlightAngleDeg)) * velNeedleLen, velDialY + std::sin(toRadians(telemetryFlightAngleDeg)) * velNeedleLen, 0.010f, 1.0f, 0.65f, 0.3f, 1.0f);

    appendBitmapText(hudVertices, "ANGLE", panelLeft + 0.055f, -0.735f, 0.0031f, 0.60f, 0.95f, 1.0f, 0.95f);
    appendBitmapText(hudVertices, "VECTOR", panelLeft + 0.200f, -0.735f, 0.0031f, 1.0f, 0.70f, 0.40f, 0.95f);

    float modeR = launching ? 0.15f : 0.08f;
    float modeG = launching ? 0.78f : 0.6f;
    float modeB = launching ? 0.3f : 0.9f;
    if (paused) { modeR = 0.95f; modeG = 0.85f; modeB = 0.25f; }
    if (exploded) { modeR = 1.0f; modeG = 0.25f; modeB = 0.2f; }
    appendQuad(hudVertices, panelRight - 0.070f, panelTop - 0.055f, panelRight - 0.030f, panelTop - 0.020f, modeR, modeG, modeB, 0.95f);
    appendBitmapText(hudVertices, "CAM MODE", panelRight - 0.240f, panelTop - 0.030f, 0.0034f, 0.85f, 0.9f, 1.0f, 0.96f);

    float leftX0 = panelLeft + 0.36f; // More right to clear dials
    float rightX0 = panelLeft + 0.60f;
    // Card dimensions — label on top, value on bottom, clearly separated
    float leftCardW  = 0.22f;
    float rightCardW = 0.21f;
    float cardH      = 0.110f;   // tall enough for label + gap + value
    float row0Top    = -0.50f;
    float rowGap     = 0.125f;   // gap between successive cards
    float row1Top    = row0Top - rowGap;
    float row2Top    = row1Top - rowGap;

    appendMetricCard(hudVertices, leftX0,  row0Top - cardH, leftX0  + leftCardW,  row0Top, 0.35f, 0.80f, 1.0f);
    appendMetricCard(hudVertices, rightX0, row0Top - cardH, rightX0 + rightCardW, row0Top, 1.0f,  0.56f, 0.25f);
    appendMetricCard(hudVertices, leftX0,  row1Top - cardH, leftX0  + leftCardW,  row1Top, 0.65f, 1.0f,  0.55f);
    appendMetricCard(hudVertices, rightX0, row1Top - cardH, rightX0 + rightCardW, row1Top, 0.95f, 0.90f, 0.2f);
    appendMetricCard(hudVertices, leftX0,  row2Top - cardH, leftX0  + leftCardW,  row2Top, 0.95f, 0.45f, 0.15f);
    appendMetricCard(hudVertices, rightX0, row2Top - cardH, rightX0 + rightCardW, row2Top, 0.55f, 1.0f,  0.4f);

    float lp       = 0.010f;   // left inner padding
    float lblPixel = 0.0026f;  // bitmap glyph size (~0.018 tall)

    // --- Labels: pinned just below the top accent bar of each card ---
    float lblOff = 0.020f;   // how far down from rowTop the label sits
    appendBitmapText(hudVertices, "HEIGHT", leftX0  + lp, row0Top - lblOff, lblPixel, 0.92f, 0.96f, 1.0f, 0.98f);
    appendBitmapText(hudVertices, "SPEED",  rightX0 + lp, row0Top - lblOff, lblPixel, 0.92f, 0.96f, 1.0f, 0.98f);
    appendBitmapText(hudVertices, "DIST",   leftX0  + lp, row1Top - lblOff, lblPixel, 0.92f, 0.96f, 1.0f, 0.98f);
    appendBitmapText(hudVertices, "VX",     rightX0 + lp, row1Top - lblOff, lblPixel, 0.92f, 0.96f, 1.0f, 0.98f);
    appendBitmapText(hudVertices, "VY",     leftX0  + lp, row2Top - lblOff, lblPixel, 0.92f, 0.96f, 1.0f, 0.98f);
    appendBitmapText(hudVertices, "ANGLE",  rightX0 + lp, row2Top - lblOff, lblPixel, 0.92f, 0.96f, 1.0f, 0.98f);

    // --- Values: 7-segment digits pinned near the bottom of each card ---
    char valueText[32];
    float digitW = 0.012f;
    float digitH = 0.030f;
    float digitT = 0.0034f;
    float gap    = 0.0024f;
    float rp     = 0.010f; // right padding for value alignment
    float leftRightX  = leftX0 + leftCardW - rp;
    float rightRightX = rightX0 + rightCardW - rp;

    // Value Y: bottom of digit block sits ~0.014 above the card bottom edge
    // card bottom = rowTop - cardH;  value bottom = cardBottom + 0.014
    float valOff = 0.014f;  // from card bottom upward
    float valY0  = (row0Top - cardH) + valOff;
    float valY1  = (row1Top - cardH) + valOff;
    float valY2  = (row2Top - cardH) + valOff;

    std::snprintf(valueText, sizeof(valueText), "%.2f", telemetryHeightCovered);
    appendSegmentText(hudVertices, valueText, leftRightX - getSegmentTextWidth(valueText, digitW, gap), valY0, digitW, digitH, digitT, gap, 0.35f, 0.8f,  1.0f,  1.0f);
    std::snprintf(valueText, sizeof(valueText), "%.2f", telemetrySpeed);
    appendSegmentText(hudVertices, valueText, rightRightX - getSegmentTextWidth(valueText, digitW, gap), valY0, digitW, digitH, digitT, gap, 1.0f,  0.56f, 0.25f, 1.0f);

    std::snprintf(valueText, sizeof(valueText), "%.2f", telemetryHorizontalDistance);
    appendSegmentText(hudVertices, valueText, leftRightX - getSegmentTextWidth(valueText, digitW, gap), valY1, digitW, digitH, digitT, gap, 0.65f, 1.0f,  0.55f, 1.0f);
    std::snprintf(valueText, sizeof(valueText), "%+0.2f", velocityX);
    appendSegmentText(hudVertices, valueText, rightRightX - getSegmentTextWidth(valueText, digitW, gap), valY1, digitW, digitH, digitT, gap, 0.95f, 0.9f,  0.2f,  1.0f);

    std::snprintf(valueText, sizeof(valueText), "%+0.2f", velocityY);
    appendSegmentText(hudVertices, valueText, leftRightX - getSegmentTextWidth(valueText, digitW, gap), valY2, digitW, digitH, digitT, gap, 0.95f, 0.45f, 0.15f, 1.0f);
    std::snprintf(valueText, sizeof(valueText), "%.1f", telemetryFlightAngleDeg);
    appendSegmentText(hudVertices, valueText, rightRightX - getSegmentTextWidth(valueText, digitW, gap), valY2, digitW, digitH, digitT, gap, 0.55f, 1.0f,  0.4f,  1.0f);

    int cameraPresetUi = cameraPresetIndex + 1;
    appendMetricCard(hudVertices, panelLeft + 0.36f, -0.995f, panelRight - 0.02f, -0.88f, 0.55f, 1.0f, 0.4f); // Larger preset box
    appendBitmapText(hudVertices, "FUEL", panelLeft + 0.38f, -0.90f, 0.0026f, 0.86f, 0.95f, 0.86f, 0.98f);
    appendBitmapText(hudVertices, "TIME", panelLeft + 0.52f, -0.90f, 0.0026f, 0.86f, 0.95f, 0.86f, 0.98f);
    appendBitmapText(hudVertices, "PRESET", panelLeft + 0.65f, -0.90f, 0.0026f, 0.86f, 0.95f, 0.86f, 0.98f);

    std::snprintf(valueText, sizeof(valueText), "%5.2f", fuel);
    appendSegmentText(hudVertices, valueText, panelLeft + 0.380f, -0.980f, 0.010f, 0.028f, 0.0025f, 0.0020f, 0.55f, 1.0f, 0.4f, 1.0f);
    std::snprintf(valueText, sizeof(valueText), "%5.2f", flightTime);
    appendSegmentText(hudVertices, valueText, panelLeft + 0.520f, -0.980f, 0.010f, 0.028f, 0.0025f, 0.0020f, 0.55f, 1.0f, 0.4f, 1.0f);
    std::snprintf(valueText, sizeof(valueText), "%d", cameraPresetUi);
    appendSegmentText(hudVertices, valueText, panelLeft + 0.680f, -0.980f, 0.010f, 0.028f, 0.0025f, 0.0020f, 0.55f, 1.0f, 0.4f, 1.0f);

    if (!cinematicCamera) {
        appendQuad(hudVertices, panelRight - 0.26f, panelTop - 0.055f, panelRight - 0.08f, panelTop - 0.02f, 0.2f, 0.2f, 0.24f, 0.95f);
        appendBitmapText(hudVertices, "CLASSIC", panelRight - 0.245f, panelTop - 0.030f, 0.0028f, 0.88f, 0.88f, 0.92f, 0.95f);
    } else {
        appendQuad(hudVertices, panelRight - 0.26f, panelTop - 0.055f, panelRight - 0.08f, panelTop - 0.02f, 0.07f, 0.24f, 0.31f, 0.95f);
        appendBitmapText(hudVertices, "CINEMA", panelRight - 0.245f, panelTop - 0.030f, 0.0028f, 0.60f, 0.95f, 1.0f, 0.95f);
    }

    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
    glBufferData(GL_ARRAY_BUFFER, hudVertices.size() * sizeof(Vertex), hudVertices.data(), GL_STREAM_DRAW);
}

void updateWindowTitle(GLFWwindow* window) {
    char title[200];
    float displayedHeight = std::max(0.0f, telemetryHeightCovered);
    const char* cameraMode = cinematicCamera ? "Cine" : "Classic";
    std::snprintf(
        title,
        sizeof(title),
        "Rocket Sim | Height: %.2f | Speed: %.2f | Vx: %+0.2f | Vy: %+0.2f | Angle: %.1f deg | Cam: %s P%d",
        displayedHeight,
        telemetrySpeed,
        velocityX,
        velocityY,
        telemetryFlightAngleDeg,
        cameraMode,
        cameraPresetIndex + 1
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
    launchStartX = 0.0f;
    launchStartY = groundY;
    telemetryHeightCovered = 0.0f;
    telemetryMaxHeightCovered = 0.0f;
    telemetryHorizontalDistance = 0.0f;
    telemetrySpeed = 0.0f;
    telemetryFlightAngleDeg = 90.0f;
    flightTime = 0.0f;
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
    glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
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
        flightTime += dt;
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

    if (!launching && !exploded) {
        launchStartX = rocketX;
        launchStartY = rocketY;
        telemetryHeightCovered = 0.0f;
        telemetryHorizontalDistance = 0.0f;
        telemetrySpeed = 0.0f;
        telemetryFlightAngleDeg = launchAngleDeg;
    }

    telemetryHeightCovered = std::max(0.0f, rocketY - launchStartY);
    telemetryMaxHeightCovered = std::max(telemetryMaxHeightCovered, telemetryHeightCovered);
    telemetryHorizontalDistance = std::fabs(rocketX - launchStartX);
    telemetrySpeed = std::sqrt(velocityX * velocityX + velocityY * velocityY);
    if (telemetrySpeed > 0.0001f) {
        telemetryFlightAngleDeg = std::atan2(velocityY, velocityX) * 180.0f / PI;
    } else {
        telemetryFlightAngleDeg = launchAngleDeg;
    }

    float desiredWorldX = worldX;
    float desiredWorldY = worldY;
    float screenX = rocketX + worldX;
    float screenY = rocketY + worldY;

    if (cinematicCamera) {
        const CameraPreset& preset = CAMERA_PRESETS[cameraPresetIndex];
        float targetScreenX = -0.20f + clampf(velocityX * preset.lookAhead, -0.25f, 0.25f);
        float targetScreenY = -0.02f + preset.verticalBias + clampf(velocityY * preset.lookAhead * 0.6f, -0.18f, 0.18f);

        if (screenX > targetScreenX + preset.deadZoneX) desiredWorldX -= (screenX - (targetScreenX + preset.deadZoneX));
        if (screenX < targetScreenX - preset.deadZoneX) desiredWorldX -= (screenX - (targetScreenX - preset.deadZoneX));
        if (screenY > targetScreenY + preset.deadZoneY) desiredWorldY -= (screenY - (targetScreenY + preset.deadZoneY));
        if (screenY < targetScreenY - preset.deadZoneY) desiredWorldY -= (screenY - (targetScreenY - preset.deadZoneY));
    } else {
        if (screenX > 0.62f) desiredWorldX -= (screenX - 0.62f);
        if (screenX < -0.62f) desiredWorldX -= (screenX + 0.62f);
        if (screenY > 0.55f) desiredWorldY -= (screenY - 0.55f);
        if (screenY < -0.40f) desiredWorldY -= (screenY + 0.40f);
    }

    float follow = clampf((cinematicCamera ? CAMERA_PRESETS[cameraPresetIndex].follow : 5.5f) * dt, 0.0f, 1.0f);
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
        launchStartX = rocketX;
        launchStartY = rocketY;
        flightTime = 0.0f;
        telemetryMaxHeightCovered = 0.0f;
        float angle = toRadians(launchAngleDeg);
        velocityX = std::cos(angle) * 0.30f;
        velocityY = std::sin(angle) * 0.30f;
    }
    if (key == GLFW_KEY_P && action == GLFW_PRESS) paused = !paused;
    if (key == GLFW_KEY_R && action == GLFW_PRESS) resetRocketState();
    if (key == GLFW_KEY_C && action == GLFW_PRESS) cinematicCamera = !cinematicCamera;
    if (key == GLFW_KEY_V && action == GLFW_PRESS) cameraPresetIndex = (cameraPresetIndex + 1) % 3;
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) cameraPresetIndex = 0;
    if (key == GLFW_KEY_2 && action == GLFW_PRESS) cameraPresetIndex = 1;
    if (key == GLFW_KEY_3 && action == GLFW_PRESS) cameraPresetIndex = 2;
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
              << "Hold Left Shift = Temporary boost\n"
              << "C = Toggle cinematic camera | V = Next preset | 1/2/3 = Camera preset\n";

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
