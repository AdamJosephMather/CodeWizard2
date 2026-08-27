#include "application.h"
#include <GLFW/glfw3.h>
#include <queue>
#include <random>
#include <syntect_bridge.h>
#include <iostream>
#include "scrollnotify.h"
#include "codeedit.h"
#include "editor.h"
#include "helper_types.h"
#include <unordered_set>
#include <vector>
#include <regex>
#include "panel_holder.h"
//#include "modelxrunner.h"
#include "terminalwidget.h"
#include "text_renderer.h"
#include <cmath>
#include "tinyfiledialogs.h"
#include "titlebar.h"
#include "languageserverclient.h"
#include "textedit.h"
#include "toast.h"
#include "filetree.h"
#include "sshfilebackend.h"

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

#include <fstream>

#include <stb_image.h>
#include "curler.h"
#include "myrect.h"
#include "label.h"
#include "updatechecker.h"

#include "Verify.hpp"

int App::major_version = 2;
int App::minor_version = 5;
int App::patch_version = 17; // 🚀 (we now support emojis)

const std::vector<int> version = {App::major_version, App::minor_version, App::patch_version};

std::vector<Vec2> App::splashTexture = {};

std::function<bool()> App::doMipmapThing = [](){ return false; };

#ifndef M_PI
const float M_PI = 3.141592653589793238f; // nice
#endif

std::vector<Widget*> App::all_widgets = {};

#ifdef _WIN32
HWND App::window_handle = nullptr;
#endif

bool App::last_transparency_w_clear = false;
int App::reclear = 3;
bool App::darkmode = true;
std::string App::empty = "";

MST::MonoString App::vnum = MST::MonoString();
std::string App::vnumstr = "";

int App::moveMouseToX = -1;
int App::moveMouseToY = -1;

Widget* App::helpMenu = nullptr;
int App::currentMenu = -1;

bool App::recording_macro = false;
bool App::replaying_macro = false;
int App::rep_count = 0;
int App::current_step = 0;
std::vector<std::vector<KeyboardEvent>> App::keyboard_events = {{}};

//int App::chauffeur_call_id = 0;

bool App::REQUESTING_STRING = false;
App::StringGivenFunc App::ON_STRING_GIVEN = nullptr;
TextEdit* STRING_REQUEST_TEXTEDIT = nullptr;
MyRect* STRING_REQUEST_RECTANGLE = nullptr;
Label* STRING_REQUEST_LABEL = nullptr;

SettingsManager* App::settings = new SettingsManager();
std::mutex App::canMakeChanges;
FileIndexResult App::INDEXED_FILES = FileIndexResult();
SharedProgress App::storedsearches = {};
std::thread searchWorker;
std::atomic<bool>* searchStopFlag = new std::atomic<bool>(false);

Widget* App::commandPalette = nullptr;
Widget* App::filesButton = nullptr;
Widget* App::filesList = nullptr;
Widget* App::commandBox = nullptr;
Widget* App::menu = nullptr;
Widget* App::toastBox = nullptr;
Widget* App::scrollNotifyBox = nullptr;

std::unordered_map<std::string, CW_SyntaxEngine*> App::highlighters = {};

std::vector<std::vector<std::string>> App::files_in_box = {};

Widget* App::before_reps_request = nullptr;

int App::WINDOW_WIDTH = 1200;
int App::WINDOW_HEIGHT = 800;
std::string App::WINDOW_TITLE = "CodeWizard2 V";
std::string pending_window_title = "";

int App::mouseX = 0;
int App::mouseY = 0;
const double App::SQRT_2 = 1.41421356;

bool (*App::on_key_event)(int key, int scancode, int action, int mods) = nullptr;
bool (*App::on_char_event)(unsigned int codepoint) = nullptr;
bool (*App::on_mouse_button_event)(int button, int action, int mods) = nullptr;
bool (*App::on_mouse_move_event)() = nullptr;
bool (*App::on_scroll_event)(double xchange, double ychange) = nullptr;
bool (*App::on_resize_event)(int width, int height) = nullptr;

GLFWwindow* App::window = nullptr;
GLFWwindow* g_main_window = nullptr;
Widget* App::rootelement = nullptr;

bool App::recheckmenusizing = true;

int App::text_padding = 5;
int App::border_width = 1;

Color* App::bgcolor = MakeColor(0.5, 0.5, 0.5);

#ifdef _WIN32
WNDPROC App::originalWndProc;
#endif
TitleBar* App::tb = nullptr;

bool App::curr_removing_panel = false;
bool App::curr_adding_panel = false;

int App::SKIZ_X = 0;
int App::SKIZ_Y = 0;
int App::SKIZ_W = 0;
int App::SKIZ_H = 0;

Widget* App::activeLeafNode = nullptr;
Widget* App::activeEditor = nullptr;
Widget* App::beforeCommandLeafNode = nullptr;

Theme App::theme;

double App::lastTime = 0.0;
int App::frameCount = 0;

std::atomic<bool> App::running = true;
bool App::rerender = true;
int App::time_till_regular = 40;
bool App::forceWaitTime = false;
double App::lastUpdate = 0;

std::unordered_map<std::string, Language> App::languagemap = {};
std::unordered_map<std::string, LanguageServerClient*> App::lsp_client_map = {};

GLFWcursor* App::regularCursor = nullptr;
GLFWcursor* App::hResizeCursor = nullptr;
GLFWcursor* App::vResizeCursor = nullptr;
GLFWcursor* App::textCursor = nullptr;
GLFWcursor* App::handCursor = nullptr;

int App::currentCursorType = -1;
int App::expectedCursorType = -1;

int App::x_nb_current = 0; // for cleaner animations across panel holders.
int App::y_nb_current = 0;
int App::w_nb_current = 0;
int App::h_nb_current = 0;

bool App::rendering_add_rect = false;
bool App::rendering_rem_rect = false;

#ifdef _WIN32
static void EnablePerMonitorDpiAwareness() {
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	if (user32) {
		using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
		auto setCtx = (SetDpiCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (setCtx) {
			setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			return;
		}
	}
	SetProcessDPIAware();
}

static bool rectIntersects(const RECT& a, const RECT& b) {
	RECT out{};
	return IntersectRect(&out, &a, &b) != 0;
}

static bool rectOnAnyMonitorWorkArea(const RECT& r) {
	struct Ctx {
		const RECT* r;
		bool hit;
	} ctx{ &r, false };

	auto enumProc = [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
		Ctx* c = reinterpret_cast<Ctx*>(lp);
		MONITORINFO mi{};
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfo(hMon, &mi)) {
			if (rectIntersects(*c->r, mi.rcWork)) {
				c->hit = true;
				return FALSE; // stop enumeration early
			}
		}
		return TRUE;
	};

	EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.hit;
}
#endif

void restoreWindowPosAndSize(GLFWwindow* window, SettingsManager* settings, int screenWidth, int screenHeight) {
	int w = settings->getValue("window_width", 1200);
	int h = settings->getValue("window_height", 800);

	int x = settings->getValue("window_x", screenWidth / 2 - w / 2);
	int y = settings->getValue("window_y", screenHeight / 2 - h / 2);
	
#ifdef _WIN32
	RECT winRect{ x, y, x + w, y + h };
	if (!rectOnAnyMonitorWorkArea(winRect)) {
		x = screenWidth/2-w/2;
		y = screenHeight/2-h/2;
	}
#endif
	
	glfwSetWindowSize(window, w, h);
	glfwSetWindowPos(window, x, y);
}

void App::fixUpLanguages() {
	auto langs = settings->loadLanguages(); // just goes ahead and has it reload them, ignoring unneeded things
	settings->saveLanguages(langs);
}

std::vector<Vec2> LoadSVGTriangles(const std::string& filename) {
	std::ifstream file(filename);
	if (!file.is_open()) return {};

	int numTriangles;
	if (!(file >> numTriangles)) return {};

	std::vector<Vec2> vertices;
	vertices.reserve(numTriangles * 3);

	for (int i = 0; i < numTriangles * 3; ++i) {
		float x, y;
		file >> x >> y;
		// Shift [0.0, 1.0] to [-0.5, 0.5] for origin-centered transformation
		vertices.push_back({ x - 0.5f, y - 0.5f });
	}
	return vertices;
}

bool App::Init() {
	std::cout << "Init...\n";
	
	MST::MonoString vnum = MST::toMonoString(std::to_string(App::major_version)+"."+std::to_string(App::minor_version)+"."+std::to_string(App::patch_version));
	vnumstr = MST::toString(vnum);
	WINDOW_TITLE += vnumstr;
	
	STRING_REQUEST_TEXTEDIT = new TextEdit(nullptr, [&](Widget* w){
		w->t_x = w->t_x+w->t_w/2-(w->t_w/4);
		w->t_w /= 2;
		int new_h = TextRenderer::get_text_height()+text_padding*2;
		w->t_y = w->t_y+w->t_h/2-(new_h/2);
		w->t_h = new_h;
		STRING_REQUEST_RECTANGLE->position(w->t_x, w->t_y, w->t_w, w->t_h);
	});
	STRING_REQUEST_TEXTEDIT->id = MST::toMonoString("STRING_REQUEST_TEXTEDIT");
	STRING_REQUEST_TEXTEDIT->borderColor = nullptr;
	STRING_REQUEST_TEXTEDIT->activeBorderColor = nullptr;
	
	STRING_REQUEST_RECTANGLE = new MyRect(nullptr, [&](Widget* w){
		int h = TextRenderer::get_text_height()+text_padding*2;
		w->t_x = STRING_REQUEST_TEXTEDIT->t_x-h;
		w->t_w = STRING_REQUEST_TEXTEDIT->t_w+h*2;
		w->t_y = STRING_REQUEST_TEXTEDIT->t_y-h;
		w->t_h = STRING_REQUEST_TEXTEDIT->t_h+h*2;
	});
	STRING_REQUEST_RECTANGLE->id = MST::toMonoString("STRING_REQUEST_RECTANGLE");
	STRING_REQUEST_RECTANGLE->border_color = App::theme.active_color;
	
	STRING_REQUEST_LABEL = new Label(nullptr);
	STRING_REQUEST_LABEL->POSITIONER = [&](Widget* w) {
		int h = TextRenderer::get_text_height()+text_padding*2;
		w->t_x = STRING_REQUEST_RECTANGLE->t_x;
		w->t_w = STRING_REQUEST_RECTANGLE->t_w;
		w->t_y = STRING_REQUEST_RECTANGLE->t_y;
		w->t_h = h;
	};
	STRING_REQUEST_LABEL->rect = false;
	STRING_REQUEST_LABEL->border = false;
	STRING_REQUEST_LABEL->id = MST::toMonoString("STRING_REQUEST_LABEL");
	
	settings->loadSettings();
	
	MST::setTabWidth(settings->getValue("tab_width", 4));
	
	std::string password = settings->getValue("ajm_asv3_password", (std::string)"devpassword"); // must happen after settings setup.
	
	Verify::setup(password);
	
	std::string initial_folder = settings->getValue("current_folder", getExecutableDir());
	BackendFileStat initial_info;
	std::string initial_error;
	if (!FileBackends::current()->stat(initial_folder, initial_info, initial_error) ||
		!initial_info.exists || !initial_info.is_directory) {
		initial_folder = settings->getValue("ssh_previous_local_folder", getExecutableDir());
		settings->setValue("current_folder", initial_folder);
	}
	setFolder(initial_folder);
	
	darkmode = settings->getValue("dark_mode", true);
	WINDOW_WIDTH = settings->getValue("window_width", 1200);
	WINDOW_HEIGHT = settings->getValue("window_height", 800);
	
#ifdef _WIN32
	EnablePerMonitorDpiAwareness();
#endif
	
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return false;
	}
	
	glfwWindowHint(GLFW_SAMPLES, 4); // Request 4x MSAA (or 2, 8, 16 depending on GPU support)
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	
	window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE.c_str(), nullptr, nullptr);
	g_main_window = window;
	
	glfwSetWindowSizeLimits(window, 500, 250, GLFW_DONT_CARE, GLFW_DONT_CARE);
	
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

	if (!window) {
		std::cerr << "Failed to create window\n";
		glfwTerminate();
		return false;
	}
	
	regularCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	if (!regularCursor) {
		std::cerr << "Failed to create arrow cursor" << std::endl;
	}
	hResizeCursor = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
	if (!hResizeCursor) {
		std::cerr << "Failed to create resize ew cursor" << std::endl;
	}
	vResizeCursor = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
	if (!vResizeCursor) {
		std::cerr << "Failed to create resize ns cursor" << std::endl;
	}
	textCursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
	if (!textCursor) {
		std::cerr << "Failed to create ibeam cursor" << std::endl;
	}
	handCursor = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
	if (!handCursor) {
		std::cerr << "Failed to create hand cursor" << std::endl;
	}
	
	glfwMakeContextCurrent(window);
	
	glEnable(GL_MULTISAMPLE); // this makes a noticable difference in image quality (also reduced fps from 800 to 620 on my laptop at half screen width)
	
	glfwSwapInterval(1); // Enable vsync
	
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	
	
	// Set input callbacks
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetWindowSizeCallback(window, resize_callback);
	glfwSetCharCallback(window, character_callback);
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, -1, 1); // Top-left is (0,0), bottom-right is (width, height)
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	
	int screenWidth = mode->width;
	int screenHeight = mode->height;

	restoreWindowPosAndSize(window, settings, screenWidth, screenHeight);
	
	TextRenderer::after_font_change = [&](){
		text_padding = std::min(TextRenderer::get_text_width(1), TextRenderer::get_text_height()) * 0.5;
		recheckmenusizing = true;
		reclear = 3;
		rerender = true;
	};
	
	TextRenderer::set_font_size(settings->getValue("font_size", 23.0f));
	std::string default_font_path = getExecutableDir()+"/cascadia/CascadiaCode-Regular.ttf";
	std::string font_path = settings->getValue("font_path", default_font_path);
	
	bool success = TextRenderer::init_font(font_path.c_str()); // Or whatever .ttf you have
	
	if (!success) {
		TextRenderer::init_font(default_font_path.c_str());
	}
	
	// setup titlebar
	
	rootelement = new Widget(nullptr);
	rootelement->id = MST::toMonoString("Root");
	new PanelHolder(rootelement);
	tb = new TitleBar(rootelement);
	tb->exempt_from_parent_for_cursor = true;
	toastBox = new Toast(nullptr);
	
