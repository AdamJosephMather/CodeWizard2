#pragma once

//#include <GL/glew.h>
#include "languageserverclient.h"
#include "settingsmanager.h"
#include <atomic>
#include <functional>
#include <memory>

#ifndef _WIN64
#define _WIN64
#endif
#ifndef _M_X64
#define _M_X64
#endif
#ifndef _AMD64_
#define _AMD64_
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include <GLFW/glfw3.h>
#include <minwindef.h>
#include <string>
#include <vector>
#include <windef.h>
#include <winuser.h>
#include "helper_types.h"
#include "titlebar.h"
#include "widget.h"
#include <mutex>

struct StoredSearch {
	std::string path;
	int line;
};

class App {
public:
	using add_rem_func = std::function<void(bool,bool,bool)>;
	using VoidFunction = std::function<void()>;
	using PosFunction = std::function<void(Widget*)>;
	using UpdateFInfoFunction = std::function<void(Widget*, FileInfo*)>;
	using StringGivenFunc = std::function<void(icu::UnicodeString)>;
	
	static bool REQUESTING_STRING;
	static StringGivenFunc ON_STRING_GIVEN;
		
	static Widget* commandPalette;
	static Widget* commandBox;
	
	static std::mutex canMakeChanges;
	
	static SettingsManager* settings;
	
	static std::unordered_map<std::string, Language> languagemap;
	static std::unordered_map<std::string, LanguageServerClient*> lsp_client_map;
	
	static LanguageServerClient* getLSP(std::string lsp_command);
	
	static FileIndexResult INDEXED_FILES;
	static std::vector<StoredSearch> storedsearches;
	
	static bool rerender;
	static int time_till_regular;
	static bool forceWaitTime;
	static double lastUpdate;
	
	static int text_padding;
	
	static Widget* rootelement;
	static int WINDOW_WIDTH;
	static int WINDOW_HEIGHT;
	static std::string WINDOW_TITLE;
	static int mouseX;
	static int mouseY;
	static Theme theme;
	static Widget* activeLeafNode;
	static Widget* activeEditor;
	static Widget* beforeCommandLeafNode;
	
	static void DoFullRenderWithoutInput();
	static bool Init();
	static void Run();
	
	static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
	static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
	static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void character_callback(GLFWwindow* window, unsigned int codepoint);
	static void scroll_callback(GLFWwindow* window, double xpos, double ypos);
	static void resize_callback(GLFWwindow* window, int width, int height);
	
	static bool (*on_key_event)(int key, int scancode, int action, int mods);
	static bool (*on_char_event)(unsigned int codepoint);
	static bool (*on_mouse_button_event)(int button, int action, int mods);
	static bool (*on_mouse_move_event)();
	static bool (*on_scroll_event)(double xchange, double ychange);
	static bool (*on_resize_event)(int width, int height);
	
	static GLFWwindow* window;
	
	static void DrawRect(int x, int y, int w, int h, Color* color);
	static void DrawRoundedRect(float x, float y, float w, float h, float radius, Color* color, int segments = 5);
	
	static void setTheme(Theme theme);
	
	static Color* bgcolor;
	
	static TitleBar *tb;
	static LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static WNDPROC originalWndProc;
	
	static void min_button();
	static void win_button();
	static void ext_button();
	
	static void adding_panel();
	static void removing_panel();
	static void nada_panel();
	static bool curr_removing_panel;
	static bool curr_adding_panel;
	
	static int SKIZ_X;
	static int SKIZ_Y;
	static int SKIZ_W;
	static int SKIZ_H;
	static void runWithSKIZ(int nx, int ny, int nw, int nh, VoidFunction withskiz);
	
	static void MoveWidget(Widget* w, Widget* new_parent);
	static void RemoveWidgetFromParent(Widget* w);
	static int GetWidgetIndexInParent(Widget* w);
	static void ReplaceWith(Widget* existing, Widget* replacement);
	static void setActiveLeafNode(Widget* w);
	
	static double lastTime;
	static int frameCount;
	
	static std::vector<Widget*> saveable;
	
	static std::atomic<bool> running;

	static void repeatEveryXSeconds(int intervalSeconds, std::function<void()> task);
	static void save();
	static icu::UnicodeString readFileToUnicodeString(const std::string& filename, bool& worked);
	
	static void launchCommandNonBlocking(const std::string& command);
	static void commandUnfocused();
	static void executeCommandPaletteAction();
	static void fillCmdBox();
	static void indexFiles();
	static std::vector<std::string> extractStringWords(std::string word);
	static SearchResult searchAcrossFiles(const std::string& searchTerm);
	
	static void openFromCMD(std::string filepath, std::string filename, int line = -1);
	
	static void updateFromTintColor(Theme* t);
	static void setTintedColor(Color* tint_c, Color* c, float b);
};