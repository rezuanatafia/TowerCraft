/*
 =====================================================================
  TOWERCRAFT  —  A Tower-Stacking Game built with GLUT / OpenGL
 =====================================================================
  Features:
    - Moving cube block (alternates X / Z axis each floor)
    - "Storybook cottage" look: Color Hunt pastel body palette (peach /
      sand / cream / sky blue), arched windows with warm lit-glass fill,
      an arched front door on the ground floor, and a pitched roof +
      chimney on whichever floor is currently on top -- inspired by flat
      vector house illustrations
    - Linear movement -- classic constant-speed slide, bounces at edges
    - Space / Left-Click to drop the floor and stack it
    - No trimming: every block keeps its full size and lands wherever it
      was when dropped. A completely missed drop (no overlap at all with
      the block below) is still game over.
    - Balancing / stability system: sloppy landings destabilize the
      tower, rendered via a spring-damper Wobble simulation that gets
      kicked by each landing's offset and oscillates/settles, so the
      tower visibly sways like an inverted pendulum.
      The underlying towerLean meter decides WHEN a collapse happens.
      If the lean meter maxes out, the top few floors topple off
      (extra penalty) and play continues on the shorter, still-standing
      tower.
    - Scoring: every placed block is worth 1 point no matter how sloppy the
      landing; a perfect (dead-center) landing is worth 5, and consecutive
      perfect landings multiply that like a combo (streak of 3 = 15, etc.)
    - "Perfect!" popup plus a separate "COMBO xN!" popup for streaks of 2+
    - A full collapse (balance meter maxes out) costs a flat 7 points
    - Falling animation for collapse debris
    - Score tracking + increasing difficulty (speed grows with height)
    - Smooth camera that follows the tower as it grows
    - Game-over detection (fully missed block) + Restart system (R key)

  HOW TO BUILD IN CODE::BLOCKS:
    1. Create a new "GLUT" or empty Console/Win32 project.
    2. Add this file as main.cpp
    3. Project -> Build options -> Linker settings -> Add libraries:
          On Windows (MinGW):  freeglut  opengl32   glu32
          On Linux:             glut      GL         GLU
    4. Make sure "freeglut.dll" (Windows) is beside the .exe, or use
       the static freeglut lib.
    5. Build and Run.

  CONTROLS:
    SPACE / Left Mouse Click  -> Drop the current floor
    R                         -> Restart the game
    ESC                       -> Quit
 =====================================================================
*/

#include <GL/glut.h>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------
//  CONSTANTS
// ---------------------------------------------------------------
const float BLOCK_H     = 3.0f;   // height of every floor (== BASE_SIZE, so blocks are cubes)
const float BASE_SIZE   = 3.0f;   // width/depth of every block (cube edge length)
const float MOVE_RANGE  = 6.0f;   // how far a floor slides / swings left-right
const float GRAVITY     = 0.035f; // for falling debris pieces
const float PI_F        = 3.14159265f;

int windowWidth  = 900;
int windowHeight = 650;

// ---------------------------------------------------------------
//  DATA STRUCTURES
// ---------------------------------------------------------------
struct Block {
    float x, y, z;   // position (y = bottom of the block)
    float w, h, d;   // dimensions
    float r, g, b;   // body color
    float roofR, roofG, roofB; // roof color -- used only while this floor is on top
    float doorR, doorG, doorB; // door color -- used only for the ground floor (index 0)
};

struct FallingPiece {
    float x, y, z;
    float w, h, d;
    float r, g, b;
    float vy, vx, vz; // vx/vz give collapse debris an outward "toppling" drift
    float rot;
    float vrot;
};

enum GameState { PLAYING, GAME_OVER };

// ---------------------------------------------------------------
//  GLOBAL STATE
// ---------------------------------------------------------------
std::vector<Block> tower;
std::vector<FallingPiece> fallingPieces;

Block currentBlock;
int   moveAxis   = 0;     // 0 = moves along X, 1 = moves along Z
float moveDir    = 1.0f;
float moveSpeed  = 0.08f;
const float moveSpeedBase = 0.08f;

GameState state = PLAYING;
int   score = 0;
int   bestScore = 0;

float camY = 6.0f, camYTarget = 6.0f;
float camAngle = 0.35f; // slow orbit for eye-candy

// --- "Perfect!" landing bonus popup ---
const float PERFECT_TOLERANCE      = 0.15f; // how close to dead-center counts as perfect
const float PERFECT_POPUP_DURATION = 2.0f;  // seconds the popup stays on screen
const int   PERFECT_BASE_SCORE     = 5;     // total points a single (non-combo) perfect landing is worth
bool  perfectPopupActive = false;
float perfectPopupTimer  = 0.0f;
int   lastPerfectPoints  = 0;      // actual points earned by the most recent perfect (scales with combo)

// --- Combo streak: consecutive perfect landings multiply the perfect score ---
int   comboStreak       = 0;
bool  comboPopupActive  = false;
float comboPopupTimer   = 0.0f;
const float COMBO_POPUP_DURATION = 1.6f;

// --- Balancing / stability system ---
const float LEAN_MAX               = 1.0f;  // stability meter cap -- reaching this collapses the tower
const float LEAN_GAIN_SCALE        = 0.55f; // how much a sloppy landing adds to lean
const float LEAN_RECOVERY_SCALE    = 0.18f; // how much a clean landing settles the tower back down
const int   MAX_MISS_PENALTY       = 5;     // worst-case points lost on a still-surviving bad landing
const int   COLLAPSE_PENALTY       = 4;     // base points lost when the tower topples
const int   COLLAPSE_BLOCK_COUNT   = 3;     // how many top floors topple off in a collapse
const float COLLAPSE_LEAN_RESET    = 0.35f; // meter doesn't fully reset -- tower stays a bit shaky
const float COLLAPSE_POPUP_DURATION = 2.0f;