#ifdef _WIN32
	// Now grab the HWND and force a resize border
	window_handle = glfwGetWin32Window(window);

	DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
	DwmSetWindowAttribute(window_handle, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

	// Chain your custom proc
	originalWndProc = (WNDPROC)SetWindowLongPtr(window_handle, GWLP_WNDPROC, (LONG_PTR)CustomWndProc);

	// Grab the existing style
	LONG_PTR style = GetWindowLongPtr(window_handle, GWL_STYLE);

	// Re-add the bits Windows needs for snapping + Win+Arrows:
	style |= (WS_THICKFRAME    // sizing border
			 | WS_MINIMIZEBOX 
			 | WS_MAXIMIZEBOX 
			 | WS_SYSMENU );    // optional, for system menu
	style &= ~WS_CAPTION;

	SetWindowLongPtr(window_handle, GWL_STYLE, style);

	// Force Windows to re-evaluate the non-client area:
	SetWindowPos(window_handle, nullptr, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
#endif
	
	lastTime = glfwGetTime();
	
	repeatEveryXSeconds(4, [&](){
		std::lock_guard<std::mutex> lock(canMakeChanges);
		save();
	});
	
	repeatEveryXSeconds(60, [&](){
		std::lock_guard<std::mutex> lock(canMakeChanges);
		fixAllTmpFiles();
	});
	
	
	// app icon
	
	auto pth_str = getExecutableDir()+"/app.png";
	auto pth = pth_str.c_str();

	GLFWimage icon;
	int channels;
	icon.pixels = stbi_load(pth, &icon.width, &icon.height, &channels, 4);
	
	if (icon.pixels) {
		glfwSetWindowIcon(window, 1, &icon);
		stbi_image_free(icon.pixels);
	} else {
		std::cerr << "Failed to load icon: " << stbi_failure_reason() << "\n";
	}
	
	// load splashscreen texture
	
	splashTexture = LoadSVGTriangles(getExecutableDir()+"/splashscreen.triangles");
	
//	auto stuff = prepareTexture(getExecutableDir()+"/splashscreen.png");
//	splashTexture = stuff.tex;
//	splashW = stuff.imgW;
//	splashH = stuff.imgH;
	
	// restore full screened
	
	if (settings->getValue("window_maximized", false)) {
		win_button();
	}
	
	if (settings->getValue("lm_load_model_on_start", false)) {
		Curler::loadModel();
	}
	
	std::thread t(checkForUpdates);
	t.detach();
	
	updateTransparency(settings->getValue("use_transparency", false));
	
	
	
	// if (settings->getValue("use_chauffeur", false)) {
	// 	std::thread starter([&](){
	// 		ModelXRunner::load();
	// 	});
	// 	starter.detach();
	// }
	
	if (isNewer(version, {settings->getValue("version_major", 0), settings->getValue("version_minor", 0), settings->getValue("version_patch", 0)})) {
		fixUpLanguages();
		settings->setValue("version_major", major_version);
		settings->setValue("version_minor", minor_version);
		settings->setValue("version_patch", patch_version);
	}
	
	return true;
}

void App::updateTransparency(bool transparent) {
#ifdef _WIN32
	HMODULE hUser = GetModuleHandleW(L"user32.dll");
	if (!hUser) return;

	auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
		GetProcAddress(hUser, "SetWindowCompositionAttribute")
	);
	if (!setWCA) return;

	ACCENT_POLICY accent = {};
	if (transparent) {
		accent.accentState = ACCENT_ENABLE_BLURBEHIND;
	} else {
		accent.accentState = ACCENT_DISABLED;
	}

	WINDOWCOMPOSITIONATTRIBDATA data = {};
	data.Attribute  = WCA_ACCENT_POLICY;
	data.Data       = &accent;
	data.SizeOfData = sizeof(accent);

	setWCA(window_handle, &data);
#endif
}

bool App::isNewer(std::vector<int> check, std::vector<int> current) { // checks if provided vector is newer than current
	int f1 = check[0]-current[0];
	int f2 = check[1]-current[1];
	int f3 = check[2]-current[2];
	
	return (f1 > 0 || (f1 == 0 && f2 > 0) || (f1 == 0 && f2 == 0 && f3 > 0));
}

void App::checkForUpdates() {
	std::vector<int> latest = UpdateChecker::getLatestVersion();
	if (latest.size() != 3) {
		return;
	}
	
	int f1 = latest[0]-major_version;
	int f2 = latest[1]-minor_version;
	int f3 = latest[2]-patch_version;
	
	if (isNewer(latest, version)) {
		displayToast(MST::toMonoString("There is a new version of CodeWizard available!"));
	}else if (isNewer(version, latest)) {
		displayToast(MST::toMonoString("This CodeWizard is ahead of the latest release!"));
	}else{
//		displayToast(MST::toMonoString("This is the latest release!"));
	}
}

LanguageServerClient* App::readyLSP(std::string lsp_command) {
	if (lsp_client_map.find(lsp_command) != lsp_client_map.end()) {
		return lsp_client_map[lsp_command];
	}
	
	lsp_client_map[lsp_command] = new LanguageServerClient(lsp_command, [](const std::string& msg) {
		std::cout << "LOG_LSP: " << msg << std::endl;
	});
	
	std::string folder = settings->getValue("current_folder", getExecutableDir());
	lsp_client_map[lsp_command]->initialize(folder);
	
	return lsp_client_map[lsp_command];
}

void App::openLanguagesFile() {
	std::string path = settings->getLocalAppDataPath() + "/CodeWizard/languages.json";
	openFromCMD(path, "languages.json");
	displayToast(MST::toMonoString("Remember to reopen CodeWizard after making changes."));
}

void App::restartLSPs() {
	for (auto itm : lsp_client_map) {
		if (!itm.second) { continue; }
		
		itm.second->shutdown();
		delete itm.second;
	}
	
	lsp_client_map = {};
	
	rootelement->executeAction(WidgetActionType::RESTART_LSP);
}

void App::adding_panel() {
	rerender = true;
	reclear = 3;
	curr_removing_panel = false;
	curr_adding_panel = true;
}

void App::removing_panel() {
	rerender = true;
	reclear = 3;
	curr_adding_panel = false;
	curr_removing_panel = true;
}

void App::nada_panel() {
	rerender = true;
	reclear = 3;
	curr_removing_panel = false;
	curr_adding_panel = false;
}

#ifdef _WIN32
LRESULT CALLBACK App::CustomWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_NCHITTEST: {
			POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			ScreenToClient(hwnd, &pt);
			
			RECT rect;
			GetClientRect(hwnd, &rect);
			int windowWidth = rect.right - rect.left;
			int windowHeight = rect.bottom - rect.top;
			
			const int BORDER_THICKNESS = 8;
			
			bool isZoomed = IsZoomed(hwnd);
			
			if ((pt.y > BORDER_THICKNESS || isZoomed) && pt.y < tb->t_h && tb->is_out_of_child(pt.x)) {
				return HTCAPTION;
			}
			
			if (isZoomed) {
				return HTCLIENT;
			}
			
			if (pt.x < BORDER_THICKNESS && pt.y < BORDER_THICKNESS) {
				return HTTOPLEFT;
			}
			if (pt.x > windowWidth - BORDER_THICKNESS && pt.y < BORDER_THICKNESS) {
				return HTTOPRIGHT;
			}
			if (pt.x < BORDER_THICKNESS && pt.y > windowHeight - BORDER_THICKNESS) {
				return HTBOTTOMLEFT;
			}
			if (pt.x > windowWidth - BORDER_THICKNESS && pt.y > windowHeight - BORDER_THICKNESS) {
				return HTBOTTOMRIGHT;
			}
			
			if (pt.x < BORDER_THICKNESS) {
				return HTLEFT;
			}
			if (pt.x > windowWidth - BORDER_THICKNESS) {
				return HTRIGHT;
			}
			if (pt.y < BORDER_THICKNESS) {
				return HTTOP;
			}
			if (pt.y > windowHeight - BORDER_THICKNESS) {
				return HTBOTTOM;
			}
			
			return HTCLIENT;
		}
		
		case WM_PAINT: {
			rerender = true;
			DoFullRenderWithoutInput();
			ValidateRect(hwnd, NULL);
			return 0;
		}

		case WM_SETCURSOR: {
			if (IsZoomed(hwnd)) {
				return CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
			}
			
			WORD ht = LOWORD(lParam);
			HCURSOR hCur = nullptr;
			switch (ht) {
			case HTTOP:      hCur = LoadCursor(NULL, IDC_SIZENS);   break;
			case HTBOTTOM:   hCur = LoadCursor(NULL, IDC_SIZENS);   break;
			case HTLEFT:     hCur = LoadCursor(NULL, IDC_SIZEWE);   break;
			case HTRIGHT:    hCur = LoadCursor(NULL, IDC_SIZEWE);   break;
			case HTTOPLEFT:  hCur = LoadCursor(NULL, IDC_SIZENWSE); break;
			case HTTOPRIGHT: hCur = LoadCursor(NULL, IDC_SIZENESW); break;
			case HTBOTTOMLEFT:  hCur = LoadCursor(NULL, IDC_SIZENESW); break;
			case HTBOTTOMRIGHT: hCur = LoadCursor(NULL, IDC_SIZENWSE); break;
			default:
				return CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
			}
			SetCursor(hCur);
			return TRUE;
		} case WM_NCCALCSIZE: {
			if (!wParam) { break; }
			return 0;
		}case WM_GETMINMAXINFO: {
			auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
		
			HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = {};
			mi.cbSize = sizeof(mi);
			GetMonitorInfoW(hm, &mi);
		
			const RECT& rcWork = mi.rcWork;
			const RECT& rcMon  = mi.rcMonitor;
		
			mmi->ptMaxPosition.x = rcWork.left - rcMon.left;
			mmi->ptMaxPosition.y = rcWork.top  - rcMon.top;
		
			mmi->ptMaxSize.x = rcWork.right  - rcWork.left;
			mmi->ptMaxSize.y = rcWork.bottom - rcWork.top;
			
			mmi->ptMinTrackSize.x = 500;
			mmi->ptMinTrackSize.y = 100;
		
			mmi->ptMaxTrackSize = mmi->ptMaxSize;
			return 0;
		}case WM_MOVING: {
		}case WM_SIZING: {
			CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
		
			RECT* rc = reinterpret_cast<RECT*>(lParam);
			int newW = rc->right  - rc->left;
			int newH = rc->bottom - rc->top;
		
			App::resize_callback(App::window, newW, newH);
			InvalidateRect(hwnd, NULL, FALSE);
		
			return TRUE;
		}case WM_DPICHANGED: {
			const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
			SetWindowPos(hwnd, NULL,
				suggested->left,
				suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}case WM_NCMOUSEMOVE: {
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hwnd, &pt);
			InvalidateRect(hwnd, NULL, FALSE); 
			App::cursor_position_callback(window, pt.x, pt.y);
			return 0;
		}
	}
	
	return CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
}
#endif

auto drawCorner = [](float cx, float cy, float startAngle, float endAngle, int segments, double radius) {
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(cx, cy);
	for (int i = 0; i <= segments; ++i) {
		float t = (float)i / (float)segments;
		float theta = startAngle + t * (endAngle - startAngle);
		glVertex2f(cx + std::cos(theta) * radius, cy + std::sin(theta) * radius);
	}
	glEnd();
};

auto drawInverseCorner = [](float cx, float cy, float startAngle, float endAngle, int segments, double radius) {
	double biggerrad = radius*App::SQRT_2;
	float center = (startAngle+endAngle)/2;
	
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(cx+std::cos(center)*biggerrad, cy+std::sin(center)*biggerrad);
	
	for (int i = 0; i <= segments; ++i) {
		float t = (float)i / (float)segments;
		float theta = startAngle + t * (endAngle - startAngle);
		glVertex2f(cx + std::cos(theta) * radius, cy + std::sin(theta) * radius);
	}
	glEnd();
};

auto drawCornerEdge = [](float cx, float cy, float startAngle, float endAngle, int segments, double radius, int edgewidth) {
	double smallerradius = radius-edgewidth;
	
	glBegin(GL_QUAD_STRIP);
	for (int i = 0; i <= segments; ++i) {
		float t = (float)i / (float)segments;
		float theta = startAngle + t * (endAngle - startAngle);
		glVertex2f(cx + std::cos(theta) * radius, cy + std::sin(theta) * radius);
		glVertex2f(cx + std::cos(theta) * smallerradius, cy + std::sin(theta) * smallerradius);
	}
	glEnd();
};

void App::DrawRoundedRect(float x, float y, float w, float h, float radius, Color* color, bool border, int segments) {
	glColor4f(color->r, color->g, color->b, color->a);
	
	// Draw the center and side/quadrant straight regions as quads
	glBegin(GL_QUADS);
		// Center
		glVertex2f(x + radius,      y);
		glVertex2f(x + w - radius,  y);
		glVertex2f(x + w - radius,  y + h);
		glVertex2f(x + radius,      y + h);

		// Left strip
		glVertex2f(x,          y + radius);
		glVertex2f(x + radius, y + radius);
		glVertex2f(x + radius, y + h - radius);
		glVertex2f(x,          y + h - radius);

		// Right strip
		glVertex2f(x + w - radius, y + radius);
		glVertex2f(x + w,          y + radius);
		glVertex2f(x + w,          y + h - radius);
		glVertex2f(x + w - radius, y + h - radius);
	glEnd();

	// Draw the four quartercircles
	drawCorner(x + radius, y + radius, M_PI, 1.5f * M_PI, segments, radius); // BL
	drawCorner(x + w - radius, y + radius, 1.5f * M_PI, 2.0f * M_PI, segments, radius); // BR
	drawCorner(x + w - radius, y + h - radius, 0.0f, 0.5f * M_PI, segments, radius); // TR
	drawCorner(x + radius,      y + h - radius, 0.5f * M_PI, M_PI, segments, radius); // TL
	
	if (border) {
		DrawRoundBorder(x, y, w, h, theme.border, segments, radius);
	}
}

void App::DrawInverseRoundedRect(float x, float y, float w, float h, float radius, Color* color, int segments) {
	glColor4f(color->r, color->g, color->b, color->a);
	drawInverseCorner(x + radius, y + radius, M_PI, 1.5f * M_PI, segments, radius); // BL
	drawInverseCorner(x + w - radius, y + radius, 1.5f * M_PI, 2.0f * M_PI, segments, radius); // BR
	drawInverseCorner(x + w - radius, y + h - radius, 0.0f, 0.5f * M_PI, segments, radius); // TR
	drawInverseCorner(x + radius,      y + h - radius, 0.5f * M_PI, M_PI, segments, radius); // TL
}

void App::DrawRect(int x, int y, int w, int h, Color* color) {
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_QUADS);
		glVertex2f(x, y); // top-left
		glVertex2f(x+w, y); // top-right
		glVertex2f(x+w, y+h); // bottom-right
		glVertex2f(x, y+h); // bottom-left
	glEnd();
}

ImageInfo App::prepareTexture(std::string imagepath, std::shared_ptr<FileBackend> backend) {
	int channels;
	int imgW;
	int imgH;
	
	unsigned char* data = nullptr;
	std::vector<std::uint8_t> remote_bytes;
	if (backend && backend->isRemote()) {
		std::string error;
		if (backend->readFile(imagepath, remote_bytes, error) &&
			remote_bytes.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
			data = stbi_load_from_memory(
				remote_bytes.data(), static_cast<int>(remote_bytes.size()),
				&imgW, &imgH, &channels, STBI_rgb_alpha);
		}
	} else {
		data = stbi_load(
			imagepath.c_str(),
			&imgW, &imgH, &channels,
			STBI_rgb_alpha
		);
	}
	
	if (!data) {
		return {(GLuint)-1, 0, 0};
	}
	
	// Premultiply alpha to prevent bright/white edges (halo artifact) during filtering/mipmapping.
	// Optimized using fast integer math and branch short-circuiting.
	int numPixels = imgW * imgH;
	for (int i = 0; i < numPixels; ++i) {
		unsigned int a = data[i * 4 + 3];
		if (a == 255) {
			continue; // Opaque pixels are already correct; skip to save work
		}
		if (a == 0) {
			// Zero out colors for fully transparent pixels
			data[i * 4 + 0] = 0;
			data[i * 4 + 1] = 0;
			data[i * 4 + 2] = 0;
			continue;
		}
		
		unsigned int r = data[i * 4 + 0];
		unsigned int g = data[i * 4 + 1];
		unsigned int b = data[i * 4 + 2];
		
		// Highly accurate fast division by 255 using integer shift math: (t + (t >> 8)) >> 8
		unsigned int tr = r * a + 128;
		unsigned int tg = g * a + 128;
		unsigned int tb = b * a + 128;
		
		data[i * 4 + 0] = (unsigned char)((tr + (tr >> 8)) >> 8);
		data[i * 4 + 1] = (unsigned char)((tg + (tg >> 8)) >> 8);
		data[i * 4 + 2] = (unsigned char)((tb + (tb >> 8)) >> 8);
	}
	
	GLuint texID;
	// Generate GL texture
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(
		GL_TEXTURE_2D, 0, GL_RGBA,
		imgW, imgH, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, data
	);
	
	// Build mip chain so minification samples a lower-res level instead
	// of point/linear-sampling the full-res image (avoids shimmering/aliasing
	// when the texture is drawn smaller than its source size).
	bool mipmapped = doMipmapThing();
	
	if (mipmapped) {
		// Trilinear filtering: smooth blend between mip levels and within each level.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	} else {
		// Fallback to bilinear filtering if mipmaps could not be generated.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	// Prevents edge bleed/wrap artifacts at smaller mip levels for non-tiling images.
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(data);
	
	return {texID, imgW, imgH};
}

void App::renderColorlessTexture(GLuint texID, int x, int y, int w, int h, Color* foreground, Color* background) {
	if (texID == (GLuint)-1) { return; }

	glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texID);

	glColor4f(foreground->r,
			  foreground->g,
			  foreground->b,
			  foreground->a);

	float blendColor[] = { background->r, background->g, background->b, 1.0f };
	glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blendColor);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);

	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

	glDisable(GL_BLEND);

	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f); glVertex2f(x,   y+h);
		glTexCoord2f(1.0f, 1.0f); glVertex2f(x+w, y+h);
		glTexCoord2f(1.0f, 0.0f); glVertex2f(x+w, y);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(x,   y);
	glEnd();

	glPopAttrib();
}

