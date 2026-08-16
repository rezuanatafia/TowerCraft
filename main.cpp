/*
 =====================================================================
  TOWERCRAFT  —  A Tower-Stacking Game built with GLUT / OpenGL
 =====================================================================
  Features:
    - Moving building block (alternates X / Z axis each floor)
    - Space / Left-Click to drop the floor and stack it
    - Automatic collision/overlap trimming (classic "Tower Stack" logic)
    - Falling animation for the trimmed-off overhang pieces
    - Score tracking + increasing difficulty (speed grows with height)
    - Smooth camera that follows the tower as it grows
    - Game-over detection (missed block) + Restart system (R key)
    - Lit, shaded 3D graphics with a rainbow color gradient per floor

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
const float BLOCK_H     = 1.0f;   // height of every floor
const float BASE_SIZE   = 5.0f;   // width/depth of the starting platform
const float MOVE_RANGE  = 6.0f;   // how far a floor slides left/right
const float GRAVITY     = 0.035f; // for falling debris pieces

int windowWidth  = 900;
int windowHeight = 650;

// ---------------------------------------------------------------
//  DATA STRUCTURES
// ---------------------------------------------------------------
struct Block {
    float x, y, z;   // position (y = bottom of the block)
    float w, h, d;   // dimensions
    float r, g, b;   // color
};

struct FallingPiece {
    float x, y, z;
    float w, h, d;
    float r, g, b;
    float vy;
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

// ---------------------------------------------------------------
//  UTILITIES
// ---------------------------------------------------------------
void hsv2rgb(float h, float s, float v, float &r, float &g, float &b) {
    int   i = (int)(h / 60.0f) % 6;
    float f = (h / 60.0f) - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);
    switch (i) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default: r=v; g=p; b=q; break;
    }
}

void colorForLevel(int level, float &r, float &g, float &b) {
    float hue = fmod(level * 33.0f, 360.0f);
    hsv2rgb(hue, 0.55f, 0.95f, r, g, b);
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

    // thin dark outline for a crisp "building block" look
    glDisable(GL_LIGHTING);
    glColor3f(0.05f, 0.05f, 0.08f);
    glLineWidth(1.4f);
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

// ---- window on a face that lies in the XY plane (front/back, fixed Z) ----
void drawWindowXY(float cx, float cy, float z, float ww, float wh) {
    glColor3f(0.68f, 0.87f, 0.96f);
    glBegin(GL_QUADS);
        glVertex3f(cx-ww/2, cy-wh/2, z); glVertex3f(cx+ww/2, cy-wh/2, z);
        glVertex3f(cx+ww/2, cy+wh/2, z); glVertex3f(cx-ww/2, cy+wh/2, z);
    glEnd();
    glColor3f(0.14f, 0.14f, 0.18f);
    glLineWidth(1.3f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(cx-ww/2, cy-wh/2, z); glVertex3f(cx+ww/2, cy-wh/2, z);
        glVertex3f(cx+ww/2, cy+wh/2, z); glVertex3f(cx-ww/2, cy+wh/2, z);
    glEnd();
    glBegin(GL_LINES); // window cross-bars
        glVertex3f(cx, cy-wh/2, z); glVertex3f(cx, cy+wh/2, z);
        glVertex3f(cx-ww/2, cy, z); glVertex3f(cx+ww/2, cy, z);
    glEnd();
}

// ---- window on a face that lies in the ZY plane (left/right, fixed X) ----
void drawWindowZY(float x, float cy, float cz, float wd, float wh) {
    glColor3f(0.68f, 0.87f, 0.96f);
    glBegin(GL_QUADS);
        glVertex3f(x, cy-wh/2, cz-wd/2); glVertex3f(x, cy-wh/2, cz+wd/2);
        glVertex3f(x, cy+wh/2, cz+wd/2); glVertex3f(x, cy+wh/2, cz-wd/2);
    glEnd();
    glColor3f(0.14f, 0.14f, 0.18f);
    glLineWidth(1.3f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(x, cy-wh/2, cz-wd/2); glVertex3f(x, cy-wh/2, cz+wd/2);
        glVertex3f(x, cy+wh/2, cz+wd/2); glVertex3f(x, cy+wh/2, cz-wd/2);
    glEnd();
    glBegin(GL_LINES);
        glVertex3f(x, cy-wh/2, cz); glVertex3f(x, cy+wh/2, cz);
        glVertex3f(x, cy, cz-wd/2); glVertex3f(x, cy, cz+wd/2);
    glEnd();
}

// Adds windows on all 4 vertical faces of a block, turning it into a "floor"
void drawFacadeDetails(float cx, float baseY, float cz, float w, float h, float d) {
    float x0 = cx - w/2, x1 = cx + w/2;
    float z0 = cz - d/2, z1 = cz + d/2;
    float cyMid = baseY + h*0.5f;

    glDisable(GL_LIGHTING);

    int colsX = std::max(1, (int)(w / 1.3f));
    float ww = std::min(0.55f, (w/colsX) * 0.55f);
    float wh = std::min(0.5f, h * 0.55f);
    for (int i = 0; i < colsX; i++) {
        float cxw = x0 + (i + 0.5f) * (w / colsX);
        drawWindowXY(cxw, cyMid, z1 + 0.02f, ww, wh);
        drawWindowXY(cxw, cyMid, z0 - 0.02f, ww, wh);
    }

    int colsZ = std::max(1, (int)(d / 1.3f));
    float wd = std::min(0.55f, (d/colsZ) * 0.55f);
    for (int i = 0; i < colsZ; i++) {
        float czw = z0 + (i + 0.5f) * (d / colsZ);
        drawWindowZY(x1 + 0.02f, cyMid, czw, wd, wh);
        drawWindowZY(x0 - 0.02f, cyMid, czw, wd, wh);
    }

    glEnable(GL_LIGHTING);
}

// A simple front door for the ground floor
void drawDoor(float cx, float baseY, float cz, float w, float h, float d) {
    float doorW = std::min(0.9f, w * 0.3f);
    float doorH = std::min(0.85f, h * 0.85f);
    float z = cz + d/2 + 0.03f;

    glDisable(GL_LIGHTING);
    glColor3f(0.36f, 0.22f, 0.12f);
    glBegin(GL_QUADS);
        glVertex3f(cx-doorW/2, baseY, z); glVertex3f(cx+doorW/2, baseY, z);
        glVertex3f(cx+doorW/2, baseY+doorH, z); glVertex3f(cx-doorW/2, baseY+doorH, z);
    glEnd();
    glColor3f(0.12f, 0.08f, 0.05f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
        glVertex3f(cx-doorW/2, baseY, z); glVertex3f(cx+doorW/2, baseY, z);
        glVertex3f(cx+doorW/2, baseY+doorH, z); glVertex3f(cx-doorW/2, baseY+doorH, z);
    glEnd();
    glColor3f(0.85f, 0.75f, 0.25f); // doorknob
    glPointSize(4.0f);
    glBegin(GL_POINTS);
        glVertex3f(cx+doorW*0.28f, baseY+doorH*0.5f, z+0.01f);
    glEnd();
    glEnable(GL_LIGHTING);
}

// Pitched roof + little chimney, drawn on whichever block is currently on top
void drawRoof(float cx, float topY, float cz, float w, float d) {
    float x0 = cx - w/2, x1 = cx + w/2;
    float z0 = cz - d/2, z1 = cz + d/2;
    float apexY = topY + std::min(w, d) * 0.45f + 0.3f;

    glColor3f(0.72f, 0.24f, 0.2f);
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

    // little chimney
    drawBox(cx + w*0.22f, topY + std::min(w,d)*0.22f, cz - d*0.15f,
            0.35f, 0.55f, 0.35f, 0.55f, 0.3f, 0.28f);
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
    float r,g,b;
    colorForLevel((int)tower.size(), r, g, b);
    currentBlock.r = r; currentBlock.g = g; currentBlock.b = b;

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

    Block base;
    base.x = 0; base.y = 0; base.z = 0;
    base.w = BASE_SIZE; base.h = BLOCK_H; base.d = BASE_SIZE;
    base.r = 0.55f; base.g = 0.55f; base.b = 0.6f;
    tower.push_back(base);

    camY = 6.0f; camYTarget = 6.0f;
    spawnNext();
}

void addFallingPiece(float x, float y, float z, float w, float h, float d,
                      float r, float g, float b) {
    if (w <= 0.001f || d <= 0.001f) return;
    FallingPiece fp;
    fp.x = x; fp.y = y; fp.z = z;
    fp.w = w; fp.h = h; fp.d = d;
    fp.r = r; fp.g = g; fp.b = b;
    fp.vy = 0.02f;
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

void dropBlock() {
    if (state != PLAYING) return;

    const Block &top = tower.back();

    // NOTE: blocks are NO LONGER sliced/trimmed on landing.
    // We only check whether the moving block overlaps the block below
    // by at least a minimum amount ("good landing"). If it does, the
    // FULL-SIZE block is stacked as-is (like a real building block
    // simply resting on the one beneath it). If there isn't enough
    // overlap, it's a miss -> game over.

    if (moveAxis == 0) {
        float curL = currentBlock.x - currentBlock.w/2;
        float curR = currentBlock.x + currentBlock.w/2;
        float topL = top.x - top.w/2;
        float topR = top.x + top.w/2;

        float overlapL = std::max(curL, topL);
        float overlapR = std::min(curR, topR);
        float overlapW = overlapR - overlapL;

        // require a reasonable chunk of the block to be supported
        float minOverlap = std::min(currentBlock.w, top.w) * 0.35f;
        if (overlapW <= minOverlap) { triggerGameOver(); return; }

        // block keeps its full width -- no cutting, just settles in place
    } else {
        float curF = currentBlock.z - currentBlock.d/2;
        float curB = currentBlock.z + currentBlock.d/2;
        float topF = top.z - top.d/2;
        float topB = top.z + top.d/2;

        float overlapF = std::max(curF, topF);
        float overlapB = std::min(curB, topB);
        float overlapD = overlapB - overlapF;

        float minOverlap = std::min(currentBlock.d, top.d) * 0.35f;
        if (overlapD <= minOverlap) { triggerGameOver(); return; }

        // block keeps its full depth -- no cutting, just settles in place
    }

    tower.push_back(currentBlock);
    score++;
    moveSpeed += 0.0035f;   // gets harder as you climb
    spawnNext();
}

// ---------------------------------------------------------------
//  UPDATE / TIMER
// ---------------------------------------------------------------
void update(int value) {
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

    for (size_t i = 0; i < fallingPieces.size(); i++) {
        fallingPieces[i].vy += GRAVITY;
        fallingPieces[i].y  -= fallingPieces[i].vy;
        fallingPieces[i].rot += fallingPieces[i].vrot;
    }
    for (size_t i = 0; i < fallingPieces.size();) {
        if (fallingPieces[i].y < -25.0f) fallingPieces.erase(fallingPieces.begin()+i);
        else i++;
    }

    camYTarget = std::max(6.0f, (float)(tower.size()) * BLOCK_H);
    camY += (camYTarget - camY) * 0.07f;

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

    for (size_t i = 0; i < tower.size(); i++) {
        Block &blk = tower[i];
        drawBox(blk.x, blk.y, blk.z, blk.w, blk.h, blk.d, blk.r, blk.g, blk.b);
        drawFacadeDetails(blk.x, blk.y, blk.z, blk.w, blk.h, blk.d);
        if (i == 0) drawDoor(blk.x, blk.y, blk.z, blk.w, blk.h, blk.d); // ground floor entrance
    }

    if (state == PLAYING) {
        drawBox(currentBlock.x, currentBlock.y, currentBlock.z,
                currentBlock.w, currentBlock.h, currentBlock.d,
                currentBlock.r, currentBlock.g, currentBlock.b);
        drawFacadeDetails(currentBlock.x, currentBlock.y, currentBlock.z,
                           currentBlock.w, currentBlock.h, currentBlock.d);
        // roof rides on the moving block while it's still in play
        drawRoof(currentBlock.x, currentBlock.y + currentBlock.h, currentBlock.z,
                 currentBlock.w, currentBlock.d);
    } else {
        // once the game ends, the roof settles on the last placed floor
        const Block &topBlk = tower.back();
        drawRoof(topBlk.x, topBlk.y + topBlk.h, topBlk.z, topBlk.w, topBlk.d);
    }

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

    drawText(20, 20, "SPACE / Click = Drop floor   |   R = Restart   |   ESC = Quit",
              GLUT_BITMAP_HELVETICA_12);

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