float towerLean = 0.0f;
bool  collapsePopupActive = false;
float collapsePopupTimer  = 0.0f;
int   lastCollapsePenalty = 0;

// Spring-damper wobble -- the tower's balance is always visualized this way
float wobbleAngleX = 0.0f, wobbleVelX = 0.0f;
float wobbleAngleZ = 0.0f, wobbleVelZ = 0.0f;
const float WOBBLE_IMPULSE_SCALE = 0.13f;
const float WOBBLE_STIFFNESS     = 8.5f;
const float WOBBLE_DAMPING       = 2.4f;
const float WOBBLE_DEG_SCALE     = 78.0f; // radians -> degrees for rendering

int lastUpdateTimeMs = 0; // used to compute real elapsed time between frames

// ---------------------------------------------------------------
//  CURATED PALETTE  (Color Hunt: ffbe91 / ffddb0 / fffce1 / cfebff,
//  cycling per floor)
// ---------------------------------------------------------------
const int PALETTE_SIZE = 4;

const float paletteBody[PALETTE_SIZE][3] = {
    {1.00f, 0.745f, 0.569f}, // #ffbe91 -- peach
    {1.00f, 0.867f, 0.690f}, // #ffddb0 -- warm sand
    {1.00f, 0.988f, 0.882f}, // #fffce1 -- pale cream
    {0.812f, 0.922f, 1.00f}, // #cfebff -- soft sky blue
};

const float paletteRoof[PALETTE_SIZE][3] = {
    {0.55f, 0.30f, 0.22f}, // terracotta roof (for the peach floor)
    {0.50f, 0.34f, 0.20f}, // caramel brown (for the sand floor)
    {0.45f, 0.40f, 0.32f}, // warm taupe (for the cream floor)
    {0.28f, 0.34f, 0.42f}, // slate blue (for the sky-blue floor)
};

const float paletteDoor[PALETTE_SIZE][3] = {
    {0.42f, 0.24f, 0.20f}, // maroon-brown door
    {0.40f, 0.30f, 0.22f}, // brown door
    {0.34f, 0.30f, 0.26f}, // dark brown door
    {0.28f, 0.34f, 0.36f}, // slate blue door
};

void colorForLevel(int level, float &r, float &g, float &b) {
    int i = ((level % PALETTE_SIZE) + PALETTE_SIZE) % PALETTE_SIZE;
    r = paletteBody[i][0]; g = paletteBody[i][1]; b = paletteBody[i][2];
}

void roofColorForLevel(int level, float &r, float &g, float &b) {
    int i = ((level % PALETTE_SIZE) + PALETTE_SIZE) % PALETTE_SIZE;
    r = paletteRoof[i][0]; g = paletteRoof[i][1]; b = paletteRoof[i][2];
}

void doorColorForLevel(int level, float &r, float &g, float &b) {
    int i = ((level % PALETTE_SIZE) + PALETTE_SIZE) % PALETTE_SIZE;
    r = paletteDoor[i][0]; g = paletteDoor[i][1]; b = paletteDoor[i][2];
}

// ---------------------------------------------------------------
//  DRAWING HELPERS
// ---------------------------------------------------------------
void drawBox(float cx, float baseY, float cz, float w, float h, float d,
             float r, float g, float b) {
    float x0 = cx - w/2, x1 = cx + w/2;
    float y0 = baseY,    y1 = baseY + h;
    float z0 = cz - d/2, z1 = cz + d/2;

    glColor3f(r, g, b);
    glBegin(GL_QUADS);
        // top
        glNormal3f(0, 1, 0);
        glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
        glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
        // bottom
        glNormal3f(0, -1, 0);
        glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1);
        glVertex3f(x1, y0, z0); glVertex3f(x0, y0, z0);
        // front (+z)
        glNormal3f(0, 0, 1);
        glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1);
        glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
        // back (-z)
        glNormal3f(0, 0, -1);
        glVertex3f(x1, y0, z0); glVertex3f(x0, y0, z0);
        glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
        // right (+x)
        glNormal3f(1, 0, 0);
        glVertex3f(x1, y0, z1); glVertex3f(x1, y0, z0);
        glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
        // left (-x)
        glNormal3f(-1, 0, 0);
        glVertex3f(x0, y0, z0); glVertex3f(x0, y0, z1);
        glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
    glEnd();

    // soft dark outline -- crisp edge, matches the flat-illustration look
    glDisable(GL_LIGHTING);
    glColor3f(0.08f, 0.1f, 0.09f);
    glLineWidth(0.4f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
        glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    glEnd();
    glBegin(GL_LINE_LOOP);
        glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
        glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
    glEnd();
    glBegin(GL_LINES);
        glVertex3f(x0,y0,z0); glVertex3f(x0,y1,z0);
        glVertex3f(x1,y0,z0); glVertex3f(x1,y1,z0);
        glVertex3f(x1,y0,z1); glVertex3f(x1,y1,z1);
        glVertex3f(x0,y0,z1); glVertex3f(x0,y1,z1);
    glEnd();
    glEnable(GL_LIGHTING);
}