void App::renderTexture(
	GLuint texID,
	int x,
	int y,
	int w,
	int h,
	const Color* background,
	float alpha
) {
	if (texID == (GLuint)-1 || background == nullptr) {
		return;
	}
	glPushAttrib(
		GL_TEXTURE_BIT |
		GL_ENABLE_BIT |
		GL_CURRENT_BIT |
		GL_COLOR_BUFFER_BIT
	);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texID);
	// Scale all channels together since blending uses premultiplied alpha
	glColor4f(alpha, alpha, alpha, alpha);
	// Enable standard blending with premultiplied alpha
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f);
		glVertex2f(x, y + h);
		glTexCoord2f(1.0f, 1.0f);
		glVertex2f(x + w, y + h);
		glTexCoord2f(1.0f, 0.0f);
		glVertex2f(x + w, y);
		glTexCoord2f(0.0f, 0.0f);
		glVertex2f(x, y);
	glEnd();
	glPopAttrib();
}

void App::DrawCircle(int x, int y, int radius, int segments, Color* color) {
	float angleStep = 2.0f * 3.1415926f / segments;

	glColor4f(color->r, color->g, color->b, color->a);
	
	// GL_TRIANGLE_FAN creates a filled circle
	// The first vertex is the center, subsequent vertices are on the circumference
	glBegin(GL_TRIANGLE_FAN);
		glVertex2f(x, y); // Center point
		for (int i = 0; i <= segments; i++) {
			float angle = i * angleStep;
			float vx = x + cos(angle) * radius;
			float vy = y + sin(angle) * radius;
			glVertex2f(vx, vy);
		}
	glEnd();
}

void App::DrawLine(float x1, float y1, float x2, float y2, float width, Color* color) {
	glLineWidth(width);

	// Set the color
	glColor4f(color->r, color->g, color->b, color->a);

	glBegin(GL_LINES);
		glVertex2f(x1, y1);
		glVertex2f(x2, y2);
	glEnd();

	glLineWidth(1.0f);
}

void App::DrawX(double x, double y, double w, double h, double thickness, Color* color) {
	double sqrtt = sqrt(thickness);
	
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_QUADS);
		glVertex2f(x+sqrtt, y); // top-left
		glVertex2f(x+w, y+h-sqrtt); // top-right
		glVertex2f(x+w-sqrtt, y+h); // bottom-right
		glVertex2f(x, y+sqrtt); // bottom-left
		
		glVertex2f(x+w-sqrtt, y);
		glVertex2f(x, y+h-sqrtt);
		glVertex2f(x+sqrtt, y+h);
		glVertex2f(x+w, y+sqrtt);
	glEnd();
}

void App::DrawPlus(double x, double y, double w, double h, double thickness, Color* color) {
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_QUADS);
		glVertex2f(x+w/2-thickness/2, y); // top-(center-left)
		glVertex2f(x+w/2+thickness/2, y); // top-(center-right)
		glVertex2f(x+w/2+thickness/2, y+h); // bottom-(center-right)
		glVertex2f(x+w/2-thickness/2, y+h); // bottom-(center-left)
		
		glVertex2f(x, y+h/2-thickness/2); // (center-top)-left
		glVertex2f(x+w, y+h/2-thickness/2); // (center-top)-right
		glVertex2f(x+w, y+h/2+thickness/2); // (center-bottom)-right
		glVertex2f(x, y+h/2+thickness/2); // (center-bottom)-left
	glEnd();
}

void App::DrawMinus(double x, double y, double w, double h, double thickness, Color* color) {
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_QUADS);
		glVertex2f(x, y+h/2-thickness/2); // (center-top)-left
		glVertex2f(x+w, y+h/2-thickness/2); // (center-top)-right
		glVertex2f(x+w, y+h/2+thickness/2); // (center-bottom)-right
		glVertex2f(x, y+h/2+thickness/2); // (center-bottom)-left
	glEnd();
}

void App::DrawSquare(double x, double y, double w, double h, double thickness, Color* color) {
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_QUADS);
		glVertex2f(x, y); // top-left
		glVertex2f(x+w, y); // top-right
		glVertex2f(x+w, y+thickness); // bottom-right
		glVertex2f(x, y+thickness); // bottom-left
		
		glVertex2f(x, y+h-thickness); // top-left
		glVertex2f(x+w, y+h-thickness); // top-right
		glVertex2f(x+w, y+h); // bottom-right
		glVertex2f(x, y+h); // bottom-left
		
		glVertex2f(x, y); // top-left
		glVertex2f(x+thickness, y); // top-right
		glVertex2f(x+thickness, y+h); // bottom-right
		glVertex2f(x, y+h); // bottom-left
		
		glVertex2f(x+w-thickness, y); // top-left
		glVertex2f(x+w, y); // top-right
		glVertex2f(x+w, y+h); // bottom-right
		glVertex2f(x+w-thickness, y+h); // bottom-left
	glEnd();
}

void App::DrawRoundBorder(int x, int y, int w, int h, Color* color, int segments, double radius) {
	glColor4f(color->r, color->g, color->b, color->a);
	
	drawCornerEdge(x + radius, y + radius, M_PI, 1.5f * M_PI, segments, radius, border_width); // BL
	drawCornerEdge(x + w - radius, y + radius, 1.5f * M_PI, 2.0f * M_PI, segments, radius, border_width); // BR
	drawCornerEdge(x + w - radius, y + h - radius, 0.0f, 0.5f * M_PI, segments, radius, border_width); // TR
	drawCornerEdge(x + radius,      y + h - radius, 0.5f * M_PI, M_PI, segments, radius, border_width); // TL
	
	DrawRect(x+radius, y, w-radius*2, border_width, color);
	DrawRect(x+radius, y+h-border_width, w-radius*2, border_width, color);
	DrawRect(x, y+radius, border_width, h-radius*2, color);
	DrawRect(x+w-border_width, y+radius, border_width, h-radius*2, color);
}

void App::DrawBorder(int x, int y, int w, int h, Color* color) {
	DrawRect(x, y, w, border_width, color);
	DrawRect(x, y, border_width, h, color);
	DrawRect(x+w-border_width, y, border_width, h, color);
	DrawRect(x, y+h-border_width, w, border_width, color);
}

void App::DrawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
	glColor4f((float)r/255.0f, (float)g/255.0f, (float)b/255.0f, 1.0f);
	glBegin(GL_QUADS);
		glVertex2f(x, y); // top-left
		glVertex2f(x+w, y); // top-right
		glVertex2f(x+w, y+h); // bottom-right
		glVertex2f(x, y+h); // bottom-left
	glEnd();
}

void App::DrawAsteroid(int x, int y, int s, double r, Color* color, int type) {
	glPushMatrix();

	// Move to position and rotate
	glTranslatef(x + s / 2.0f, y + s / 2.0f, 0.0f);
	glRotatef(r * (180.0f / 3.14159265f), 0.0f, 0.0f, 1.0f);
	
	// Scale the points by the size parameter
	glScalef((float)s, (float)s, 1.0f);

	glColor4f(color->r, color->g, color->b, color->a);

	// Pick a shape based on the type
	glBegin(GL_POLYGON);
	if (type == 0) {
		// Type 0: Standard lumpy rock
		glVertex2f(-0.5f, -0.2f);
		glVertex2f(-0.3f, -0.5f);
		glVertex2f( 0.2f, -0.4f);
		glVertex2f( 0.5f, -0.1f);
		glVertex2f( 0.4f,  0.3f);
		glVertex2f( 0.0f,  0.5f);
		glVertex2f(-0.4f,  0.4f);
	} else if (type == 1) {
		// Type 1: Elongated, sharp rock
		glVertex2f(-0.2f, -0.5f);
		glVertex2f( 0.3f, -0.4f);
		glVertex2f( 0.5f,  0.0f);
		glVertex2f( 0.2f,  0.5f);
		glVertex2f(-0.4f,  0.3f);
		glVertex2f(-0.5f, -0.1f);
	} else {
		// Type 2: C-shaped / Cratered rock
		glVertex2f(-0.3f, -0.3f);
		glVertex2f( 0.0f, -0.5f);
		glVertex2f( 0.4f, -0.3f);
		glVertex2f( 0.3f,  0.0f);
		glVertex2f( 0.5f,  0.3f);
		glVertex2f( 0.1f,  0.5f);
		glVertex2f(-0.4f,  0.4f);
		glVertex2f(-0.5f,  0.0f);
	}
	glEnd();

	glPopMatrix();
}

Color* App::MixColors(const Color* bg, const Color* fg, float factor) {
	theme.temp -> r = bg->r * (1.0f - factor) + fg->r * factor;
	theme.temp -> g = bg->g * (1.0f - factor) + fg->g * factor;
	theme.temp -> b = bg->b * (1.0f - factor) + fg->b * factor;
	theme.temp -> a = bg->a * (1.0f - factor) + fg->a * factor;
	return theme.temp;
}

Color* App::MakeTransparentColor(const Color* fg, float factor) {
	theme.temp -> r = fg->r;
	theme.temp -> g = fg->g;
	theme.temp -> b = fg->b;
	theme.temp -> a = fg->a * factor;
	return theme.temp;
}

void App::DrawSVG(const std::vector<Vec2>& vertices, int x, int y, int w, int h, Color* color, double r) {
	glPushMatrix();

	glTranslatef(x + w / 2.0f, y + h / 2.0f, 0.0f);
	glRotatef(r * (180.0f / 3.14159265f), 0.0f, 0.0f, 1.0f);
	glScalef((float)w, (float)h, 1.0f);

	glColor4f(color->r, color->g, color->b, color->a);

	glBegin(GL_TRIANGLES);
	for (const auto& v : vertices) {
		glVertex2f(v.x, v.y);
	}
	glEnd();
	
	glPopMatrix();
}

void App::DrawShip(int x, int y, int s, double r, Color* color, bool drawfire) {
	glPushMatrix();
	
	glTranslatef(x + s / 2.0f, y + s / 2.0f, 0.0f);
	glRotatef(r * (180.0f / 3.14159265f), 0.0f, 0.0f, 1.0f);
	
	glScalef((float)s, (float)s, 1.0f);
	
	if (drawfire) {
		glColor4f(1.0f, 0.5f, 0.0f, color->a); // Defaulting to Orange
		
		glBegin(GL_POLYGON);
			glVertex2f(-0.3f,  0.0f);  // Starts at the rear indent
			glVertex2f(-0.5f,  0.2f);  // Widens slightly
			glVertex2f(-0.8f,  0.0f);  // Tip of the flame (beyond ship width)
			glVertex2f(-0.5f, -0.2f);  // Widens slightly
		glEnd();
	}
	
	glColor4f(color->r, color->g, color->b, color->a);
	glBegin(GL_POLYGON);
		glVertex2f( 0.5f,  0.0f);  // Nose
		glVertex2f(-0.5f,  0.4f);  // Top-back wing
		glVertex2f(-0.3f,  0.0f);  // Rear indent
		glVertex2f(-0.5f, -0.4f);  // Bottom-back wing
	glEnd();
	
	glPopMatrix();
}

void App::DoFullRenderWithoutInput() {
#ifdef _WIN32
	if (curr_adding_panel || curr_removing_panel) {
		reclear = 3;
	}
#else
	reclear = 2;
#endif
	
	double currentTime = glfwGetTime();
	frameCount++;
	
	if (currentTime - lastTime >= 5.0) {
		float fps = (double)frameCount/(currentTime-lastTime);
		
		std::string fps_str = std::to_string(fps);
		
		std::cout << "FPS: " << fps_str << std::endl;
		frameCount = 0;
		lastTime = currentTime;
		
		if (settings->getValue("show_fps", false)) {
			displayText(MST::toMonoString("FPS: " + fps_str));
		}
	}
	
	if (moveMouseToX != -1 && moveMouseToY != -1) {
		glfwSetCursorPos(window, moveMouseToX, moveMouseToY);
		moveMouseToX = -1;
		moveMouseToY = -1;
	}
	
	rendering_add_rect = false;
	rendering_rem_rect = false;
	
	{
		std::lock_guard<std::mutex> lock(storedsearches.mtx);
		if (storedsearches.updateSomething) {
			if (ListBox* lb = dynamic_cast<ListBox*>(commandBox)) {
				for (int i = storedsearches.added_already; i < storedsearches.storedsearches.size(); i++) {
					lb->elements.push_back(MST::toMonoString(storedsearches.storedsearches[i].text));
					INDEXED_FILES.currentlyshowingtype.push_back(2);
					INDEXED_FILES.currentlyshowing.push_back(i);
				}
				
				if (lb->elements.size() != 0) {
					lb->elements[0] = MST::toMonoString(storedsearches.toptext);
				}
			}
			
			storedsearches.added_already = storedsearches.storedsearches.size();
			storedsearches.updateSomething = false;
			time_till_regular = 2;
		}
	}
	
	expectedCursorType = -1; // must be reset every position call
	std::unique_lock<std::mutex> lock(canMakeChanges); // this prevents separate threads (the lsp clients) from messing with shit while positioning/rendering
	
	for (auto w : all_widgets) { // already sorted by seniority because parents are first
		w->prepare(mouseX, mouseY); // technically the cursor checks can be off by one frame because of this being before position and not at the same time
	}
	
	if (rootelement) {
		rootelement->position(text_padding, tb->t_h, WINDOW_WIDTH-text_padding*2, WINDOW_HEIGHT-tb->t_h-text_padding);
		toastBox->position(0, tb->t_h, WINDOW_WIDTH, WINDOW_HEIGHT-tb->t_h);
	}
	
	if (expectedCursorType != currentCursorType) {
		if ((expectedCursorType == 0 || expectedCursorType == -1) && regularCursor) {
			glfwSetCursor(window, regularCursor);
		}else if (expectedCursorType == 1 && hResizeCursor) {
			glfwSetCursor(window, hResizeCursor);
		}else if (expectedCursorType == 2 && vResizeCursor) {
			glfwSetCursor(window, vResizeCursor);
		}else if (expectedCursorType == 3 && handCursor) {
			glfwSetCursor(window, handCursor);
		}else if (expectedCursorType == 4 && textCursor) {
			glfwSetCursor(window, textCursor);
		}
		currentCursorType = expectedCursorType;
	}
	
	if (recheckmenusizing) {
		checkMenubarVisibility();
	}
	
	//render
	
	
	if (!rerender && time_till_regular == 0) {
		if (forceWaitTime && !replaying_macro) { // we don't want to hold up operations happening over multiple frames (think highlighting out of view lines that we don't need to re-render for)
			int time = 10;
			
			if (currentTime-lastUpdate > 5) { 
				time = 140; // go to 'sleep' if we haven't updated anything in the last x seconds this is roughly 7 fps
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(time));
		}else {
			lastUpdate = currentTime;
		}

		return;
	}
	
	time_till_regular -= 1;
	if (time_till_regular < 0) {
		time_till_regular = 0;
	}
	
	if (pending_window_title != "") {
		glfwSetWindowTitle(window, pending_window_title.c_str());
		pending_window_title = "";
	}
	
	lastUpdate = currentTime;
	
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); // we need to overwrite everything now (rgb and a)
	
	if (settings->getValue("use_transparency", false)) {
		glClearColor(bgcolor->r, bgcolor->g, bgcolor->b, settings->getValue("opacity", 0.65f)); // reduce opacity to "opacity"
	}else{
		glClearColor(bgcolor->r, bgcolor->g, bgcolor->b, 1.0f);
	}
	
	
	if (settings->getValue("use_transparency", false) != last_transparency_w_clear) {
		reclear = 3;
	}
	
	
	if (reclear != 0) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		last_transparency_w_clear = settings->getValue("use_transparency", false);
		// change reclear to false later
	}
	
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); // don't let anything else touch alpha
	
	glEnable(GL_SCISSOR_TEST);
	
	SKIZ_X = 0;
	SKIZ_Y = 0;
	SKIZ_W = WINDOW_WIDTH;
	SKIZ_H = WINDOW_HEIGHT;
	
	glScissor(SKIZ_X, SKIZ_Y, SKIZ_W, SKIZ_H);
	
	if (rootelement) {
		rootelement->render();
		runWithSKIZ(toastBox->t_x, toastBox->t_y, toastBox->t_w, toastBox->t_h, []() {
			toastBox->render();
		});
		
		if (rendering_add_rect) {
			DrawRoundedRect(x_nb_current, y_nb_current, w_nb_current, h_nb_current, App::text_padding, theme.add_panel);
		}else if (rendering_rem_rect) {
			DrawRoundedRect(x_nb_current, y_nb_current, w_nb_current, h_nb_current, App::text_padding, theme.remove_panel);
		}
	}
	
	glDisable(GL_SCISSOR_TEST);
	if (reclear != 0) {
		reclear -= 1;
	}
	
	lock.unlock(); // swap takes a while (due to vsync), so we can unlock the mutex ahead of time
	
	glfwSwapBuffers(window);
}

