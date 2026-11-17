#include "rounded_clip.h"
#include <ghostty/vt.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void rect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y); glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}

static void circle(float x, float y, float r) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x,y);
    for (int i=0; i<=64; ++i) {
        const float angle = i * 6.2831853f / 64;
        glVertex2f(x + std::cos(angle)*r, y + std::sin(angle)*r);
    }
    glEnd();
}

static void pixel(int x, int y, int r, int g, int b) {
    unsigned char rgb[3]{};
    glReadPixels(x,y,1,1,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    require(rgb[0]==r && rgb[1]==g && rgb[2]==b, "rounded clip pixel mismatch");
}

static void testClipping() {
    require(glfwInit(), "GLFW initialization failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    auto* window = glfwCreateWindow(64,64,"Theme clipping test",nullptr,nullptr);
    require(window != nullptr, "hidden OpenGL window unavailable");
    glfwMakeContextCurrent(window);
    glViewport(0,0,64,64);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,64,0,64,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    GLint bits=0; glGetIntegerv(GL_STENCIL_BITS,&bits);
    require(bits>=8,"missing stencil buffer");
    glClearColor(0,0,1,1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    glEnable(GL_SCISSOR_TEST); glScissor(0,0,48,64);
    Rendering::withRoundedClip(64,64,25,[]{circle(32,32,25);},[]{
        glColor3f(1,0,0); rect(0,0,64,64);
        Rendering::withRoundedClip(64,64,10,[]{circle(32,32,10);},[]{
            glColor3f(0,1,0); rect(0,0,64,64);
            Rendering::withRoundedClip(12,12,6,[]{circle(32,32,6);},[]{
                glColor3f(0,0,1); rect(0,0,64,64);
                Rendering::withRoundedClip(4,4,2,[]{circle(32,32,2);},[]{
                    glColor3f(1,1,1); rect(0,0,64,64);
                });
            });
        });
        glColor3f(1,1,0); rect(20,30,5,5);
    });
    pixel(0,0,0,0,255); pixel(32,32,255,255,255); pixel(15,32,255,0,0);
    pixel(36,32,0,0,255); pixel(40,32,0,255,0);
    pixel(22,32,255,255,0); pixel(55,32,0,0,255);
    Rendering::withRoundedClip(8,8,4,[]{circle(8,8,4);},[]{
        glColor3f(1,1,1); rect(0,0,64,64);
    });
    pixel(8,8,255,255,255); pixel(32,32,255,255,255);
    std::array<unsigned char,64*64> stencil{};
    glReadPixels(0,0,64,64,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,stencil.data());
    for (auto value:stencil) require(value==0,"stencil clip leaked into sibling rendering");
    require(!glIsEnabled(GL_STENCIL_TEST),"stencil enable state leaked");
    require(glIsEnabled(GL_SCISSOR_TEST),"scissor state lost");
    GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    require(mask[0] && mask[1] && mask[2] && !mask[3],"color write mask lost");
    bool masked=false, drawn=false;
    Rendering::withRoundedClip(8,8,0,[&]{masked=true;},[&]{drawn=true;});
    require(drawn && !masked,"square theme used a rounded mask");
    drawn=false;
    Rendering::withRoundedClip(0,8,4,[&]{masked=true;},[&]{drawn=true;});
    require(!drawn,"empty widget rendered");
    require(glGetError()==GL_NO_ERROR,"OpenGL error during clipping");
    glfwDestroyWindow(window); glfwTerminate();
}

static bool same(GhosttyColorRgb a, GhosttyColorRgb b) {
    return a.r==b.r && a.g==b.g && a.b==b.b;
}

static void testTerminalDefaults() {
    GhosttyTerminal terminal{};
    GhosttyTerminalOptions options{}; options.cols=32; options.rows=8;
    require(ghostty_terminal_new(nullptr,&terminal,options)==GHOSTTY_SUCCESS,"terminal creation failed");
    GhosttyColorRgb dark{24,24,24}, light{245,245,245}, fg{220,220,220}, actual{};
    std::array<GhosttyColorRgb,256> paletteBefore{}, paletteAfter{};
    require(ghostty_terminal_get(terminal,GHOSTTY_TERMINAL_DATA_COLOR_PALETTE,paletteBefore.data())==GHOSTTY_SUCCESS,"palette query failed");
    require(ghostty_terminal_set(terminal,GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND,&dark)==GHOSTTY_SUCCESS,"background default failed");
    require(ghostty_terminal_set(terminal,GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND,&fg)==GHOSTTY_SUCCESS,"foreground default failed");
    ghostty_terminal_get(terminal,GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND,&actual);
    require(same(actual,dark),"theme background not applied");
    const char* overrideColor="\x1b]11;#112233\x07";
    ghostty_terminal_vt_write(terminal,reinterpret_cast<const uint8_t*>(overrideColor),std::strlen(overrideColor));
    ghostty_terminal_set(terminal,GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND,&light);
    ghostty_terminal_get(terminal,GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND,&actual);
    require(same(actual,{17,34,51}),"theme change overwrote terminal program's OSC color");
    const char* resetColor="\x1b]111\x07";
    ghostty_terminal_vt_write(terminal,reinterpret_cast<const uint8_t*>(resetColor),std::strlen(resetColor));
    ghostty_terminal_get(terminal,GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND,&actual);
    require(same(actual,light),"OSC reset did not restore current theme default");
    ghostty_terminal_get(terminal,GHOSTTY_TERMINAL_DATA_COLOR_PALETTE,paletteAfter.data());
    for(size_t i=0;i<paletteBefore.size();++i) require(same(paletteBefore[i],paletteAfter[i]),"theme change altered ANSI palette");
    ghostty_terminal_free(terminal);
}

int main() {
    testClipping();
    testTerminalDefaults();
    std::cout << "Nested clipping, square fallback, GL state, terminal defaults and OSC/ANSI preservation passed\n";
}