// ---- arched window on a face lying in the XY plane (front/back, fixed Z) ----
// ww = full width (also used as the arch diameter), wh = full height including the arch cap.
void drawArchedWindowXY(float cx, float cyMid, float z, float ww, float wh,
                          float frameR, float frameG, float frameB) {
    float archR   = ww * 0.5f;
    float rectH   = std::max(0.05f, wh - archR);
    float y0      = cyMid - wh * 0.5f;
    float archY   = y0 + rectH; // height where the rectangle meets the arch

    glDisable(GL_LIGHTING);

    // warm "lit glass" fill
    glColor3f(0.96f, 0.85f, 0.55f);
    glBegin(GL_QUADS);
        glVertex3f(cx-archR, y0,   z); glVertex3f(cx+archR, y0,   z);
        glVertex3f(cx+archR, archY, z); glVertex3f(cx-archR, archY, z);
    glEnd();
    const int segs = 10;
    glBegin(GL_TRIANGLE_FAN);
        glVertex3f(cx, archY, z);
        for (int i = 0; i <= segs; i++) {
            float t = PI_F * (float)i / segs;
            glVertex3f(cx + archR*cosf(t), archY + archR*sinf(t), z);
        }
    glEnd();

    // frame outline
    glColor3f(frameR, frameG, frameB);
    glLineWidth(1.4f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(cx-archR, y0, z);
        glVertex3f(cx+archR, y0, z);
        for (int i = 0; i <= segs; i++) {
            float t = PI_F * (float)i / segs;
            glVertex3f(cx + archR*cosf(t), archY + archR*sinf(t), z);
        }
    glEnd();
    // cross-bars
    glBegin(GL_LINES);
        glVertex3f(cx, y0, z); glVertex3f(cx, archY + archR, z);
        glVertex3f(cx-archR, (y0+archY)*0.5f, z); glVertex3f(cx+archR, (y0+archY)*0.5f, z);
    glEnd();

    glEnable(GL_LIGHTING);
}

// ---- arched window on a face lying in the ZY plane (left/right, fixed X) ----
void drawArchedWindowZY(float x, float cyMid, float cz, float wd, float wh,
                          float frameR, float frameG, float frameB) {
    float archR = wd * 0.5f;
    float rectH = std::max(0.05f, wh - archR);
    float y0    = cyMid - wh * 0.5f;
    float archY = y0 + rectH;

    glDisable(GL_LIGHTING);

    glColor3f(0.96f, 0.85f, 0.55f);
    glBegin(GL_QUADS);
        glVertex3f(x, y0,   cz-archR); glVertex3f(x, y0,   cz+archR);
        glVertex3f(x, archY, cz+archR); glVertex3f(x, archY, cz-archR);
    glEnd();
    const int segs = 10;
    glBegin(GL_TRIANGLE_FAN);
        glVertex3f(x, archY, cz);
        for (int i = 0; i <= segs; i++) {
            float t = PI_F * (float)i / segs;
            glVertex3f(x, archY + archR*sinf(t), cz + archR*cosf(t));
        }
    glEnd();

    glColor3f(frameR, frameG, frameB);
    glLineWidth(1.4f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(x, y0, cz-archR);
        glVertex3f(x, y0, cz+archR);
        for (int i = 0; i <= segs; i++) {
            float t = PI_F * (float)i / segs;
            glVertex3f(x, archY + archR*sinf(t), cz + archR*cosf(t));
        }
    glEnd();
    glBegin(GL_LINES);
        glVertex3f(x, y0, cz); glVertex3f(x, archY + archR, cz);
        glVertex3f(x, (y0+archY)*0.5f, cz-archR); glVertex3f(x, (y0+archY)*0.5f, cz+archR);
    glEnd();

    glEnable(GL_LIGHTING);
}

// Adds arched windows on all 4 vertical faces of a block, turning it into a "floor"
void drawFacadeDetails(float cx, float baseY, float cz, float w, float h, float d,
                        float bodyR, float bodyG, float bodyB) {
    float x0 = cx - w/2, x1 = cx + w/2;
    float z0 = cz - d/2, z1 = cz + d/2;
    float cyMid = baseY + h*0.52f;

    // frame color: a darker tint of the block's own body color
    float frameR = bodyR * 0.32f, frameG = bodyG * 0.32f, frameB = bodyB * 0.32f;

    int colsX = std::max(1, (int)(w / 1.3f));
    float ww = std::min(0.6f, (w/colsX) * 0.5f);
    float wh = std::min(0.85f, h * 0.6f);
    for (int i = 0; i < colsX; i++) {
        float cxw = x0 + (i + 0.5f) * (w / colsX);
        drawArchedWindowXY(cxw, cyMid, z1 + 0.02f, ww, wh, frameR, frameG, frameB);
        drawArchedWindowXY(cxw, cyMid, z0 - 0.02f, ww, wh, frameR, frameG, frameB);
    }

    int colsZ = std::max(1, (int)(d / 1.3f));
    float wd = std::min(0.6f, (d/colsZ) * 0.5f);
    for (int i = 0; i < colsZ; i++) {
        float czw = z0 + (i + 0.5f) * (d / colsZ);
        drawArchedWindowZY(x1 + 0.02f, cyMid, czw, wd, wh, frameR, frameG, frameB);
        drawArchedWindowZY(x0 - 0.02f, cyMid, czw, wd, wh, frameR, frameG, frameB);
    }
}

// Arched front door, drawn on the ground floor only
void drawArchedDoor(float cx, float baseY, float cz, float w, float h, float d,
                     float doorR, float doorG, float doorB) {
    float doorW = std::min(0.8f, w * 0.24f);
    float doorH = std::min(1.2f, h * 0.72f);
    float z = cz + d/2 + 0.03f;

    float archR = doorW * 0.5f;
    float rectH = std::max(0.05f, doorH - archR);
    float y0    = baseY;
    float archY = y0 + rectH;

    glDisable(GL_LIGHTING);

    glColor3f(doorR, doorG, doorB);
    glBegin(GL_QUADS);
        glVertex3f(cx-archR, y0,   z); glVertex3f(cx+archR, y0,   z);
        glVertex3f(cx+archR, archY, z); glVertex3f(cx-archR, archY, z);
    glEnd();
    const int segs = 10;
    glBegin(GL_TRIANGLE_FAN);
        glVertex3f(cx, archY, z);
        for (int i = 0; i <= segs; i++) {
            float t = PI_F * (float)i / segs;
            glVertex3f(cx + archR*cosf(t), archY + archR*sinf(t), z);
        }
    glEnd();

    glColor3f(0.12f, 0.08f, 0.06f);
    glLineWidth(1.6f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(cx-archR, y0, z);
        glVertex3f(cx+archR, y0, z);
        for (int i = 0; i <= segs; i++) {
            float t = PI_F * (float)i / segs;
            glVertex3f(cx + archR*cosf(t), archY + archR*sinf(t), z);
        }
    glEnd();

    glColor3f(0.85f, 0.75f, 0.25f); // doorknob
    glPointSize(4.0f);
    glBegin(GL_POINTS);
        glVertex3f(cx + archR*0.55f, y0 + rectH*0.5f, z+0.01f);
    glEnd();

    glEnable(GL_LIGHTING);
}

// Pitched roof + little chimney, drawn on whichever block is currently on top
void drawRoof(float cx, float topY, float cz, float w, float d,
              float roofR, float roofG, float roofB) {
    float x0 = cx - w/2, x1 = cx + w/2;
    float z0 = cz - d/2, z1 = cz + d/2;
    float apexY = topY + std::min(w, d) * 0.45f + 0.3f;

    glColor3f(roofR, roofG, roofB);
    glBegin(GL_TRIANGLES);
        // front slope
        glNormal3f(0, 0.6f, 0.8f);
        glVertex3f(x0, topY, z1); glVertex3f(x1, topY, z1); glVertex3f(cx, apexY, cz);
        // back slope
        glNormal3f(0, 0.6f, -0.8f);
        glVertex3f(x1, topY, z0); glVertex3f(x0, topY, z0); glVertex3f(cx, apexY, cz);
    glEnd();
    glBegin(GL_QUADS);
        // left slope
        glNormal3f(-0.8f, 0.6f, 0);
        glVertex3f(x0, topY, z0); glVertex3f(x0, topY, z1);
        glVertex3f(cx, apexY, cz); glVertex3f(cx, apexY, cz);
        // right slope
        glNormal3f(0.8f, 0.6f, 0);
        glVertex3f(x1, topY, z1); glVertex3f(x1, topY, z0);
        glVertex3f(cx, apexY, cz); glVertex3f(cx, apexY, cz);
    glEnd();

    // little chimney -- a touch darker than the roof itself
    drawBox(cx + w*0.22f, topY + std::min(w,d)*0.22f, cz - d*0.15f,
            0.35f, 0.55f, 0.35f, roofR*0.75f, roofG*0.75f, roofB*0.75f);


}

void drawText(float x, float y, const char* text, void* font = GLUT_BITMAP_HELVETICA_18) {
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; c++)
        glutBitmapCharacter(font, *c);
}

void drawGround() {
    glDisable(GL_LIGHTING);
    glColor3f(0.16f, 0.18f, 0.24f);
    float size = 60.0f;
    glBegin(GL_QUADS);
        glVertex3f(-size, -0.02f, -size);
        glVertex3f( size, -0.02f, -size);
        glVertex3f( size, -0.02f,  size);
        glVertex3f(-size, -0.02f,  size);
    glEnd();

    glColor3f(0.26f, 0.29f, 0.36f);
    glBegin(GL_LINES);
    for (float i = -size; i <= size; i += 2.0f) {
        glVertex3f(i, -0.01f, -size); glVertex3f(i, -0.01f, size);
        glVertex3f(-size, -0.01f, i); glVertex3f(size, -0.01f, i);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

// ---------------------------------------------------------------
//  GAME LOGIC
// ---------------------------------------------------------------
void spawnNext() {
    const Block &top = tower.back();
    currentBlock.w = top.w;
    currentBlock.d = top.d;
    currentBlock.h = BLOCK_H;
    currentBlock.y = top.y + BLOCK_H;

    moveAxis = (int)(tower.size()) % 2;   // alternate direction each floor
    int level = (int)tower.size();
    colorForLevel(level, currentBlock.r, currentBlock.g, currentBlock.b);
    roofColorForLevel(level, currentBlock.roofR, currentBlock.roofG, currentBlock.roofB);
    doorColorForLevel(level, currentBlock.doorR, currentBlock.doorG, currentBlock.doorB);

    if (moveAxis == 0) {
        currentBlock.x = -MOVE_RANGE;
        currentBlock.z = top.z;
    } else {
        currentBlock.z = -MOVE_RANGE;
        currentBlock.x = top.x;
    }
    moveDir = 1.0f;
}

void resetGame() {
    tower.clear();
    fallingPieces.clear();
    score = 0;
    moveSpeed = moveSpeedBase;
    state = PLAYING;
    perfectPopupActive = false;
    perfectPopupTimer  = 0.0f;
    lastPerfectPoints  = 0;
    comboStreak        = 0;
    comboPopupActive   = false;
    comboPopupTimer    = 0.0f;
    towerLean = 0.0f;
    collapsePopupActive = false;
    collapsePopupTimer  = 0.0f;
    lastCollapsePenalty = 0;

    wobbleAngleX = wobbleVelX = wobbleAngleZ = wobbleVelZ = 0.0f;

    Block base;
    base.x = 0; base.y = 0; base.z = 0;
    base.w = BASE_SIZE; base.h = BLOCK_H; base.d = BASE_SIZE;
    colorForLevel(0, base.r, base.g, base.b);
    roofColorForLevel(0, base.roofR, base.roofG, base.roofB);
    doorColorForLevel(0, base.doorR, base.doorG, base.doorB);
    tower.push_back(base);

    camY = 6.0f; camYTarget = 6.0f;
    spawnNext();
}

void addFallingPiece(float x, float y, float z, float w, float h, float d,
                      float r, float g, float b, float vx = 0.0f, float vz = 0.0f) {
    if (w <= 0.001f || d <= 0.001f) return;
    FallingPiece fp;
    fp.x = x; fp.y = y; fp.z = z;
    fp.w = w; fp.h = h; fp.d = d;
    fp.r = r; fp.g = g; fp.b = b;
    fp.vy = 0.02f;
    fp.vx = vx; fp.vz = vz;
    fp.rot = 0.0f;
    fp.vrot = (rand() % 200 - 100) / 15.0f;
    fallingPieces.push_back(fp);
}

void triggerGameOver() {
    state = GAME_OVER;
    // send the whole missed block tumbling down for visual feedback
    addFallingPiece(currentBlock.x, currentBlock.y, currentBlock.z,
                     currentBlock.w, currentBlock.h, currentBlock.d,
                     currentBlock.r, currentBlock.g, currentBlock.b);
    if (score > bestScore) bestScore = score;
}

// The tower has leaned too far -- topple the top few floors off, take a
// score hit, and keep playing on the shorter (but standing) tower.
void collapseTower() {
    int blocksToDrop = std::min(COLLAPSE_BLOCK_COUNT, (int)tower.size() - 1); // base always survives
    if (blocksToDrop < 1) { towerLean = COLLAPSE_LEAN_RESET; return; }

    for (int i = 0; i < blocksToDrop; i++) {
        Block b = tower.back();
        tower.pop_back();
        float vx = ((rand() % 200) - 100) / 60.0f; // outward drift so it reads as "toppling"
        float vz = ((rand() % 200) - 100) / 60.0f;
        addFallingPiece(b.x, b.y, b.z, b.w, b.h, b.d, b.r, b.g, b.b, vx, vz);
    }

    int penalty = COLLAPSE_PENALTY + blocksToDrop;
    score = std::max(0, score - penalty);
    lastCollapsePenalty = penalty;

    towerLean = COLLAPSE_LEAN_RESET; // shaky, not stable -- another sloppy drop could collapse again

    // Bleed off some of the wobble too, so the render matches the
    // "shaky but standing" state the meter resets to.
    wobbleAngleX *= 0.4f; wobbleVelX *= 0.4f;
    wobbleAngleZ *= 0.4f; wobbleVelZ *= 0.4f;

    collapsePopupActive = true;
    collapsePopupTimer  = COLLAPSE_POPUP_DURATION;
}

void dropBlock() {
    if (state != PLAYING) return;

    const Block &top = tower.back();
    bool alongX = (moveAxis == 0);

    float curLo, curHi, topLo, topHi, fullSize;
    if (alongX) {
        curLo = currentBlock.x - currentBlock.w/2; curHi = currentBlock.x + currentBlock.w/2;
        topLo = top.x - top.w/2;                   topHi = top.x + top.w/2;
        fullSize = currentBlock.w;
    } else {
        curLo = currentBlock.z - currentBlock.d/2; curHi = currentBlock.z + currentBlock.d/2;
        topLo = top.z - top.d/2;                   topHi = top.z + top.d/2;
        fullSize = currentBlock.d;
    }

    float overlapLo = std::max(curLo, topLo);
    float overlapHi = std::min(curHi, topHi);
    float overlapAmount = overlapHi - overlapLo;

    if (overlapAmount <= 0.0f) {
        // Missed the tower entirely -- there's nothing left to balance on.
        triggerGameOver();
        return;
    }

    // How centered was it? (checked before trimming moves the block)
    float centerDiff = alongX ? std::fabs(currentBlock.x - top.x)
                               : std::fabs(currentBlock.z - top.z);
    float signedDiff = alongX ? (currentBlock.x - top.x)
                               : (currentBlock.z - top.z);
    bool perfectLanding = centerDiff <= PERFECT_TOLERANCE;

    // No more trimming -- the block keeps its full size and lands wherever
    // it was when dropped. Balance/scoring now scale with how far off
    // center the landing was, rather than how much got cut away.
    float missFrac = std::min(1.0f, centerDiff / fullSize); // 0 = dead center, 1 = about to miss entirely

    int scoreBeforeDrop = score;

    tower.push_back(currentBlock);
    score++; // every successful placement is worth at least 1 point, no matter how sloppy

    if (perfectLanding) {
        comboStreak++;
        int totalForThisDrop = PERFECT_BASE_SCORE * comboStreak; // combo multiplies the perfect score
        score += (totalForThisDrop - 1); // the base point above already covers 1 of these
        lastPerfectPoints = totalForThisDrop;

        perfectPopupActive = true;
        perfectPopupTimer  = PERFECT_POPUP_DURATION;

        if (comboStreak >= 2) {
            comboPopupActive = true;
            comboPopupTimer  = COMBO_POPUP_DURATION;
        }
    } else {
        comboStreak = 0; // any non-perfect landing breaks the combo
        if (missFrac > 0.02f) {
            int penalty = (int)std::round(missFrac * MAX_MISS_PENALTY);
            score = std::max(0, score - penalty);
        }
    }

    // ---- balancing: sloppy landings destabilize the tower, clean ones settle it ----
    towerLean += missFrac * LEAN_GAIN_SCALE;
    towerLean -= (1.0f - missFrac) * LEAN_RECOVERY_SCALE;
    towerLean = std::max(0.0f, std::min(LEAN_MAX, towerLean));

    // --- WOBBLE: kick the spring in the direction of the bad landing ---
    if (alongX) wobbleVelX += signedDiff * WOBBLE_IMPULSE_SCALE;
    else        wobbleVelZ += signedDiff * WOBBLE_IMPULSE_SCALE;

    if (towerLean >= LEAN_MAX) {
        collapseTower();
    }
    score = std::max(score, scoreBeforeDrop + 1);

    moveSpeed += 0.0035f;   // gets harder as you climb
    spawnNext();
}

// ---------------------------------------------------------------
//  UPDATE / TIMER
// ---------------------------------------------------------------
void update(int value) {
    // Real elapsed time since the last frame (in seconds).
    int nowMs = glutGet(GLUT_ELAPSED_TIME);
    float dt = (lastUpdateTimeMs == 0) ? 0.016f : (nowMs - lastUpdateTimeMs) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f; // clamp so a big stall doesn't cause a huge jump
    lastUpdateTimeMs = nowMs;

    if (state == PLAYING) {
        if (moveAxis == 0) {
            currentBlock.x += moveDir * moveSpeed;
            if (currentBlock.x > MOVE_RANGE)  { currentBlock.x = MOVE_RANGE;  moveDir = -1; }
            if (currentBlock.x < -MOVE_RANGE) { currentBlock.x = -MOVE_RANGE; moveDir =  1; }
        } else {
            currentBlock.z += moveDir * moveSpeed;
            if (currentBlock.z > MOVE_RANGE)  { currentBlock.z = MOVE_RANGE;  moveDir = -1; }
            if (currentBlock.z < -MOVE_RANGE) { currentBlock.z = -MOVE_RANGE; moveDir =  1; }
        }
    }

    // Wobble spring-damper -- always simulated so it settles smoothly.
    wobbleVelX += (-WOBBLE_STIFFNESS * wobbleAngleX - WOBBLE_DAMPING * wobbleVelX) * dt;
    wobbleAngleX += wobbleVelX * dt;
    wobbleVelZ += (-WOBBLE_STIFFNESS * wobbleAngleZ - WOBBLE_DAMPING * wobbleVelZ) * dt;
    wobbleAngleZ += wobbleVelZ * dt;

    for (size_t i = 0; i < fallingPieces.size(); i++) {
        fallingPieces[i].vy += GRAVITY;
        fallingPieces[i].y  -= fallingPieces[i].vy;
        fallingPieces[i].x  += fallingPieces[i].vx;
        fallingPieces[i].z  += fallingPieces[i].vz;
        fallingPieces[i].rot += fallingPieces[i].vrot;
    }
    for (size_t i = 0; i < fallingPieces.size();) {
        if (fallingPieces[i].y < -25.0f) fallingPieces.erase(fallingPieces.begin()+i);
        else i++;
    }

    camYTarget = std::max(6.0f, (float)(tower.size()) * BLOCK_H);
    camY += (camYTarget - camY) * 0.07f;

    if (perfectPopupActive) {
        perfectPopupTimer -= dt;
        if (perfectPopupTimer <= 0.0f) {
            perfectPopupTimer = 0.0f;
            perfectPopupActive = false;
        }
    }

    if (comboPopupActive) {
        comboPopupTimer -= dt;
        if (comboPopupTimer <= 0.0f) {
            comboPopupTimer = 0.0f;
            comboPopupActive = false;
        }
    }

    if (collapsePopupActive) {
        collapsePopupTimer -= dt;
        if (collapsePopupTimer <= 0.0f) {
            collapsePopupTimer = 0.0f;
            collapsePopupActive = false;
        }
    }

    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

// ---------------------------------------------------------------
//  DISPLAY
// ---------------------------------------------------------------
void display() {
    glClearColor(0.53f, 0.72f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(55.0, (double)windowWidth / windowHeight, 0.1, 200.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float eyeX = 16.0f, eyeZ = 16.0f;
    gluLookAt(eyeX, camY + 8.0f, eyeZ,
              0.0f,  camY + 1.0f, 0.0f,
              0.0f,  1.0f,        0.0f);

    GLfloat lightPos[] = { 10.0f, 30.0f, 10.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    drawGround();

    // Tower + in-play block are rendered inside a rotation representing the
    // current instability (spring wobble). Debris in fallingPieces is drawn
    // OUTSIDE this rotation since it has already detached and shouldn't
    // keep moving with the tower.
    glPushMatrix();
    glRotatef(wobbleAngleZ * WOBBLE_DEG_SCALE, 1.0f, 0.0f, 0.0f);
    glRotatef(wobbleAngleX * WOBBLE_DEG_SCALE, 0.0f, 0.0f, 1.0f);

    for (size_t i = 0; i < tower.size(); i++) {
        Block &blk = tower[i];
        drawBox(blk.x, blk.y, blk.z, blk.w, blk.h, blk.d, blk.r, blk.g, blk.b);
        drawFacadeDetails(blk.x, blk.y, blk.z, blk.w, blk.h, blk.d, blk.r, blk.g, blk.b);
        if (i == 0) drawArchedDoor(blk.x, blk.y, blk.z, blk.w, blk.h, blk.d,
                                     blk.doorR, blk.doorG, blk.doorB); // ground floor entrance
    }

    if (state == PLAYING) {
        drawBox(currentBlock.x, currentBlock.y, currentBlock.z,
                currentBlock.w, currentBlock.h, currentBlock.d,
                currentBlock.r, currentBlock.g, currentBlock.b);
        drawFacadeDetails(currentBlock.x, currentBlock.y, currentBlock.z,
                           currentBlock.w, currentBlock.h, currentBlock.d,
                           currentBlock.r, currentBlock.g, currentBlock.b);
        // roof rides on the moving block while it's still in play
        drawRoof(currentBlock.x, currentBlock.y + currentBlock.h, currentBlock.z,
                 currentBlock.w, currentBlock.d,
                 currentBlock.roofR, currentBlock.roofG, currentBlock.roofB);
    } else {
        // once the game ends, the roof settles on the last placed floor
        const Block &topBlk = tower.back();
        drawRoof(topBlk.x, topBlk.y + topBlk.h, topBlk.z, topBlk.w, topBlk.d,
                 topBlk.roofR, topBlk.roofG, topBlk.roofB);
    }

    glPopMatrix();

    for (size_t i = 0; i < fallingPieces.size(); i++) {
        FallingPiece &fp = fallingPieces[i];
        glPushMatrix();
        glTranslatef(fp.x, fp.y, fp.z);
        glRotatef(fp.rot, 0.4f, 1.0f, 0.2f);
        glTranslatef(-fp.x, -fp.y, -fp.z);
        drawBox(fp.x, fp.y, fp.z, fp.w, fp.h, fp.d, fp.r, fp.g, fp.b);
        glPopMatrix();
    }

    // ---------------- HUD (2D overlay) ----------------
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    char buf[128];
    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(buf, "TowerCraft   Score: %d", score);
    drawText(20, windowHeight - 30, buf, GLUT_BITMAP_HELVETICA_18);

    sprintf(buf, "Best: %d", bestScore);
    drawText(20, windowHeight - 55, buf, GLUT_BITMAP_HELVETICA_18);

    // ---- Stability meter: fills up as the tower leans, resets on collapse ----
    float leanFrac = towerLean / LEAN_MAX;
    float barX = 20.0f, barY = windowHeight - 100.0f, barW = 200.0f, barH = 16.0f;
    glColor3f(0.15f, 0.15f, 0.2f);
    glBegin(GL_QUADS);
        glVertex2f(barX, barY); glVertex2f(barX+barW, barY);
        glVertex2f(barX+barW, barY+barH); glVertex2f(barX, barY+barH);
    glEnd();
    float fillW = barW * std::min(1.0f, leanFrac);
    float barR = std::min(1.0f, 0.3f + leanFrac * 1.2f);
    float barG = std::max(0.15f, 1.0f - leanFrac * 1.1f);
    glColor3f(barR, barG, 0.2f);
    glBegin(GL_QUADS);
        glVertex2f(barX, barY); glVertex2f(barX+fillW, barY);
        glVertex2f(barX+fillW, barY+barH); glVertex2f(barX, barY+barH);
    glEnd();
    glColor3f(0.85f, 0.85f, 0.85f);
    drawText(barX, barY + barH + 4, "Stability", GLUT_BITMAP_HELVETICA_12);

    drawText(20, 20, "SPACE/Click=Drop | R=Restart | ESC=Quit",
              GLUT_BITMAP_HELVETICA_12);

    // ---- "Perfect!" bonus popup: pops up, floats, then fades out ----
    if (perfectPopupActive) {
        float t = perfectPopupTimer / PERFECT_POPUP_DURATION; // 1 -> 0
        float floatUp = (1.0f - t) * 55.0f;                   // drifts upward
        float alpha   = std::min(1.0f, t * 1.6f);              // fade near the end
        float popScale = (t > 0.8f) ? (1.0f + (t - 0.8f) * 2.5f) : 1.0f; // quick "punch"

        float baseX = windowWidth / 2.0f;
        float baseY = windowHeight / 2.0f + 90.0f + floatUp;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // soft glow behind the text (drawn a few times, slightly offset)
        glColor4f(1.0f, 0.85f, 0.2f, alpha * 0.25f);
        for (int ox = -2; ox <= 2; ox += 2)
            for (int oy = -2; oy <= 2; oy += 2)
                drawText(baseX - 70 * popScale + ox, baseY + oy, "PERFECT!",
                          GLUT_BITMAP_TIMES_ROMAN_24);

        glColor4f(1.0f, 0.9f, 0.25f, alpha);
        drawText(baseX - 70 * popScale, baseY, "PERFECT!", GLUT_BITMAP_TIMES_ROMAN_24);

        glColor4f(1.0f, 1.0f, 0.7f, alpha);
        char bonusBuf[32];
        sprintf(bonusBuf, "+%d points", lastPerfectPoints);
        drawText(baseX - 45, baseY - 28, bonusBuf, GLUT_BITMAP_HELVETICA_18);

        glDisable(GL_BLEND);
    }

    // ---- "Combo xN!" popup: shown on the 2nd+ consecutive perfect landing,
    //      scaling up a bit more dramatically the longer the streak runs ----
    if (comboPopupActive) {
        float t = comboPopupTimer / COMBO_POPUP_DURATION;
        float floatUp = (1.0f - t) * 45.0f;
        float alpha   = std::min(1.0f, t * 1.6f);
        float streakScale = 1.0f + std::min(comboStreak, 6) * 0.08f;
        float popScale = ((t > 0.8f) ? (1.0f + (t - 0.8f) * 2.5f) : 1.0f) * streakScale;

        float baseX = windowWidth / 2.0f;
        float baseY = windowHeight / 2.0f + 150.0f + floatUp;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        char comboBuf[32];
        sprintf(comboBuf, "COMBO x%d!", comboStreak);

        glColor4f(0.7f, 0.4f, 1.0f, alpha * 0.25f);
        for (int ox = -2; ox <= 2; ox += 2)
            for (int oy = -2; oy <= 2; oy += 2)
                drawText(baseX - 60 * popScale + ox, baseY + oy, comboBuf,
                          GLUT_BITMAP_TIMES_ROMAN_24);

        glColor4f(0.85f, 0.55f, 1.0f, alpha);
        drawText(baseX - 60 * popScale, baseY, comboBuf, GLUT_BITMAP_TIMES_ROMAN_24);

        glDisable(GL_BLEND);
    }

    // ---- "Tower shifted!" popup: shown when a collapse happens (survivable) ----
    if (collapsePopupActive) {
        float t = collapsePopupTimer / COLLAPSE_POPUP_DURATION;
        float floatUp = (1.0f - t) * 55.0f;
        float alpha   = std::min(1.0f, t * 1.6f);
        float popScale = (t > 0.8f) ? (1.0f + (t - 0.8f) * 2.5f) : 1.0f;

        float baseX = windowWidth / 2.0f;
        float baseY = windowHeight / 2.0f - 40.0f + floatUp;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glColor4f(1.0f, 0.3f, 0.15f, alpha * 0.25f);
        for (int ox = -2; ox <= 2; ox += 2)
            for (int oy = -2; oy <= 2; oy += 2)
                drawText(baseX - 100 * popScale + ox, baseY + oy, "TOWER SHIFTED!",
                          GLUT_BITMAP_TIMES_ROMAN_24);

        glColor4f(1.0f, 0.4f, 0.2f, alpha);
        drawText(baseX - 100 * popScale, baseY, "TOWER SHIFTED!", GLUT_BITMAP_TIMES_ROMAN_24);

        glColor4f(1.0f, 0.8f, 0.7f, alpha);
        char penBuf[32];
        sprintf(penBuf, "-%d points", lastCollapsePenalty);
        drawText(baseX - 40, baseY - 28, penBuf, GLUT_BITMAP_HELVETICA_18);

        glDisable(GL_BLEND);
    }

    if (state == GAME_OVER) {
        glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
            glVertex2f(windowWidth/2.0f - 220, windowHeight/2.0f - 60);
            glVertex2f(windowWidth/2.0f + 220, windowHeight/2.0f - 60);
            glVertex2f(windowWidth/2.0f + 220, windowHeight/2.0f + 60);
            glVertex2f(windowWidth/2.0f - 220, windowHeight/2.0f + 60);
        glEnd();
        glDisable(GL_BLEND);

        glColor3f(1.0f, 0.35f, 0.35f);
        drawText(windowWidth/2.0f - 95, windowHeight/2.0f + 20, "GAME OVER",
                  GLUT_BITMAP_TIMES_ROMAN_24);
        glColor3f(1.0f, 1.0f, 1.0f);
        sprintf(buf, "Final Score: %d", score);
        drawText(windowWidth/2.0f - 70, windowHeight/2.0f - 5, buf, GLUT_BITMAP_HELVETICA_18);
        drawText(windowWidth/2.0f - 100, windowHeight/2.0f - 35, "Press R to Restart",
                  GLUT_BITMAP_HELVETICA_18);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glutSwapBuffers();
}

// ---------------------------------------------------------------
//  INPUT
// ---------------------------------------------------------------
void keyboard(unsigned char key, int x, int y) {
    switch (key) {
        case ' ':
            dropBlock();
            break;
        case 'r':
        case 'R':
            resetGame();
            break;
        case 27: // ESC
            exit(0);
    }
}

void mouse(int button, int mstate, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && mstate == GLUT_DOWN) {
        dropBlock();
    }
}

void reshape(int w, int h) {
    windowWidth  = w;
    windowHeight = (h == 0) ? 1 : h;
    glViewport(0, 0, windowWidth, windowHeight);
}

// ---------------------------------------------------------------
//  INIT
// ---------------------------------------------------------------
void initGL() {
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat ambient[]  = { 0.35f, 0.35f, 0.35f, 1.0f };
    GLfloat diffuse[]  = { 0.85f, 0.85f, 0.85f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    resetGame();
}

// ---------------------------------------------------------------
//  MAIN
// ---------------------------------------------------------------
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(windowWidth, windowHeight);
    glutInitWindowPosition(80, 60);
    glutCreateWindow("TowerCraft - GLUT Tower Stacking Game");

    initGL();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutTimerFunc(16, update, 0);

    glutMainLoop();
    return 0;
}