void App::setTextEditHighlighter(Widget* w, std::string name) {
	auto te = dynamic_cast<TextEdit*>(w);
	if (!te) {
		return;
	}
	
	if (!name.empty()) {
		auto it = App::highlighters.find(name);
		if (it == App::highlighters.end()) {
			te->highlighter = cw_syntect_setup(
				name.c_str(),
				nullptr,
				0
			);
			
			App::highlighters[name] = te->highlighter;
		}else{
			te->highlighter = it->second;
		}
		
		if (te->highlighter != nullptr) {
			te->highlighter_initial_state.reset(cw_syntect_initial_state(te->highlighter));
		}
	}
}

void App::MoveWidget(Widget* w, Widget* new_parent) {
	rerender = true;
	
	RemoveWidgetFromParent(w);
	w->parent = new_parent;
	w->const_parent = new_parent;
	new_parent->children.push_back(w);
}

void App::deleteWidget(Widget* w) {
	std::vector<int> toDelete = {};
	
	for (int i = 0; i < all_widgets.size(); i++) {
		if (all_widgets[i] == w) {
			toDelete.push_back(i);
			break;
		}
	}
	
	while (true) {
		bool foundOne = false;
		std::vector<int> newIndices;
		for (int i = 0; i < all_widgets.size(); i++) {
			bool alreadyInToDelete = false;
			for (int w_indx : toDelete) {
				if (i == w_indx) {
					alreadyInToDelete = true;
					break;
				}
			}
			if (alreadyInToDelete) {
				continue;
			}
			for (int w_indx : toDelete) {
				if (all_widgets[i]->const_parent == all_widgets[w_indx]) {
					foundOne = true;
					newIndices.push_back(i);
					break;
				}
			}
		}
		if (!foundOne) {
			break;
		}
		toDelete.insert(toDelete.end(), newIndices.begin(), newIndices.end());
	}
	
	std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
	
	for (int i : toDelete) {
		if (all_widgets[i]->parent != nullptr) {
			RemoveWidgetFromParent(all_widgets[i]);
		}
	}
	
	for (int i : toDelete) {
		Widget* wDelete = all_widgets[i];
		if (wDelete->before_self_close) {
			wDelete->before_self_close();
		}
	}

	for (int i : toDelete) {
		Widget* wDelete = all_widgets[i];
		if (wDelete == activeLeafNode) {
			activeLeafNode = nullptr;
		}
		all_widgets.erase(all_widgets.begin()+i);
		delete wDelete;
	}
}

int App::GetWidgetIndexInParent(Widget* w) {
	for (int i = 0; i < w->parent->children.size(); i++) {
		auto c = w->parent->children[i];
		
		if (c == w) {
			return i;
		}
	}
	return -1;
}

void App::RemoveWidgetFromParent(Widget* w) {
	rerender = true;
	
	if (w->parent){
		int indx = GetWidgetIndexInParent(w);
		if (indx != -1){
			w->parent->children.erase(w->parent->children.begin()+indx);
		}
	}
	w->parent = nullptr;
}

void App::ReplaceWith(Widget* existing, Widget* replacement) {
	rerender = true;
	
	auto newprnt = existing->parent;
	
	int indx = GetWidgetIndexInParent(existing);
	
	RemoveWidgetFromParent(replacement);
	RemoveWidgetFromParent(existing);
	
	MoveWidget(replacement, newprnt);
	
	if (indx == 0 && newprnt->children.size() > 1) {
		std::swap(newprnt->children[0], newprnt->children[newprnt->children.size()-1]);
	}
}

void App::Run() {
	STRING_REQUEST_RECTANGLE->background_color = theme.overlay_background_color;
	STRING_REQUEST_TEXTEDIT->background_color = theme.overlay_background_color;
	STRING_REQUEST_LABEL->background_color = theme.overlay_background_color;
	
	commandUnfocused(); // sets the command bar text to start with
	
	// Main loop
	while (!glfwWindowShouldClose(window)) {
		// Handle input/events
		rerender = false;
		forceWaitTime = true;
		if (!keyboard_events.back().empty()) {
			keyboard_events.push_back({});
		}
		
		glfwPollEvents();
		
		if (replaying_macro) {
			if (keyboard_events.size() == 0) {
				glfwSwapInterval(1); // Enable vsync
				replaying_macro = false;
			} else{
				if (current_step >= keyboard_events.size()) {
					current_step = 0;
					if (rep_count > 0) {
						rep_count -= 1;
					}
				}
				if (rep_count != 0) {
					current_step = current_step%keyboard_events.size();
					std::vector<KeyboardEvent> es = keyboard_events.at(current_step);
					for (KeyboardEvent e : es) {
						if (e.character_callback) {
							character_callback(e.window, e.cc_codepoint);
						}else{
							key_callback(e.window, e.key, e.scancode, e.action, e.mods);
						}
					}
					current_step++;
				}else{
					glfwSwapInterval(1); // Enable vsync
					replaying_macro = false;
					displayToast(MST::toMonoString("Finished Executing Macro"));
				}
			}
		}
		DoFullRenderWithoutInput();
	}
	
	std::cout << "Saving..." << std::endl;
	
	save(); // save before exit.
	
	for (auto c : rootelement->children) {
		if (auto pe = dynamic_cast<PanelHolder*>(c)) {
			settings->saveConfig(pe->saveConfiguration());
			break;
		}
	}

	bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
	bool iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
	
	settings->setValue("window_maximized", maximized);
	
	if (!maximized && !iconified) {
		int x, y, w, h;
		glfwGetWindowPos(window, &x, &y);
		glfwGetWindowSize(window, &w, &h);
		
		settings->setValue("window_x",      x);
		settings->setValue("window_y",      y);
		settings->setValue("window_width",  w);
		settings->setValue("window_height", h);
	}
	
	glfwDestroyWindow(window);
	glfwTerminate();
}

void App::cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
	rerender = true;
	
	mouseX = xpos;
	mouseY = ypos;
	
	if (on_mouse_move_event) {
		on_mouse_move_event();
	}
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_mouse_move_event()) { return; }
	}
	
	if (rootelement) {
		rootelement->on_mouse_move_event();
	}
}

void App::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
	rerender = true;
	
	// action - GLFW_PRESS/GLFW_RELEASE/GLFW_REPEAT
	// button - GLFW_MOUSE_BUTTON(LEFT/RIGHT)
	
	if (on_mouse_button_event) {
		if (on_mouse_button_event(button, action, mods)) { return; }
	}
	
	if (menu->parent != nullptr) {
		if (menu->cursor_in_this) {
			menu->on_mouse_button_event(button, action, mods);
			return;
		}else if (action == GLFW_PRESS && mouseY > tb->t_h){
			closeMenu(); // don't take the action - let it through, and close the menu
		}
	}
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_mouse_button_event(button, action, mods)) { return; }
	}
	
	if (commandBox->parent != nullptr) {
		if (commandBox->cursor_in_this) {
			commandBox->on_mouse_button_event(button, action, mods);
			return;
		}
	}
	if (filesList->parent != nullptr) {
		if (filesList->cursor_in_this) {
			filesList->on_mouse_button_event(button, action, mods);
			return;
		}else if (action == GLFW_PRESS) {
			closeFilesList();
			return;
		}
	}
	if (REQUESTING_STRING) {
		if (STRING_REQUEST_TEXTEDIT->cursor_in_this) {
			STRING_REQUEST_TEXTEDIT->on_mouse_button_event(button, action, mods);
			return;
		}else if (action == GLFW_PRESS){
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_RECTANGLE);
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			RemoveWidgetFromParent(STRING_REQUEST_LABEL);
			setActiveLeafNode(nullptr);
			reclear = 3;
		}else{
			return;
		}
	}
	
	if (action == GLFW_PRESS && activeLeafNode == commandPalette && !commandPalette->cursor_in_this) {
		setActiveLeafNode(beforeCommandLeafNode);
	}
	
	if (rootelement) { rootelement->on_mouse_button_event(button, action, mods); }
}

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	rerender = true;
	
	if (recording_macro) {
		if ((key == GLFW_KEY_F12 || key == GLFW_KEY_F11) && action == GLFW_PRESS) {
			recording_macro = false;
			displayToast(MST::toMonoString("Macro Recording Over ("+std::to_string(keyboard_events.size())+")"));
			return;
		}
		
		KeyboardEvent event = {};
		event.character_callback = false;
		event.window = window;
		event.key = key;
		event.scancode = scancode;
		event.action = action;
		event.mods = mods;
		keyboard_events.back().push_back(event);
	}else if (key == GLFW_KEY_F12 && action == GLFW_PRESS) {
		if (replaying_macro) {
			displayToast(MST::toMonoString("Stopped Macro Replay"));
			glfwSwapInterval(1); // Enable vsync
			replaying_macro = false;
		}else{
			displayToast(MST::toMonoString("Starting Macro Recording"));
			recording_macro = true;
			keyboard_events.clear();
			keyboard_events.push_back({});
		}
		return;
	}
	
	if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
		if (!replaying_macro) {
			if (keyboard_events.empty()) {
				displayToast(MST::toMonoString("No Recorded Keystrokes"));
				return;
			}
	
			displayToast(MST::toMonoString("Replaying Macro Recording"));
	
			requestString(
				"Number of Repetitions (0 for Infinite)?",
				"",
				[&](MST::MonoString str) {
					setActiveLeafNode(before_reps_request);
	
					if (str.length == 0) {
						displayToast(MST::toMonoString("Canceled"));
						return;
					}
	
					try {
						rep_count = std::stoi(MST::toString(str));
					} catch (const std::invalid_argument&) {
						displayToast(MST::toMonoString("Invalid Number, Canceled"));
						return;
					} catch (const std::out_of_range&) {
						displayToast(MST::toMonoString("Number Too Large, Canceled"));
						return;
					}
	
					if (rep_count < 0) {
						displayToast(MST::toMonoString("Invalid Number, Canceled"));
						return;
					}
	
					if (rep_count == 0) {
						rep_count = -1;
					}
	
					glfwSwapInterval(0); // Disable VSync during macro replay.
					replaying_macro = true;
					current_step = 0;
				}
			);
		} else {
			displayToast(MST::toMonoString("Stopped Macro Replay"));
			glfwSwapInterval(1); // Re-enable VSync.
			replaying_macro = false;
		}
	
		return;
	}
	
	bool control = ((mods & GLFW_MOD_CONTROL) != 0);
	bool shift = ((mods & GLFW_MOD_SHIFT) != 0);
	
	if (on_key_event) {
		if (on_key_event(key, scancode, action, mods)) { return; };
	}
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_key_event(key, scancode, action, mods)) { return; }
	}
	
	if (filesList->parent != nullptr) {
		closeFilesList();
		return;
	}
	
	if (REQUESTING_STRING) {
		if (key == GLFW_KEY_ENTER && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_RECTANGLE);
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			RemoveWidgetFromParent(STRING_REQUEST_LABEL);
			setActiveLeafNode(nullptr);
			reclear = 3;
			if (ON_STRING_GIVEN) {
				ON_STRING_GIVEN(STRING_REQUEST_TEXTEDIT->getFullText());
			}
			return;
		}if (key == GLFW_KEY_ESCAPE && (action == GLFW_PRESS || action == GLFW_REPEAT) && (STRING_REQUEST_TEXTEDIT->mode == 'n' || !settings->getValue("use_vim", false))) {
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_RECTANGLE);
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			RemoveWidgetFromParent(STRING_REQUEST_LABEL);
			reclear = 3;
			if (beforeCommandLeafNode && rootelement->widgetexists(beforeCommandLeafNode)){
				setActiveLeafNode(beforeCommandLeafNode);
			}else{
				auto wdgt = rootelement->getFirstEditor();
				if (auto edtr = dynamic_cast<Editor*>(wdgt)) {
					auto wdgt = edtr->editors[edtr->tab_bar->selected_id];
					if (auto cdet = dynamic_cast<CodeEdit*>(wdgt)) {
						if (cdet->textedit) {
							setActiveLeafNode(cdet->textedit);
						}
					}
				}
			}
			return;
		}
	}
	
	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE && (curr_removing_panel || curr_adding_panel)){
		curr_removing_panel = false;
		curr_adding_panel = false;
		rerender = true;
		reclear = 3;
		return;
	}if (action == GLFW_PRESS && key == GLFW_KEY_O && control && shift) {
		openFolderSelector();
		return;
	}else if (action == GLFW_PRESS && key == GLFW_KEY_S && control && !shift) {
		displayText(MST::toMonoString("Saving..."));
		std::thread([&]() {
			std::lock_guard<std::mutex> lock(canMakeChanges);
			save();
		}).detach();
	}else if (action == GLFW_PRESS && key == GLFW_KEY_F5) {
		std::lock_guard<std::mutex> lock(canMakeChanges);
		displayText(MST::toMonoString("Saving..."));
		save();
		std::string build_command = settings->getProjectBuild();
		if (build_command != "") {
			launchCommandNonBlocking(build_command);
			return;
		}
	}else if (action == GLFW_PRESS && key == GLFW_KEY_P && control && shift) {
		setActiveLeafNode(commandPalette);
		return;
	}else if (action == GLFW_PRESS && key == GLFW_KEY_U && control && shift) {
		MST::MonoString selectedStr = MST::toMonoString("");
		if (auto te = dynamic_cast<TextEdit*>(activeLeafNode)) {
			selectedStr = te->getSelectedText(te->cursors[0]);
		}
		
		setActiveLeafNode(commandPalette);
		auto cp = dynamic_cast<TextEdit*>(commandPalette);
		cp->setFullText(MST::toMonoString(U'&'));
		cp->cursors = { {0, 1, 0, 1, 1} };
		cp->mode = 'i';
		
		if (selectedStr.length != 0) {
			cp->insertTextAtCursor(cp->cursors[0], selectedStr);
		}
		
		return;
	}else if (action == GLFW_PRESS && key == GLFW_KEY_EQUAL && control) {
		float new_v = App::settings->getValue("font_size", 23.0f) + 1;
		
		if (new_v > 50) {
			new_v = 50;
		}
		
		settings->setValue("font_size", new_v);
		
		TextRenderer::set_font_size(new_v);
		std::string default_font_path = getExecutableDir()+"/cascadia/CascadiaCode-Regular.ttf";
		std::string font_path = App::settings->getValue("font_path", default_font_path);
		bool success = TextRenderer::init_font(font_path.c_str());
		
		if (!success) {
			TextRenderer::init_font(default_font_path.c_str());
		}
		
		displayText(MST::toMonoString(std::to_string(new_v)));
	}else if (action == GLFW_PRESS && key == GLFW_KEY_MINUS && control) {
		float new_v = App::settings->getValue("font_size", 23.0f) - 1;
		
		if (new_v < 8) {
			new_v = 8;
		}
		
		settings->setValue("font_size", new_v);
		
		TextRenderer::set_font_size(new_v);
		std::string default_font_path = getExecutableDir()+"/cascadia/CascadiaCode-Regular.ttf";
		std::string font_path = App::settings->getValue("font_path", default_font_path);
		bool success = TextRenderer::init_font(font_path.c_str());
		
		if (!success) {
			TextRenderer::init_font(default_font_path.c_str());
		}
		
		displayText(MST::toMonoString(std::to_string(new_v)));
	}
	
	if ((action == GLFW_PRESS || action == GLFW_REPEAT) && activeLeafNode == commandPalette) {
		if (auto cmd_p = dynamic_cast<TextEdit*>(commandPalette)) {
			if (key == GLFW_KEY_ESCAPE) {
				if (cmd_p->mode == 'n' || !settings->getValue("use_vim", false)) {
					if (rootelement->widgetexists(beforeCommandLeafNode)) {
						setActiveLeafNode(beforeCommandLeafNode);
						return;
					}
				}
			}else if  (key == GLFW_KEY_DOWN || (key == GLFW_KEY_J && cmd_p->mode == 'n')) {
				auto cmdbx = dynamic_cast<ListBox*>(commandBox);
				cmdbx->moveDown();
				return;
			}else if  (key == GLFW_KEY_UP || (key == GLFW_KEY_K && cmd_p->mode == 'n')) {
				auto cmdbx = dynamic_cast<ListBox*>(commandBox);
				cmdbx->moveUp();
				return;
			}else if (key == GLFW_KEY_ENTER) {
				executeCommandPaletteAction();
				return;
			}
		}
	}
	
	if (rootelement) { rootelement->on_key_event(key, scancode, action, mods); }
}

