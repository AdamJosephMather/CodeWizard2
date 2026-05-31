#pragma once

#include "languageserverclient.h"
#include "settingsmanager.h"
#include <atomic>
#include <functional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <minwindef.h>
#include <windef.h>
#include <winuser.h>
#include <dwmapi.h>
#endif

#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include "helper_types.h"
#include "titlebar.h"
#include "widget.h"
#include <mutex>

#ifndef GL_COMBINE
#define GL_COMBINE              0x8570
#define GL_COMBINE_RGB          0x8571
#define GL_SOURCE0_RGB          0x8580
#define GL_SOURCE1_RGB          0x8581
#define GL_SOURCE2_RGB          0x8582
#define GL_OPERAND0_RGB         0x8590
#define GL_OPERAND1_RGB         0x8591
#define GL_OPERAND2_RGB         0x8592
#define GL_INTERPOLATE          0x8575
#define GL_CONSTANT             0x8576
#define GL_PRIMARY_COLOR        0x8577
#define GL_PREVIOUS             0x8578
#endif

#ifdef _WIN32
enum ACCENT_STATE {
	ACCENT_DISABLED                  = 0,
	ACCENT_ENABLE_GRADIENT           = 1,
	ACCENT_ENABLE_TRANSPARENTGRADIENT= 2,
	ACCENT_ENABLE_BLURBEHIND         = 3,
	ACCENT_ENABLE_ACRYLICBLURBEHIND  = 4, // strong acrylic blur (Win10 RS4+)
	ACCENT_ENABLE_HOSTBACKDROP       = 5, // Win11-ish
	ACCENT_INVALID_STATE             = 6
};

struct ACCENT_POLICY {
	ACCENT_STATE accentState;
	DWORD        accentFlags;
	DWORD        gradientColor; // ARGB
	DWORD        animationId;
};

enum WINDOW_COMPOSITION_ATTRIBUTE {
	WCA_UNDEFINED          = 0,
	/* ... */
	WCA_ACCENT_POLICY      = 19,
	/* ... */
};

struct WINDOWCOMPOSITIONATTRIBDATA {
	WINDOW_COMPOSITION_ATTRIBUTE Attribute;
	PVOID   Data;
	SIZE_T  SizeOfData;
};