void App::openFolderSelector() {
	if (FileBackends::isRemote()) {
		const std::string current = settings->getValue("current_folder", FileBackends::current()->homeDirectory());
		requestString("Remote folder path?", current, [](MST::MonoString path) {
			if (path.length != 0) setFolder(MST::toString(path));
			commandUnfocused();
		});
		return;
	}
	std::string fldr = settings->getValue("current_folder", std::string());
	
	const char * fpr = tinyfd_selectFolderDialog(
		"Open folder?",
		fldr.c_str()
	);
	
	if (fpr) {
		setFolder(fpr);
	}
	commandUnfocused();
}

void App::requestString(const std::string& label, const std::string& initial, StringGivenFunc callback) {
	ON_STRING_GIVEN = std::move(callback);
	REQUESTING_STRING = true;
	STRING_REQUEST_TEXTEDIT->setFullText(MST::toMonoString(initial));
	STRING_REQUEST_TEXTEDIT->mode = 'i';
	int chr = STRING_REQUEST_TEXTEDIT->lines[0].line_text.length;
	STRING_REQUEST_TEXTEDIT->cursors = {{0, chr, 0, chr, chr}};
	STRING_REQUEST_LABEL->setFullText(MST::toMonoString(label));
	MoveWidget(STRING_REQUEST_RECTANGLE, rootelement);
	MoveWidget(STRING_REQUEST_TEXTEDIT, rootelement);
	MoveWidget(STRING_REQUEST_LABEL, rootelement);
	before_reps_request = activeLeafNode;
	setActiveLeafNode(STRING_REQUEST_TEXTEDIT);
	reclear = 3;
}

namespace {
void refreshFileTrees(Widget* widget) {
	if (!widget) return;
	if (auto tree = dynamic_cast<FileTree*>(widget)) tree->save();
	for (auto* child : widget->children) refreshFileTrees(child);
}

bool parseSSHTarget(const std::string& input, SSHConnectionOptions& options, std::string& error) {
	std::string target = trim(input);
	if (target.empty()) {
		error = "SSH target is empty";
		return false;
	}
	const auto at = target.rfind('@');
	if (at != std::string::npos) {
		options.username = target.substr(0, at);
		target = target.substr(at + 1);
	}
	if (!target.empty() && target.front() == '[') {
		const auto close = target.find(']');
		if (close == std::string::npos) {
			error = "Invalid bracketed SSH hostname";
			return false;
		}
		options.hostname = target.substr(1, close - 1);
		if (close + 1 < target.size() && target[close + 1] == ':') {
			try {
				const int port = std::stoi(target.substr(close + 2));
				if (port < 1 || port > 65535) throw std::out_of_range("port");
				options.port = static_cast<unsigned short>(port);
			} catch (...) {
				error = "Invalid SSH port";
				return false;
			}
		}
	} else {
		const auto colon = target.rfind(':');
		if (colon != std::string::npos && target.find(':') == colon) {
			try {
				const int port = std::stoi(target.substr(colon + 1));
				if (port < 1 || port > 65535) throw std::out_of_range("port");
				options.port = static_cast<unsigned short>(port);
				target.resize(colon);
			} catch (...) {
				error = "Invalid SSH port";
				return false;
			}
		}
		options.hostname = target;
	}
	if (options.hostname.empty()) {
		error = "SSH hostname is empty";
		return false;
	}
	return true;
}
} // namespace

#ifdef _WIN32
bool LaunchDetachedProcess(const std::wstring& exePath, const std::vector<std::wstring>& args) {
	// 1. Build the command line string. The first token should be the executable path.
	std::wstring cmdLine = L"\"" + exePath + L"\"";
	for (const auto& arg : args) {
		cmdLine += L" \"" + arg + L"\""; // Quotes prevent splitting on internal spaces
	}

	// CreateProcessW requires a mutable buffer for the command line
	std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
	cmdLineBuffer.push_back(L'\0');

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};

	// 2. Launch with DETACHED_PROCESS flag to decouple from the parent console/process tree
	BOOL success = CreateProcessW(
		nullptr,               // Application Name (null when passed in cmdLine)
		cmdLineBuffer.data(),  // Command line arguments
		nullptr,               // Process security attributes
		nullptr,               // Thread security attributes
		FALSE,                 // Inherit handles
		DETACHED_PROCESS,      // Detach completely from parent
		nullptr,               // Use parent's environment
		nullptr,               // Use parent's starting directory
		&si,                   // Startup info
		&pi                    // Process information
	);

	if (success) {
		// Close handles immediately so OS can clean up when the child terminates
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return true;
	}

	std::wcerr << L"Launch failed. Error: " << GetLastError() << std::endl;
	return false;
}
#else
bool LaunchDetachedProcess(const std::string& exePath, const std::vector<std::string>& args) {
	// 1. Convert vector to the C-style array format expected by execvp
	std::vector<char*> c_args;
	c_args.push_back(const_cast<char*>(exePath.c_str()));
	for (const auto& arg : args) {
		c_args.push_back(const_cast<char*>(arg.c_str()));
	}
	c_args.push_back(nullptr); // Argument list must be null-terminated

	// 2. First fork
	pid_t pid = fork();
	if (pid < 0) return false;

	if (pid == 0) {
		// Inside the first child: fork a second time
		pid_t grandchild_pid = fork();
		
		if (grandchild_pid < 0) {
			_exit(1); 
		}
		
		if (grandchild_pid > 0) {
			// First child exits immediately. The grandchild becomes an orphan 
			// and gets adopted by init (PID 1), disconnecting it from the parent.
			_exit(0); 
		}

		// Inside the grandchild: start a new session to fully detach
		setsid();

		// Optional: Redirect standard streams to prevent writing to parent terminal
		FILE* devNull = fopen("/dev/null", "w");
		if (devNull) {
			dup2(fileno(devNull), STDIN_FILENO);
			dup2(fileno(devNull), STDOUT_FILENO);
			dup2(fileno(devNull), STDERR_FILENO);
			fclose(devNull);
		}

		// Replace grandchild process image with the target executable
		execvp(exePath.c_str(), c_args.data());
		
		// execvp only returns if an error occurred
		_exit(1); 
	}

	// Inside the parent: reap the first child immediately
	int status;
	waitpid(pid, &status, 0); 
	
	return true; 
}
#endif

void App::connectSSH() {
	const std::string previous = settings->getValue("ssh_target", std::string());
	requestString("SSH target (user@host[:port])?", previous, [](MST::MonoString value) {
		SSHConnectionOptions options;
		std::string error;
		const std::string target = MST::toString(value);
		
		if (!parseSSHTarget(target, options, error)) {
			displayToast(MST::toMonoString(error));
			return;
		}
		
		settings->setValue("ssh_target", target);
		requestString("SSH password (blank for key/agent)?", "", [options, target](MST::MonoString passwordMST) mutable {
			std::string password = MST::toString(passwordMST);
			STRING_REQUEST_TEXTEDIT->setFullText(MST::MonoString{});
			
			std::string startpath = getExecutablePath();
			
			bool worked;
			
			#ifdef _WIN32
			if (password != "") {
				worked = LaunchDetachedProcess(widen(startpath), {widen("--ssh"), widen(target), widen("--ssh_password"), widen(password)});
			}else{
				worked = LaunchDetachedProcess(widen(startpath), {widen("--ssh"), widen(target)});
			}
			#else
			if (password != "") {
				worked = LaunchDetachedProcess(startpath, {"--ssh", target, "--ssh_password", password});
			}else{
				worked = LaunchDetachedProcess(startpath, {"--ssh", target});
			}
			#endif
			
			if (!worked) {
				displayToast(MST::toMonoString("Failed to start codewizard process"));
			}
			
			commandUnfocused();
		});
	});
}

void App::_connectSSH(std::string host, std::string password) {
	SSHConnectionOptions options;
	std::string error;
	if (!parseSSHTarget(host, options, error)) {
		displayToast(MST::toMonoString(error));
		return;
	}
	options.key_path = settings->getValue("ssh_key_path", std::string());
	options.helper_path = settings->getValue("ssh_helper_path", std::string("cwremote"));
	options.password = password;
	
	std::string connect_error;
	auto backend = SSHFileBackend::connect(options, connect_error);
	// Do not retain the password after OpenSSH has started.
	options.password.clear();
	if (!backend) {
		std::cout << "SSH connection failed: " << connect_error << "\n";
		displayToast(MST::toMonoString("SSH connection failed: " + connect_error));
		return;
	}

	backend->setDisconnectCallback([backend]() {
		if (FileBackends::current().get() != backend.get()) return;
		const std::string local_folder = settings->getValue("ssh_previous_local_folder", getExecutableDir());
		FileBackends::useLocal();
		setFolder(local_folder);
		
		tinyfd_messageBox(
			"SSH Disconnected",
			"The SSH connection has been lost!",
			"ok",
			"error",
			1
		);
		
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	});

	if (!FileBackends::isRemote()) {
		settings->setValue("ssh_previous_local_folder", settings->getValue("current_folder", getExecutableDir()));
	}
	
	FileBackends::use(backend);
	const std::string remote_folder = backend->remoteInfo().cwd.empty()
		? backend->homeDirectory()
		: backend->remoteInfo().cwd;
	setFolder(remote_folder);
	refreshFileTrees(rootelement);
	displayToast(MST::toMonoString("Connected to " + backend->displayName()));
}

void App::disconnectSSH() {
	if (!FileBackends::isRemote()) {
		displayToast(MST::toMonoString("No SSH connection is active."));
		return;
	}
	
	save();
	
	const std::string local_folder = settings->getValue("ssh_previous_local_folder", getExecutableDir());
	FileBackends::useLocal();
	setFolder(local_folder);
	
	glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void App::setFolder(std::string fpr) {
	BackendFileStat info;
	std::string backend_error;
	if (!FileBackends::current()->stat(fpr, info, backend_error) || !info.exists || !info.is_directory) {
		return;
	}

	if (!FileBackends::isRemote()) {
		std::error_code ec;
		std::filesystem::current_path(std::filesystem::path(fpr), ec);
		if (ec) return;
	}
	
	std::string oldfolder = settings->getValue("current_folder", getExecutableDir());
	for (auto lsp : lsp_client_map){
		if (lsp.second) {
			lsp.second->changeFolder(oldfolder, fpr);
		}
	}
	
	settings->setValue("current_folder", fpr);
}

void App::character_callback(GLFWwindow* window, unsigned int codepoint) {
	rerender = true;
	
	if (recording_macro) {
		KeyboardEvent event = {};
		event.character_callback = true;
		event.window = window;
		event.cc_codepoint = codepoint;
		keyboard_events.back().push_back(event);
	}
	
	if (on_char_event) {
		on_char_event(codepoint);
	}
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_char_event(codepoint)) { return; }
	}
	
	if (filesList->parent != nullptr) {
		closeFilesList();
		return;
	}
	
	if (rootelement) { rootelement->on_char_event(codepoint); }
}

void App::scroll_callback(GLFWwindow* window, double xpos, double ypos) {
	rerender = true;
	
	if (settings->getValue("invert_scroll_v", false)) {
		ypos *= -1;
	}if (settings->getValue("invert_scroll_h", false)) {
		xpos *= -1;
	}
	
	if (on_scroll_event) {
		if (on_scroll_event(-xpos, -ypos)) { return; };
	}
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_scroll_event(-xpos, -ypos)) { return; }
	}
	
	if (commandBox->parent != nullptr) {
		if (commandBox->cursor_in_this) {
			commandBox->on_scroll_event(-xpos, -ypos);
			return;
		}
	}
	if (filesList->parent != nullptr) {
		if (filesList->cursor_in_this) {
			filesList->on_scroll_event(-xpos, -ypos);
			return;
		}
	}
	if (REQUESTING_STRING) {
		if (STRING_REQUEST_TEXTEDIT->cursor_in_this) {
			STRING_REQUEST_TEXTEDIT->on_scroll_event(-xpos, -ypos);
			return;
		}
	}
	
	if (rootelement) { rootelement->on_scroll_event(-xpos, -ypos); }
}

void App::resize_callback(GLFWwindow* window, int width, int height) {
	reclear = 3;
	
	WINDOW_WIDTH = width;
	WINDOW_HEIGHT = height;
	
	 // 1) Update the viewport to cover the whole new window:
	glViewport(0, 0, width, height);

	// 2) Rebuild your projection matrix:
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	// keep the same top-left origin you like, but use the new size:
	glOrtho(0, width, height, 0, -1, 1);

	// 3) Go back to modelview for your draws:
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	if (on_resize_event) {
		if (on_resize_event(width, height)) { return; };
	}
	
	recheckmenusizing = true;
	
	rerender = true;
}

void App::checkMenubarVisibility() {
	if (!filesButton) { return; }
	
	bool showFiles = settings->getValue("use_files_button", true) && (filesButton->t_x+filesButton->t_w < tb->min_b->t_x);
	
	if (showFiles && filesButton->parent == nullptr) {
		MoveWidget(filesButton, tb);
	}else if (!showFiles && filesButton->parent != nullptr) {
		RemoveWidgetFromParent(filesButton);
	}
}

void App::setTheme(Theme t) {
	rerender = true;
	theme = t;
	bgcolor = t.main_background_color;
}

void App::min_button() {
	rerender = true;
	glfwIconifyWindow(window);
}

void App::win_button() {
	rerender = true;
#ifdef _WIN32
	HWND hwnd = glfwGetWin32Window(window);
	if (IsZoomed(hwnd)) {
		ShowWindow(hwnd, SW_RESTORE);
	} else {
		ShowWindow(hwnd, SW_MAXIMIZE);
	}
#else
	if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
		glfwRestoreWindow(window);
	} else {
		glfwMaximizeWindow(window);
	}
#endif
}

void App::commandUnfocused() {
	searchStopFlag->store(true, std::memory_order_relaxed);
	
	if (auto com_p = dynamic_cast<TextEdit*>(commandPalette)) {
		if (auto editor = dynamic_cast<Editor*>(activeEditor)) {
			com_p->setFullText(editor->getPaletteName());
		}else{
			std::string folder = settings->getValue("current_folder", getExecutableDir());
			std::filesystem::path p(folder);
			com_p->setFullText(MST::toMonoString(p.filename().string()));
		}
		
		std::string txt = MST::toString(com_p->getFullText());
		pending_window_title = txt + " - " + WINDOW_TITLE;
		time_till_regular = 2;
	}
	
	// here let's remove the cp_listbox from the rootwidget
	if (commandBox && commandBox->parent != nullptr) {
		RemoveWidgetFromParent(commandBox);
	}
	
	reclear = 3;
}

void App::closeFilesList() {
	reclear = 3;
	
	if (filesList && filesList->parent != nullptr) {
		RemoveWidgetFromParent(filesList);
	}
}

void App::openFilesList() {
	reclear = 3;
	
	if (!filesList) { return; }
	
	if (filesList->parent == nullptr) {
		App::MoveWidget(filesList, rootelement);
	}
	
	files_in_box = rootelement->getOpenFiles(false);
	
	if (auto lb = dynamic_cast<ListBox*>(filesList)) {
		std::vector<MST::MonoString> items;
		for (auto v : files_in_box) {
			items.push_back(MST::toMonoString(v[0]));
		}
		
		lb->setElements(items);
		lb->toshow = std::min(12, (int)items.size());
	}
}

static std::string quoteCmdPathWindows(const std::string& s) {
	// For paths only. Windows paths cannot contain literal ".
	// Still avoid % expansion.
	std::string out;
	out.reserve(s.size() + 2);

	out += '"';

	for (char c : s) {
		if (c == '%') {
			out += "%%";
		}
		else {
			out += c;
		}
	}

	out += '"';
	return out;
}

static std::string quoteShellPathPosix(const std::string& s) {
	std::string out;
	out.reserve(s.size() + 2);

	out += '\'';

	for (char c : s) {
		if (c == '\'') {
			out += "'\\''";
		}
		else {
			out += c;
		}
	}

	out += '\'';
	return out;
}

static std::string makeTempCommitMessageFile(const std::string& message) {
	namespace fs = std::filesystem;

	auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();

	std::random_device rd;
	std::mt19937_64 rng(rd());
	uint64_t r = rng();

	fs::path path = fs::temp_directory_path() /
		("codewizard_git_commit_" + std::to_string(now) + "_" + std::to_string(r) + ".txt");

	std::ofstream file(path, std::ios::binary);
	if (!file) {
		return "";
	}

	file << message;

	// Git likes commit message files to end with a newline.
	if (message.empty() || message.back() != '\n') {
		file << '\n';
	}

	file.close();

	if (!file) {
		return "";
	}

	return path.string();
}

void App::gitPush() {
	requestString(
		"Git Commit Message?",
		"",
		[&](MST::MonoString str) {
			if (str.length == 0) {
				return;
			}

			std::string mes = MST::toString(str);
			std::string folder = settings->getValue("current_folder", getExecutableDir());

			std::string commitFile = makeTempCommitMessageFile(mes);

			if (commitFile.empty()) {
				std::cerr << "Failed to create temporary commit message file\n";
				return;
			}

#ifdef _WIN32
			launchCommandNonBlocking(
				"git -C " + quoteCmdPathWindows(folder) +
				" add . && git -C " + quoteCmdPathWindows(folder) +
				" commit -F " + quoteCmdPathWindows(commitFile) +
				" && git -C " + quoteCmdPathWindows(folder) +
				" push"
			);
#else
			launchCommandNonBlocking(
				"git -C " + quoteShellPathPosix(folder) +
				" add . && git -C " + quoteShellPathPosix(folder) +
				" commit -F " + quoteShellPathPosix(commitFile) +
				" && git -C " + quoteShellPathPosix(folder) +
				" push"
			);
#endif
		}
	);
}

void App::gitPull() {
	std::string folder = settings->getValue("current_folder", getExecutableDir());
#ifdef _WIN32
		launchCommandNonBlocking("cd /d "+quoteCmdPathWindows(folder)+" && git pull");
#else
		launchCommandNonBlocking("cd "+quoteShellPathPosix(folder)+" && git pull");
#endif
}

void App::gitForcePull() {
	std::string folder = settings->getValue("current_folder", getExecutableDir());
#ifdef _WIN32
	launchCommandNonBlocking(
		"pushd " + quoteCmdPathWindows(folder) + " && "
		"git merge --abort 2>nul & git rebase --abort 2>nul & git cherry-pick --abort 2>nul & "
		"git fetch --prune origin && "
		"git reset --hard @{u} && "
		"popd"
	);
#else
	launchCommandNonBlocking(
		"cd " + quoteShellPathPosix(folder) + " && "
		"git merge --abort 2>/dev/null; git rebase --abort 2>/dev/null; git cherry-pick --abort 2>/dev/null; "
		"git fetch --prune origin && "
		"git reset --hard @{u}"
	);
#endif
}

void App::undoFixIt() {
	if (!activeEditor) {
		displayToast(MST::toMonoString("No editor active."));
	}else{
		Editor* edtr = (Editor*)activeEditor;
		auto wdgt = edtr->editors[edtr->tab_bar->selected_id];
		if (auto cdet = dynamic_cast<CodeEdit*>(wdgt)) {
			cdet->undo_fixit();
			displayToast(MST::toMonoString("Reverted to Spaces"));
		}else{
			displayToast(MST::toMonoString("Active Editor is not a CodeEdit."));
		}
	}
}

void App::fixIt() {
	if (!activeEditor) {
		displayToast(MST::toMonoString("No editor active."));
	}else{
		Editor* edtr = (Editor*)activeEditor;
		auto wdgt = edtr->editors[edtr->tab_bar->selected_id];
		if (auto cdet = dynamic_cast<CodeEdit*>(wdgt)) {
			cdet->run_fixit();
			displayToast(MST::toMonoString("Fix-It Complete"));
		}else{
			displayToast(MST::toMonoString("Active Editor is not a CodeEdit."));
		}
	}
}

void App::saveThemeToFile() {
	const char * fp = tinyfd_saveFileDialog(
		"Save as?", // dialog title
		"CodeWizard2Theme.json", // default path and filename
		0, NULL, // filter count and filters
		0 // allow multiple selections (0 = no)
	);
	
	if (fp) {
		std::string filePath(fp);
		
		std::string tosave = settings->getSubSet({"dark_mode", "c_comments_color", "c_functs_color", "c_keywords_color", "c_literals_color", "c_punctuation_color", "c_strings_color", "c_tint_color", "c_saturation", "c_types_color", "c_vars_color", "c_operator_color", "c_preproc_color", "c_invalid_color"});
		std::string err;
		if (!atomicWriteReplace(filePath, tosave, &err)) {
			displayToast(MST::toMonoString("Failed to write file: "+err));
		}else{
			displayToast(MST::toMonoString("Saved theme to file!"));
		}
	}
}

void App::loadThemeFromFile() {
	const char * fp = tinyfd_openFileDialog(
		"Select theme",    // dialog title
		"",                 // default path and filename
		0, NULL, NULL,      // filter count and filters
		0                   // allow multiple selections (0 = no)
	);
	
	if (fp) {
		std::string filePath(fp);
		bool worked = false;
		MST::MonoString text = readFileToMonoString(filePath, worked);
		if (!worked) {
			displayToast(MST::toMonoString("Could not read file. ")+text);
		}else{
			std::string str = MST::toString(text);
			
			if (!settings->bringInSubset(str)) {
				displayToast(MST::toMonoString("Could not load settings."));
			}else{
				std::string cl = App::settings->getValue("c_tint_color", App::empty);
				if (cl != empty) {
					bool worked;
					Color c = stringToColor(cl, worked);
					if (worked) {
						theme.tint_color->r = c.r;
						theme.tint_color->g = c.g;
						theme.tint_color->b = c.b;
					}
				}
				darkmode = settings->getValue("dark_mode", true);
				updateFromTintColor(&theme);
				setSynColor(&theme, "c_strings_color", 1);
				setSynColor(&theme, "c_comments_color", 2);
				setSynColor(&theme, "c_vars_color", 3);
				setSynColor(&theme, "c_types_color", 4);
				setSynColor(&theme, "c_functs_color", 5);
				setSynColor(&theme, "c_keywords_color", 6);
				setSynColor(&theme, "c_punctuation_color", 7);
				setSynColor(&theme, "c_literals_color", 8);
				setSynColor(&theme, "c_operator_color", 9);
				setSynColor(&theme, "c_preproc_color", 10);
				setSynColor(&theme, "c_invalid_color", 11);
				displayToast(MST::toMonoString("Done!"));
				
				App::rootelement->executeAction(WidgetActionType::SETTINGS_CHANGE);
			}
		}
	}
}

void App::executeCommandPaletteAction() {
	auto cmdpl = dynamic_cast<TextEdit*>(commandPalette);
	auto cmdbx = dynamic_cast<ListBox*>(commandBox);
	auto text = cmdpl->getFullText();
	
	if (cmdbx->elements.size() == 0) {
		return;
	}
	
	int cur_sel = cmdbx->selected_id;
	int cur_type;
	if (cur_sel >= INDEXED_FILES.currentlyshowing.size()) {
		std::cerr << "That's blaad - but the show must go on." << std::endl;
		return;
	}else{
		cur_type = INDEXED_FILES.currentlyshowingtype[cur_sel];
		cur_sel = INDEXED_FILES.currentlyshowing[cur_sel];
	}
	
	if (cur_type == 1) { // move er up to the top and copy it
		auto data = cmdbx->elements[cmdbx->selected_id];
		
		if (data.length == 0){
			return;
		}
		
		std::string str = MST::toString(data);
		
		SetClipboardText(str);
		cmdpl->setFullText(data);
		cmdpl->cursors[0].head_line = cmdpl->lines.size()-1;
		cmdpl->cursors[0].head_char = cmdpl->lines[cmdpl->cursors[0].head_line].line_text.length;
		
		return;
	}else if (cur_type == 2){
		if (beforeCommandLeafNode) {
			setActiveLeafNode(beforeCommandLeafNode);
		}
		
		if (cur_sel < 0 || cur_sel >= storedsearches.storedsearches.size()) {
			return;
		}
		
		std::lock_guard<std::mutex> lock(storedsearches.mtx);
		auto itm = storedsearches.storedsearches[cur_sel];
		
		if (itm.path == "") {
			return;
		}
		
		std::filesystem::path p(itm.path);
		openFromCMD(itm.path, p.filename().string(), itm.linenum-1);
		return;
	}
	
	if (cur_sel < 0 || cur_sel >= INDEXED_FILES.fullPaths.size()) {
		std::cerr << "That's really strange - but the show must go on." << std::endl;
		return;
	}
	
	std::string filepath = INDEXED_FILES.fullPaths[cur_sel];
	
	
	if (beforeCommandLeafNode && beforeCommandLeafNode->parent != nullptr && beforeCommandLeafNode->is_visible) {
		setActiveLeafNode(beforeCommandLeafNode);
	}else{
		auto wdgt = rootelement->getFirstEditor();
		if (auto edtr = dynamic_cast<Editor*>(wdgt)) {
			auto wdgt = edtr->editors[edtr->tab_bar->selected_id];
			if (auto cdet = dynamic_cast<CodeEdit*>(wdgt)) {
				if (cdet->textedit) {
					setActiveLeafNode(cdet->textedit);
				}
			}
		}
	}
	
	if (INDEXED_FILES.fullPaths[cur_sel] == "") {
		// this can happen with the sep between files already open and all files.
		return;
	}if (filepath.at(0) == ':') { // it's a command
		if (filepath == ":Connect via SSH") {
			connectSSH();
		}else if (filepath == ":Disconnect SSH") {
			disconnectSSH();
		}else if (filepath == ":Git Push") {
			gitPush();
		}else if (filepath == ":Git Pull") {
			gitPull();
		}else if (filepath == ":Git Force Pull") {
			gitForcePull();
		}else if (filepath == ":Help") {
			MoveWidget(helpMenu, rootelement);
			reclear = 3;
		}else if (filepath == ":Save Theme Settings To File") {
			saveThemeToFile();
		}else if (filepath == ":Load Theme Settings From File") {
			loadThemeFromFile();
		}else if(filepath == ":Test Toast Box"){
			displayToast(MST::toMonoString("Example Toast Message."));
		}else if(filepath == ":Test Text Line"){
			displayText(MST::toMonoString("Example Text Line Message."));
		}else if (filepath == ":Restart Language Servers (LSPs)") {
			restartLSPs();
		}else if (filepath == ":Open `languages.json` file") {
			openLanguagesFile();
		}else if (filepath == ":Run FixIt (Spaces to Tabs)") {
			fixIt();
		}else if (filepath == ":Undo FixIt (Tabs to Spaces)") {
			undoFixIt();
		}else if (filepath == ":How Many Widgets Currently?") {
			displayToast(MST::toMonoString("There are: " + std::to_string(all_widgets.size())+" open widgets."));
		}
		
		return;
	}
	
	openFromCMD(filepath, INDEXED_FILES.indexedNames[cur_sel]);
}

void App::setSynColor(Theme* t, std::string name, int id) {
	std::string cl = App::settings->getValue(name, empty);
	if (cl == empty) { return; }
	bool worked;
	Color c = stringToColor(cl, worked);
	if (!worked) { return; }
	t->syntax_colors[id]->r = c.r;
	t->syntax_colors[id]->g = c.g;
	t->syntax_colors[id]->b = c.b;
}

void App::openFromCMD(std::string filepath, std::string filename, int line) {
	FileInfo* finfo = new FileInfo();
	finfo->filepath = filepath;
	finfo->filename = filename;
	finfo->backend = FileBackends::current();
	
	auto openInEditor = [&](Editor* edtr) {
		if (line != -1) {
			edtr->fileOpenRequested(finfo, line, 0, line, 0);
		}else{
			edtr->fileOpenRequested(finfo);
		}
	};
	
	if (auto edtr = dynamic_cast<Editor*>(rootelement->fileOpen(filepath))) {
		openInEditor(edtr);
		return;
	}
	
	if (beforeCommandLeafNode && rootelement->widgetexists(beforeCommandLeafNode)) {
		if (auto edtr = dynamic_cast<Editor*>(beforeCommandLeafNode)) {
			openInEditor(edtr);
			return;
		}
	}
	
	if (activeEditor && rootelement->widgetexists(activeEditor)) {
		if (auto edtr = dynamic_cast<Editor*>(activeEditor)) {
			openInEditor(edtr);
			return;
		}
	}
	
	if (Widget* wdgt = rootelement->getFirstEditor()) {
		if (auto edtr = dynamic_cast<Editor*>(wdgt)) {
			openInEditor(edtr);
			return;
		}
	}
}