using pfnSetWindowCompositionAttribute =
	BOOL (WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
#endif

struct StoredSearch {
	std::string path;
	int line;
};

struct KeyboardEvent {
	bool character_callback = false;
	
	GLFWwindow* window;
	
	unsigned int cc_codepoint;
	
	int key;
	int scancode;
	int action;
	int mods;
};

class App {
public:
	using add_rem_func = std::function<void(bool,bool,bool)>;
	using VoidFunction = std::function<void()>;
	using PosFunction = std::function<void(Widget*)>;
	using UpdateFInfoFunction = std::function<void(Widget*, FileInfo*)>;
	using StringGivenFunc = std::function<void(icu::UnicodeString)>;
	
#ifdef _WIN32
	static HWND window_handle;
#endif
	
	static int major_version;
	static int minor_version;
	static int patch_version;
	static icu::UnicodeString vnum;
	static std::string vnumstr;
	
	static int x_nb_current; // for cleaner animations across panel holders.
	static int y_nb_current;
	static int w_nb_current;
	static int h_nb_current;
	
	static bool rendering_add_rect;
	static bool rendering_rem_rect;
	
	const static double SQRT_2;
	
	static bool darkmode;
	
	static std::string empty;
	static void setSynColor(Theme* t, std::string name, int id);
	
	static bool recording_macro;
	static bool replaying_macro;
	static int current_step;
	static int rep_count;
	static std::vector<std::vector<KeyboardEvent>> keyboard_events;
	
	static void checkForUpdates();
	static void setFolder(std::string fpr);
	
	static bool REQUESTING_STRING;
	static StringGivenFunc ON_STRING_GIVEN;
		
	static Widget* commandPalette;
	static Widget* filesButton;
	static Widget* filesList;
	static Widget* commandBox;
	static Widget* toastBox;
	static Widget* scrollNotifyBox;
	
	static std::vector<std::vector<std::string>> files_in_box;
	
	static Widget* before_reps_request;
	
	static std::mutex canMakeChanges;
	
	static SettingsManager* settings;
	
	static std::unordered_map<std::string, Language> languagemap;
	static std::unordered_map<std::string, LanguageServerClient*> lsp_client_map;
	
	static GLFWcursor* regularCursor;
	static GLFWcursor* hResizeCursor;
	static GLFWcursor* vResizeCursor;
	static GLFWcursor* textCursor;
	static GLFWcursor* handCursor;
	static int currentCursorType;
	static int expectedCursorType;
	
	static LanguageServerClient* readyLSP(std::string lsp_command);
	static void restartLSPs();
	
	static FileIndexResult INDEXED_FILES;
	static std::vector<StoredSearch> storedsearches;
	
	static bool rerender;
	static int time_till_regular;
	static bool forceWaitTime;
	static double lastUpdate;
	
	static int text_padding;
	static int border_width;
	
//	static int chauffeur_call_id;
	
	static std::vector<Widget*> all_widgets;
	
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
	static void DrawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
	static void DrawCircle(int x, int y, int radius, int segments, Color* color);
	static void DrawLine(float x1, float y1, float x2, float y2, float width, Color* color);
	static void DrawX(double x, double y, double w, double h, double thickness, Color* color);
	static void DrawPlus(double x, double y, double w, double h, double thickness, Color* color);
	static void DrawMinus(double x, double y, double w, double h, double thickness, Color* color);
	static void DrawSquare(double x, double y, double w, double h, double thickness, Color* color);
	static void DrawRoundedRect(float x, float y, float w, float h, float radius, Color* color, bool border = false, int segments = 5);
	static void DrawInverseRoundedRect(float x, float y, float w, float h, float radius, Color* color, int segments = 5);
	static void DrawBorder(int x, int y, int w, int h, Color* color);
	static void DrawRoundBorder(int x, int y, int w, int h, Color* color, int segments, double radius);
	static void DrawAsteroid(int x, int y, int s, double r, Color* color, int type);
	static void DrawShip(int x, int y, int s, double r, Color* color, bool drawfire);
	
	static void setTheme(Theme theme);
	
	static Color* bgcolor;
	
	static TitleBar *tb;
#ifdef _WIN32
	static LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static WNDPROC originalWndProc;
#endif
	
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
	static void deleteWidget(Widget* w);
	
	static double lastTime;
	static int frameCount;
	
	static std::vector<Widget*> saveable;
	
	static std::atomic<bool> running;

	static void repeatEveryXSeconds(int intervalSeconds, std::function<void()> task);
	static void save();
	static void fixAllTmpFiles();
	static icu::UnicodeString readFileToUnicodeString(const std::string& filename, bool& worked);
	
	static void launchCommandNonBlocking(const std::string& command);
	static void commandUnfocused();
	static void executeCommandPaletteAction();
	static void fillCmdBox();
	static void indexFiles();
	static std::vector<std::string> extractStringWords(std::string word);
	static SearchResult searchAcrossFiles(const std::string& searchTerm);
	
	static void closeFilesList();
	static void openFilesList();
	
	static void openFromCMD(std::string filepath, std::string filename, int line = -1);
	
	static void updateFromTintColor(Theme* t);
	static void setTintedColor(Color* tint_c, Color* c, float b);
	static void displayToast(icu::UnicodeString text);
	static void displayText(icu::UnicodeString text);
	
	static void moveMouse(int x, int y);
	
	static void updateTransparency(bool transparent);
	
	static int moveMouseToX;
	static int moveMouseToY;
	
	static Widget* helpMenu;
};