#include <vector>
#include <queue>
#include <unordered_set>
#include <string>
#include <filesystem>
#include <future>
#include <thread>
#include <algorithm>

void App::indexFiles() {
	std::string rootPath = settings->getValue("current_folder", getExecutableDir());
	
	INDEXED_FILES.indexedNames.clear();
	INDEXED_FILES.displayPaths.clear();
	INDEXED_FILES.fullPaths.clear();
	INDEXED_FILES.currentlyshowing.clear();
	INDEXED_FILES.currentlyshowingtype.clear();
	
	const std::size_t maxFiles          = settings->getValue("max_index_files", 15000);
	const std::size_t maxDisplayChars  = (commandPalette->t_w - text_padding * 2) / TextRenderer::get_text_width(1) - 1;
	
	static const std::vector<std::string> commands = {"Connect via SSH","Disconnect SSH","Git Push","Git Pull","Git Force Pull","Help","Save Theme Settings To File","Load Theme Settings From File","Restart Language Servers (LSPs)","Open `languages.json` file","Test Toast Box","Test Text Line","Run FixIt (Spaces to Tabs)","Undo FixIt (Tabs to Spaces)","How Many Widgets Currently?"}; 

	// 1. Pre-allocate memory to prevent vector re-allocations
	INDEXED_FILES.indexedNames.reserve(maxFiles + commands.size());
	INDEXED_FILES.displayPaths.reserve(maxFiles + commands.size());
	INDEXED_FILES.fullPaths.reserve(maxFiles + commands.size());

	// 2. O(1) Hash Set instead of O(log N) tree set
	std::unordered_set<std::string> dontshowagain;
	
	files_in_box = rootelement->getOpenFiles(false);
	dontshowagain.reserve(files_in_box.size());

	for (const auto& fInfo : files_in_box) {
		if (fInfo[1].empty()) continue;
		
		const std::string& absPath = fInfo[1];
		INDEXED_FILES.fullPaths.push_back(absPath);
		INDEXED_FILES.displayPaths.push_back(MST::toMonoString(">" + fInfo[0]));
		INDEXED_FILES.indexedNames.push_back(fInfo[0]);
		dontshowagain.insert(absPath);
	}

	if (FileBackends::isRemote()) {
		auto backend = FileBackends::current();
		std::vector<ScannedFile> scanned;
		std::string scan_error;
		std::size_t seen = 0;
		if (backend->scanFiles(rootPath, maxFiles, scanned, scan_error)) {
			for (const auto& f : scanned) {
				if (seen >= maxFiles) break;
				if (dontshowagain.count(f.fullPath)) continue;
				INDEXED_FILES.fullPaths.push_back(f.fullPath);
				std::string rel = f.fullPath.size() > (rootPath.size() + 1) ? f.fullPath.substr(rootPath.size() + 1) : f.fullPath;
				if (rel.size() > maxDisplayChars) {
					rel = rel.substr(rel.size() - maxDisplayChars);
					const auto slash = rel.find_first_of("/\\");
					if (slash != std::string::npos) rel = rel.substr(slash);
				}
				INDEXED_FILES.displayPaths.push_back(MST::toMonoString(rel));
				INDEXED_FILES.indexedNames.push_back(f.name);
				++seen;
			}
		}
	}

	const std::size_t rootLen = rootPath.size() + 1;
	std::size_t seen = 0;

	auto getDisplayPath = [rootLen, maxDisplayChars](const std::string& absPath) {
		std::string rel = absPath.size() > rootLen ? absPath.substr(rootLen) : absPath;
		if (rel.size() > maxDisplayChars) {
			rel = rel.substr(rel.size() - maxDisplayChars);
			const auto slash = rel.find_first_of("/\\");
			if (slash != std::string::npos) rel = rel.substr(slash);
		}
		return MST::toMonoString(rel);
	};

	std::queue<std::string> dirs;
	dirs.push(rootPath);

	struct FileEntry {
		std::string absPath;
		std::string fileName;
	};

	struct DirBatchResult {
		std::vector<std::string> subDirs;
		std::vector<FileEntry> files;
	};

	const unsigned int concurrency = std::max(1u, std::thread::hardware_concurrency());

	// 3. Parallel BFS preserving exact sequence order
	while (!FileBackends::isRemote() && !dirs.empty() && seen < maxFiles) {
		std::vector<std::string> currentBatch;
		while (!dirs.empty() && currentBatch.size() < concurrency) {
			currentBatch.push_back(dirs.front());
			dirs.pop();
		}

		std::vector<std::future<DirBatchResult>> futures;
		futures.reserve(currentBatch.size());

		for (const auto& curDir : currentBatch) {
			futures.push_back(std::async(std::launch::async, [curDir]() {
				DirBatchResult result;
				std::error_code dirEc;
				std::filesystem::directory_iterator iter(curDir, dirEc);
				if (dirEc) return result;

				for (const auto& entry : iter) {
					std::string absPath = entry.path().string();
					auto slash = absPath.find_last_of("/\\");
					std::string fileName = (slash != std::string::npos) ? absPath.substr(slash + 1) : absPath;

					if (entry.is_directory()) {
						if (!fileName.empty() && fileName[0] != '.') {
							result.subDirs.push_back(std::move(absPath));
						}
					} else if (entry.is_regular_file()) {
						result.files.push_back({std::move(absPath), std::move(fileName)});
					}
				}
				return result;
			}));
		}

		// Collect futures sequentially to enforce exact order
		for (auto& fut : futures) {
			DirBatchResult res = fut.get();

			for (auto& sub : res.subDirs) {
				dirs.push(std::move(sub));
			}

			for (auto& file : res.files) {
				if (seen >= maxFiles) break;
				if (dontshowagain.count(file.absPath)) continue;

				INDEXED_FILES.fullPaths.push_back(file.absPath);
				INDEXED_FILES.displayPaths.push_back(getDisplayPath(file.absPath));
				INDEXED_FILES.indexedNames.push_back(std::move(file.fileName));

				++seen;
			}
		}
	}

	for (auto const& cmd : commands) {
		std::string tagged = ":" + cmd;
		INDEXED_FILES.fullPaths.push_back(tagged);
		INDEXED_FILES.indexedNames.push_back(tagged);
		INDEXED_FILES.displayPaths.push_back(MST::toMonoString(tagged));
	}
}

std::vector<std::string> App::extractStringWords(std::string word) {
	std::vector<std::string> wordsRaw = {""};

	std::string keepers = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0987654321.";

	for (auto c : word) {
		if (keepers.find(c) == std::string::npos) {
			std::string start(1, c);
			wordsRaw.push_back(start);
			wordsRaw.push_back("");
		}else {
			wordsRaw[wordsRaw.size()-1] += c;
		}
	}
	
	std::vector<std::string> words;

	for (auto word : wordsRaw) {
		if (!word.empty() && word != " ") {
			words.push_back(word);
		}
	}
	
	return words;
}

void App::fillCmdBox() {
	auto cb = dynamic_cast<ListBox*>(commandBox);
	auto cp = dynamic_cast<TextEdit*>(commandPalette);
	
	
	
	INDEXED_FILES.currentlyshowing.clear();
	INDEXED_FILES.currentlyshowingtype.clear();
	
	
	searchStopFlag->store(true, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> searchLock(storedsearches.mtx);
		storedsearches.storedsearches.clear();
		storedsearches.torun.clear();
		storedsearches.updateSomething = false;
	}
	
	std::vector<MST::MonoString> els;
	
	MST::MonoString searchfor = cp->getFullText();
	
	// thinking is that we want fewer results back and forth, single chars are common, pairs of 3 chars is less common. thus less data
	if (((searchfor.length >= 2 && !FileBackends::isRemote()) || (searchfor.length >= 4 && FileBackends::isRemote())) && MST::char32At(searchfor, 0) == U'&') {
		els.push_back(MST::MonoString()); // for the top text
		INDEXED_FILES.currentlyshowingtype.push_back(2);
		INDEXED_FILES.currentlyshowing.push_back(-1);
		
		std::string loweredSearchfor = MST::toString(searchfor);
		loweredSearchfor = loweredSearchfor.substr(1); // remove the &
		
		searchAcrossFiles(loweredSearchfor);
		
//		for (const auto& [key, matches] : res) {
//			const auto& [filePath, fileName] = key;
//			
//			std::filesystem::path p(filePath);
//			
//			els.push_back(MST::toMonoString(p.filename().string()));
//			
//			StoredSearch itm = {filePath, 0};
//			storedsearches.push_back(itm);
//			
//			INDEXED_FILES.currentlyshowing.push_back(storedsearches.size()-1);
//			INDEXED_FILES.currentlyshowingtype.push_back(2);
//			
//			for (const auto& [lineNum, text] : matches) {
//				els.push_back(MST::toMonoString("    "+text));
//				
//				StoredSearch itm = {filePath, lineNum-1};
//				storedsearches.push_back(itm);
//				
//				INDEXED_FILES.currentlyshowing.push_back(storedsearches.size()-1);
//				INDEXED_FILES.currentlyshowingtype.push_back(2);
//			}
//		}
		
		cb->setElements(els);
		return;
	}
	
	auto res = calcExpression(searchfor);
	if (res.first){
		INDEXED_FILES.currentlyshowing.push_back(0);
		INDEXED_FILES.currentlyshowingtype.push_back(1);
		els.push_back(doubleToMonoString_pretty(res.second));
	}
	
	std::string str = MST::toString(searchfor);
	
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::tolower(c); });
	
	auto words = extractStringWords(str);

	for (int i = 0; i < INDEXED_FILES.indexedNames.size(); i++) {
		bool works = true;
		auto lowered = INDEXED_FILES.indexedNames[i];
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c){ return std::tolower(c); });
		
		for (auto word : words) {
			if (lowered.find(word) == std::string::npos) {
				works = false;
				break;
			}
		}
		if (works) {
			INDEXED_FILES.currentlyshowing.push_back(i);
			INDEXED_FILES.currentlyshowingtype.push_back(0);
			els.push_back(INDEXED_FILES.displayPaths[i]);
		}
	}
	
	cb->setElements(els);
}

void App::setActiveLeafNode(Widget* w) {
	if (REQUESTING_STRING && w != STRING_REQUEST_TEXTEDIT) {
		REQUESTING_STRING = false;
		RemoveWidgetFromParent(STRING_REQUEST_RECTANGLE);
		RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
		RemoveWidgetFromParent(STRING_REQUEST_LABEL);
		reclear = 3;
	}
	
	if (w == commandPalette && activeLeafNode != commandPalette) {
		beforeCommandLeafNode = activeLeafNode;
	}
	
	rerender = true;
	activeLeafNode = w;
	activeEditor = nullptr;
	
	if (w) {
		Widget* cur_at = w;
		while (true) {
			if (cur_at->parent) {
				cur_at = cur_at->parent;
			}else{
				break;
			}
			
			if (Editor* edtr = dynamic_cast<Editor*>(cur_at)) {
				activeEditor = edtr;
				break;
			}
		}
	}
	
	if (w != commandPalette) {
		commandUnfocused();
	}
	
	if (!w) {
		return;
	}
	
	if (w == commandPalette) { // we must have just moved to the commandPalette (let's clear it)
		if (auto edt = dynamic_cast<TextEdit*>(commandPalette)){
			edt->setFullText({});
			edt->mode = 'i'; // let's always go back to insert when going there
			
			// here let's move the cp_listbox to the rootwidget. yeah.
			App::MoveWidget(commandBox, rootelement);
			indexFiles();
			fillCmdBox();
		}
	}
}

void App::ext_button() {
	rerender = true;
	glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void App::moveMouse(int x, int y) {
	moveMouseToX = x;
	moveMouseToY = y;
}

void App::runWithSKIZ(int nx, int ny, int nw, int nh, VoidFunction withskiz) {
	int was_s_x = App::SKIZ_X;
	int was_s_y = App::SKIZ_Y;
	int was_s_w = App::SKIZ_W;
	int was_s_h = App::SKIZ_H;
	
	// using the maximum of the two top-left corners and the minimum of the two bottom-right corners
	
	int skiz_x2 = fmin(App::SKIZ_W+App::SKIZ_X, nx+nw);
	int skiz_y2 = fmin(App::SKIZ_H+App::SKIZ_Y, ny+nh);
	
	App::SKIZ_X = fmax(App::SKIZ_X, nx);
	App::SKIZ_Y = fmax(App::SKIZ_Y, ny);
	
	App::SKIZ_W = std::max(skiz_x2 - App::SKIZ_X, 0);
	App::SKIZ_H = std::max(skiz_y2 - App::SKIZ_Y, 0);
	
	glScissor(App::SKIZ_X, App::WINDOW_HEIGHT-(App::SKIZ_Y+App::SKIZ_H), App::SKIZ_W, App::SKIZ_H); // ensure no over drawwing between panels.
	
	withskiz();
	
	App::SKIZ_X = was_s_x;
	App::SKIZ_Y = was_s_y;
	App::SKIZ_W = was_s_w;
	App::SKIZ_H = was_s_h;
	
	glScissor(App::SKIZ_X, App::WINDOW_HEIGHT-(App::SKIZ_Y+App::SKIZ_H), App::SKIZ_W, App::SKIZ_H); // ensure no over drawwing between panels.
}

void App::repeatEveryXSeconds(int intervalSeconds, std::function<void()> task) {
	std::thread([intervalSeconds, task]() {
		while (running) {
			std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
			if (running) task();
		}
	}).detach();
}

void App::save() {
	std::cout << "Saving\n";
	rootelement->save();
}

void App::fixAllTmpFiles() {
	if (!settings->getValue("use_auto_tmp_clean", true)) return;
	
	std::string folderPath = settings->getValue("current_folder", getExecutableDir());
	
	if (!std::filesystem::exists(folderPath) || !std::filesystem::is_directory(folderPath)) {
		return;
	}
	
	std::regex tmpPattern(R"(.*\.tmp~\d+$|.*~RF.*\.TMP$|.*\.tmp$)", std::regex_constants::icase);
	
	auto now = std::chrono::system_clock::now();
	const int kSafeAgeSeconds = 300; // 5 minutes is safer than 20s
	
	try {
		for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
			if (!entry.is_regular_file()) continue;
			
			std::string fileName = entry.path().filename().string();
			
			if (std::regex_match(fileName, tmpPattern)) {
				auto ftime = std::filesystem::last_write_time(entry);
				
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
				);

				auto age = std::chrono::duration_cast<std::chrono::seconds>(now - sctp).count();
				
				if (age > kSafeAgeSeconds) {
					std::error_code ec;
					std::filesystem::remove(entry.path(), ec);
				}
			}
		}
	} catch (const std::exception&) {
		return;
	}
}

MST::MonoString App::readFileToMonoString(
	const std::string& filename,
	bool& worked,
	std::shared_ptr<FileBackend> backend) {
	worked = false;
	if (!backend) backend = FileBackends::current();

	auto appendUtf8 = [](std::string& output, uint32_t codepoint) {
		char encoded[4];
		const size_t length = grapheme_encode_utf8(codepoint, encoded, sizeof(encoded));
		if (length > 0) output.append(encoded, length);
	};

	auto isValidUtf8 = [](const std::uint8_t* data, size_t size) {
		size_t offset = 0;
		while (offset < size) {
			const std::uint8_t first = data[offset];
			size_t length = 0;
			uint32_t minimum = 0;
			uint32_t codepoint = 0;
			if (first < 0x80) {
				length = 1;
				codepoint = first;
			} else if ((first & 0xE0) == 0xC0) {
				length = 2; minimum = 0x80; codepoint = first & 0x1F;
			} else if ((first & 0xF0) == 0xE0) {
				length = 3; minimum = 0x800; codepoint = first & 0x0F;
			} else if ((first & 0xF8) == 0xF0) {
				length = 4; minimum = 0x10000; codepoint = first & 0x07;
			} else {
				return false;
			}
			if (offset + length > size) return false;
			for (size_t i = 1; i < length; ++i) {
				if ((data[offset + i] & 0xC0) != 0x80) return false;
				codepoint = (codepoint << 6) | (data[offset + i] & 0x3F);
			}
			if (codepoint < minimum || codepoint > 0x10FFFF ||
				(codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
			offset += length;
		}
		return true;
	};

	auto decodeBytes = [&](const std::vector<std::uint8_t>& bytes, std::string& utf8) {
		if (bytes.empty()) return true;

		size_t offset = 0;
		if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
			offset = 3;
		}
		if (offset != 0 || isValidUtf8(bytes.data(), bytes.size())) {
			utf8.assign(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
			return true;
		}

		const bool utf16le = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
		const bool utf16be = bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF;
		if (utf16le || utf16be) {
			for (size_t i = 2; i + 1 < bytes.size();) {
				auto read16 = [&](size_t pos) -> uint16_t {
					return utf16le
						? static_cast<uint16_t>(bytes[pos] | (bytes[pos + 1] << 8))
						: static_cast<uint16_t>((bytes[pos] << 8) | bytes[pos + 1]);
				};
				uint32_t cp = read16(i);
				i += 2;
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < bytes.size()) {
					const uint32_t low = read16(i);
					if (low >= 0xDC00 && low <= 0xDFFF) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
						i += 2;
					}
				}
				appendUtf8(utf8, cp);
			}
			return true;
		}

		const bool utf32le = bytes.size() >= 4 &&
			bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[2] == 0x00 && bytes[3] == 0x00;
		const bool utf32be = bytes.size() >= 4 &&
			bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xFE && bytes[3] == 0xFF;
		if (utf32le || utf32be) {
			for (size_t i = 4; i + 3 < bytes.size(); i += 4) {
				uint32_t cp = utf32le
					? static_cast<uint32_t>(bytes[i]) |
						(static_cast<uint32_t>(bytes[i + 1]) << 8) |
						(static_cast<uint32_t>(bytes[i + 2]) << 16) |
						(static_cast<uint32_t>(bytes[i + 3]) << 24)
					: (static_cast<uint32_t>(bytes[i]) << 24) |
						(static_cast<uint32_t>(bytes[i + 1]) << 16) |
						(static_cast<uint32_t>(bytes[i + 2]) << 8) |
						static_cast<uint32_t>(bytes[i + 3]);
				if (cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) appendUtf8(utf8, cp);
			}
			return true;
		}

#ifdef _WIN32
		const int wideLength = MultiByteToWideChar(
			CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()),
			static_cast<int>(bytes.size()), nullptr, 0);
		if (wideLength <= 0) return false;
		std::wstring wide(static_cast<size_t>(wideLength), L'\0');
		MultiByteToWideChar(
			CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()),
			static_cast<int>(bytes.size()), wide.data(), wideLength);
		const int utf8Length = WideCharToMultiByte(
			CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
		if (utf8Length <= 0) return false;
		utf8.resize(static_cast<size_t>(utf8Length));
		WideCharToMultiByte(
			CP_UTF8, 0, wide.data(), wideLength, utf8.data(), utf8Length, nullptr, nullptr);
#else
		for (const std::uint8_t byte : bytes) appendUtf8(utf8, byte);
#endif
		return true;
	};

	for (int attempt = 0; attempt < 5; attempt++) {
		std::vector<std::uint8_t> rawBuffer;
		std::string backendError;
		if (!backend->readFile(filename, rawBuffer, backendError)) {
			return MST::toMonoString("Failed to open file - " + backendError);
		}

		// Empty file on disk -> try again in case another process is writing it.
		if (rawBuffer.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(25));
			continue;
		}

		std::string utf8;
		if (!decodeBytes(rawBuffer, utf8)) {
			return MST::toMonoString("Failed to open file - unsupported text encoding");
		}
		MST::MonoString result = MST::toMonoString(utf8);
		result = MST::replaceAll(result, MST::toMonoString("\r\n"), MST::toMonoString("\n"));
		worked = true;
		return result;

		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}

	worked = true;
	return MST::MonoString{};
}

void App::launchCommandNonBlocking(const std::string& command) {
	TerminalWidget* activeTerminal = dynamic_cast<TerminalWidget*>(rootelement->findTerminal());
	if (activeTerminal != nullptr) {
		activeTerminal->runCommand(command);
		if (activeLeafNode != activeTerminal) {
			setActiveLeafNode(activeTerminal);
		}
		return;
	}

#ifdef _WIN32
	std::string params = "/k \"" + command + "\"";
	HINSTANCE result = ShellExecuteA(
		NULL, "open", "C:\\Windows\\System32\\cmd.exe",
		params.c_str(), NULL, SW_SHOW
	);
	if ((INT_PTR)result <= 32) {
		throw std::runtime_error(
			"Failed to launch cmd.exe (error code " +
			std::to_string((INT_PTR)result) + ")"
		);
	}
#else
	std::string escaped;
	for (char c : command) {
		if (c == '\'') escaped += "'\\''";
		else escaped += c;
	}
	std::string full_cmd = "x-terminal-emulator -e 'bash -c \"" + escaped + "; exec bash\"' 2>/dev/null || "
		"gnome-terminal -- bash -c '\"" + escaped + "; exec bash\"' 2>/dev/null || "
		"xterm -e 'bash -c \"" + escaped + "; exec bash\"' &";
	int ret = system(full_cmd.c_str());
	if (ret == -1) {
		throw std::runtime_error("Failed to launch terminal");
	}
#endif
}

void searchTheseFiles(std::atomic<bool>& stopFlag, SharedProgress& data) {
	std::string st = data.searchterm;
	
	if (FileBackends::isRemote()) {
		std::vector<std::string> searchPaths;
		
		{
			int maxlen = 300 < data.torun.size() ? 300 : data.torun.size();
			
			std::lock_guard<std::mutex> lock(data.mtx);
			searchPaths = std::vector<std::string>(
				data.torun.begin(),
				data.torun.begin() + maxlen
			);
			data.toptext = "Request sent... ("+std::to_string(searchPaths.size())+" files)";
			data.updateSomething = true;
		}
		std::vector<SearchedFile> remoteResults;
		std::string search_error;
		if (FileBackends::current()->searchFiles(searchPaths, st, remoteResults, search_error)) {
			std::lock_guard<std::mutex> lock(data.mtx);
			
			for (const auto& sf : remoteResults) {
				if (stopFlag.load(std::memory_order_relaxed)) { return; }
				
				data.storedsearches.push_back({
					1, // line 1 for some reason...
					sf.path,
					FileBackends::current()->filename(sf.path) // text to show
				});
				
				for (const auto& match : sf.matches) {
					data.storedsearches.push_back({
						match.line,
						sf.path,
						"    "+match.content
					});
				}
			}
			data.toptext = "Search Complete ("+std::to_string(searchPaths.size())+" files searched)";
			data.updateSomething = true;
		}else{
			std::lock_guard<std::mutex> lock(data.mtx);
			data.toptext = "Request failed.";
			data.updateSomething = true;
		}
		
		return;
	}
	
	unsigned char frstchr = st[0];
	bool works;
	unsigned char hc;
	int stlen = (int)st.size();
	
	for (size_t idx = 0; idx < data.torun.size(); ++idx) {
		if (stopFlag.load(std::memory_order_relaxed)) { return; }
		
		auto path = data.torun[idx];
		
		{
			std::lock_guard<std::mutex> lock(data.mtx);
			data.toptext = "Working... " + std::to_string(idx+1) + "/" + std::to_string(data.torun.size()) + " (" + FileBackends::current()->filename(path) + ")";
			data.updateSomething = true;
		}
		
		
		if (isBinaryFile(path)) { continue; }
		
		std::vector<std::uint8_t> bytes;
		std::string err;
		if (!FileBackends::current()->readFile(path, bytes, err)) { continue; }
		
		if (stopFlag.load(std::memory_order_relaxed)) { return; }
		
		std::string buf(bytes.begin(), bytes.end());
		
		int buflen = (int)buf.size();
		
		int lineNum = 1;
		int lineStart = 0;
		bool waitingforline = false;
		
		bool firstInFile = true;
		
		for (int i1 = 0; i1 < buflen-stlen+1; i1++) {
			if (stopFlag.load(std::memory_order_relaxed)) { return; }
			
			hc = ToLower[static_cast<unsigned char>(buf[i1])];
			
			if (hc == '\n') {
				lineNum ++;
				lineStart = i1+1; // we're not starting at i1 because let's not include \n in the lines
				waitingforline = false;
			}
			
			if (waitingforline || hc != frstchr) { continue; }
			
			works = true;
			for (int i2 = 1; i2 < stlen; i2++) {
				if (st[i2] != ToLower[static_cast<unsigned char>(buf[i1+i2])]) {
					works = false;
					break;
				}
			}
			
			if (works) {
				auto endLine = buf.find('\n', i1+stlen);
				if (endLine == 0) {
					endLine = buflen;
				}
				
				
				std::lock_guard<std::mutex> lock(data.mtx);
				
				if (stopFlag.load(std::memory_order_relaxed)) { return; }
				
				if (firstInFile) {
					data.storedsearches.push_back({
						1, // line 1 for some reason...
						path,
						FileBackends::current()->filename(path) // text to show
					});
					firstInFile = false;
				}
				
				data.storedsearches.push_back({
					lineNum,
					path,
					"    "+trim(buf.substr(lineStart, endLine-lineStart))
				});
				data.updateSomething = true;
				
				waitingforline = true;
				works = false;
			}
		}
	}
	
	std::lock_guard<std::mutex> lock(data.mtx);
	if (stopFlag.load(std::memory_order_relaxed)) { return; }
	data.toptext = "Search Complete ("+std::to_string(data.torun.size())+" files searched)";
	data.updateSomething = true;
}

void App::searchAcrossFiles(const std::string& searchTerm) {
	auto st = toLower(searchTerm);
	
	searchStopFlag->store(true, std::memory_order_relaxed);
	
	std::lock_guard<std::mutex> lock(storedsearches.mtx);
	
	storedsearches.storedsearches.clear();
	storedsearches.torun.clear();
	storedsearches.searchterm = st;
	storedsearches.updateSomething = true;
	storedsearches.added_already = 0;
	storedsearches.toptext = "Working... 0/" + std::to_string(INDEXED_FILES.fullPaths.size());
	
//	if (FileBackends::isRemote()) {
//		std::vector<std::string> searchPaths(
//			INDEXED_FILES.fullPaths.begin(),
//			INDEXED_FILES.fullPaths.begin() + maxlen);
//		std::vector<SearchedFile> remoteResults;
//		std::string search_error;
//		if (FileBackends::current()->searchFiles(searchPaths, searchTerm, remoteResults, search_error)) {
//			for (const auto& sf : remoteResults) {
//				SearchMatchVec matches;
//				for (const auto& m : sf.matches) {
//					matches.emplace_back(m.line, m.content);
//				}
//				out[{sf.path, sf.path}] = std::move(matches);
//			}
//		}
//		
//		return out;
//	}
	
	storedsearches.torun = INDEXED_FILES.fullPaths;
	
	// replace it with a new one
	searchStopFlag = new std::atomic<bool>(false);
	searchStopFlag->store(false, std::memory_order_relaxed);
	
	searchWorker = std::thread(searchTheseFiles, std::ref(*searchStopFlag), std::ref(storedsearches));
	searchWorker.detach();
}

void App::setTintedColor(Color* tint_c, Color* c, float b, float s) {
	if (tint_c->r == 1 && tint_c->g == 1 && tint_c->b == 1) {
		c->r = b;
		c->g = b;
		c->b = b;
		return;
	}
	
	float tcb = tint_c->r*0.299+tint_c->g*0.587+tint_c->b*0.114; // tint color brightness 
	
	if (tcb == 0) { // fix 0,0,0 breaking things
		tint_c->r = 0.1;
		tint_c->g = 0.1;
		tint_c->b = 0.1;
		
		tcb = 0.1;
	}else if (tcb < 0.1) { // raise the brightness to 0.1
		tint_c->r *= 0.1/tcb;
		tint_c->g *= 0.1/tcb;
		tint_c->b *= 0.1/tcb;
		
		tcb = 0.1;
	}
	
	float scale = s*b;
	float intercept = (1-s)*b;
	
	float new_r = fmin(1.0, tint_c->r*scale + intercept);
	float new_g = fmin(1.0, tint_c->g*scale + intercept);
	float new_b = fmin(1.0, tint_c->b*scale + intercept);
	
	c->r = new_r;
	c->g = new_g;
	c->b = new_b;
}

void App::updateFromTintColor(Theme* t) {
	float s = settings->getValue("c_saturation", 0.6f);
	
	if (darkmode) {
		setTintedColor(t->tint_color, t->main_background_color,    0.098039, s);
		setTintedColor(t->tint_color, t->extras_background_color,  0.164706, s);
		setTintedColor(t->tint_color, t->hover_background_color,   0.26,     s);
		setTintedColor(t->tint_color, t->main_text_color,          1.0,      s);
		setTintedColor(t->tint_color, t->active_color,             0.7,      1.0);
		setTintedColor(t->tint_color, t->border,                   0.35,     s);
		setTintedColor(t->tint_color, t->syntax_colors[0],         1.0,      s);
		setTintedColor(t->tint_color, t->darker_background_color,  0.05,     s);
		setTintedColor(t->tint_color, t->overlay_background_color, 0.12,     s);
		setTintedColor(t->tint_color, t->lesser_text_color,        0.392157, s);
	}else{
		setTintedColor(t->tint_color, t->main_background_color,    0.7,      s);
		setTintedColor(t->tint_color, t->extras_background_color,  0.9,      s);
		setTintedColor(t->tint_color, t->hover_background_color,   0.8,      s);
		setTintedColor(t->tint_color, t->main_text_color,          0.0,      s);
		setTintedColor(t->tint_color, t->active_color,             0.0,      1.0);
		setTintedColor(t->tint_color, t->border,                   0.6,      s);
		setTintedColor(t->tint_color, t->syntax_colors[0],         0.0,      s);
		setTintedColor(t->tint_color, t->darker_background_color,  1.0,      s);
		setTintedColor(t->tint_color, t->overlay_background_color, 0.95,     s);
		setTintedColor(t->tint_color, t->lesser_text_color,        0.5,      s);
	}
	
	rootelement->executeAction(WidgetActionType::THEME_CALCULATED);
}

void App::displayToast(MST::MonoString text) {
	if (auto toaster = dynamic_cast<Toast*>(toastBox)) {
		toaster->displayMessage(text);
	}
	time_till_regular = 2;
}

void App::displayText(MST::MonoString text) {
	if (auto sn = dynamic_cast<ScrollNotify*>(scrollNotifyBox)) {
		sn->displayMessage(text);
	}
	time_till_regular = 2;
}

void App::closeMenu() {
	App::RemoveWidgetFromParent(menu);
	reclear = 3;
	App::time_till_regular = 2;
}

void App::openMenu(int x_offset) {
	if (auto ctxMenu = dynamic_cast<ContextMenu*>(menu)) {
		ctxMenu->x_loc = x_offset + 2;
		ctxMenu->y_loc = rootelement->t_y + 2;
	}
	
	MoveWidget(menu, rootelement);
	reclear = 3;
	App::time_till_regular = 2;
}