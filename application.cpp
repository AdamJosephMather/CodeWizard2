#include "application.h"
#include <GLFW/glfw3.h>
#include <queue>
#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include <iostream>
#include "scrollnotify.h"
#include "codeedit.h"
#include "editor.h"
#include "helper_types.h"
#include <vector>
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

#include <windows.h>
#include <windowsx.h>  // This header contains GET_X_LPARAM and GET_Y_LPARAM
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <fstream>
#include <unicode/ucsdet.h>   // CharsetDetector
#include <unicode/ucnv.h>     // UConverter, ucnv_open/close

#include <stb_image.h>
#include "curler.h"
#include "myrect.h"
#include "label.h"
#include "updatechecker.h"

#include <unicode/ustring.h>
#include "Verify.hpp"



int App::major_version = 2;
int App::minor_version = 2;
int App::patch_version = 9;


float M_PI = 3.141592653589793238;

HWND App::window_handle = nullptr;

bool App::darkmode = true;
std::string App::empty = "";

icu::UnicodeString App::vnum = icu::UnicodeString();
std::string App::vnumstr = "";

int App::moveMouseToX = -1;
int App::moveMouseToY = -1;

Widget* App::helpMenu = nullptr;

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
std::vector<StoredSearch> App::storedsearches = {};

Widget* App::commandPalette = nullptr;
Widget* App::filesButton = nullptr;
Widget* App::filesList = nullptr;
Widget* App::commandBox = nullptr;
Widget* App::toastBox = nullptr;
Widget* App::scrollNotifyBox = nullptr;

std::vector<std::vector<std::string>> App::files_in_box = {};

Widget* App::before_reps_request = nullptr;

int App::WINDOW_WIDTH = 1200;
int App::WINDOW_HEIGHT = 800;
std::string App::WINDOW_TITLE = "CodeWizard2 V";

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
Widget* App::rootelement = nullptr;

int App::text_padding = 5;
int App::border_width = 1;

Color* App::bgcolor = MakeColor(0.5, 0.5, 0.5);

WNDPROC App::originalWndProc;
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

void restoreWindowPosAndSize(GLFWwindow* window, SettingsManager* settings, int screenWidth, int screenHeight) {
	int w = settings->getValue("window_width", 1200);
	int h = settings->getValue("window_height", 800);

	int x = settings->getValue("window_x", screenWidth / 2 - w / 2);
	int y = settings->getValue("window_y", screenHeight / 2 - h / 2);
	
	RECT winRect{ x, y, x + w, y + h };
	if (!rectOnAnyMonitorWorkArea(winRect)) {
		x = screenWidth/2-w/2;
		y = screenHeight/2-h/2;
	}
	
	glfwSetWindowSize(window, w, h);
	glfwSetWindowPos(window, x, y);
}

bool App::Init() {
	std::cout << "Init...\n";
	
	icu::UnicodeString vnum = icu::UnicodeString::fromUTF8(std::to_string(App::major_version)+"."+std::to_string(App::minor_version)+"."+std::to_string(App::patch_version));
	vnum.toUTF8String(vnumstr);
	WINDOW_TITLE += vnumstr;
	
	STRING_REQUEST_TEXTEDIT = new TextEdit(nullptr, [&](Widget* w){
		w->t_x = w->t_x+w->t_w/2-(w->t_w/4);
		w->t_w /= 2;
		int new_h = TextRenderer::get_text_height()+text_padding*2;
		w->t_y = w->t_y+w->t_h/2-(new_h/2);
		w->t_h = new_h;
		STRING_REQUEST_RECTANGLE->position(w->t_x, w->t_y, w->t_w, w->t_h);
	});
	
	STRING_REQUEST_RECTANGLE = new MyRect(nullptr, [&](Widget* w){
		int h = TextRenderer::get_text_height()+text_padding*2;
		w->t_x = STRING_REQUEST_TEXTEDIT->t_x-h;
		w->t_w = STRING_REQUEST_TEXTEDIT->t_w+h*2;
		w->t_y = STRING_REQUEST_TEXTEDIT->t_y-h;
		w->t_h = STRING_REQUEST_TEXTEDIT->t_h+h*2;
	});
	
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
	
	settings->loadSettings();
	
	std::string password = settings->getValue("ajm_asv3_password", (std::string)"devpassword"); // must happen after settings setup.
	
	Verify::setup(password);
	
	darkmode = settings->getValue("dark_mode", true);
	WINDOW_WIDTH = settings->getValue("window_width", 1200);
	WINDOW_HEIGHT = settings->getValue("window_height", 800);
	
	EnablePerMonitorDpiAwareness();
	
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return false;
	}
	
	glfwWindowHint(GLFW_SAMPLES, 4); // request 4 samples
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	
	window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE.c_str(), nullptr, nullptr);
	
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
	
//	glEnable(GL_MULT);
	
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

//	int x = settings->getValue("window_x", screenWidth / 2 - WINDOW_WIDTH / 2);
//	int y = settings->getValue("window_y", screenHeight / 2 - WINDOW_HEIGHT / 2);
	
	restoreWindowPosAndSize(window, settings, screenWidth, screenHeight);
	
//	glfwSetWindowPos(window, x, y);
	
	TextRenderer::after_font_change = [&](){
		text_padding = std::min(TextRenderer::get_text_width(1), TextRenderer::get_text_height()) * 0.5;
	};
	
	TextRenderer::set_font_size(settings->getValue("font_size", 23.0f));
	std::string default_font_path = getExecutableDir()+"\\cascadia\\CascadiaCode-Regular.ttf";
	std::string font_path = settings->getValue("font_path", default_font_path);
	
	bool success = TextRenderer::init_font(font_path.c_str()); // Or whatever .ttf you have
	
	if (!success) {
		TextRenderer::init_font(default_font_path.c_str());
	}
	
	// setup titlebar
	
	rootelement = new Widget(nullptr);
	new PanelHolder(rootelement);
	tb = new TitleBar(rootelement);
	toastBox = new Toast(nullptr);
	
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
	
	lastTime = glfwGetTime();
	
	repeatEveryXSeconds(4, [&](){
		std::lock_guard<std::mutex> lock(canMakeChanges);
		save();
	});
	
	
	// app icon
	
	auto pth_str = getExecutableDir()+"\\app.png";
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
	
	return true;
}

void App::updateTransparency(bool transparent) {
	// 1) Make sure window is NOT layered via glfwSetWindowOpacity.
	//    Keep glfwSetWindowOpacity(window, 1.0f) and use your per-pixel alpha instead.
	
	HMODULE hUser = GetModuleHandleW(L"user32.dll");
	if (!hUser) return;

	auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
		GetProcAddress(hUser, "SetWindowCompositionAttribute")
	);
	if (!setWCA) return;

	ACCENT_POLICY accent = {};
	if (transparent) {
		// Plain blur:
		accent.accentState = ACCENT_ENABLE_BLURBEHIND;

		// Or, for stronger Win10-style acrylic:
		// accent.accentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
		// accent.gradientColor = 0xCC000000; // AARRGGBB tint (here: semi-transparent black)
	} else {
		accent.accentState = ACCENT_DISABLED;
	}

	WINDOWCOMPOSITIONATTRIBDATA data = {};
	data.Attribute = WCA_ACCENT_POLICY;
	data.Data      = &accent;
	data.SizeOfData= sizeof(accent);

	setWCA(window_handle, &data);
}

void App::checkForUpdates() {
	std::vector<int> latest = UpdateChecker::getLatestVersion();
	if (latest.size() != 3) {
		return;
	}
	
	int f1 = latest[0]-major_version;
	int f2 = latest[1]-minor_version;
	int f3 = latest[2]-patch_version;
	
	if (f1 > 0 || (f1 == 0 && f2 > 0) || (f1 == 0 && f2 == 0 && f3 > 0)) {
		displayToast(icu::UnicodeString::fromUTF8("There is a new version of CodeWizard available!"));
	}else if (f1 < 0 || (f1 == 0 && f2 < 0) || (f1 == 0 && f2 == 0 && f3 < 0)) {
		displayToast(icu::UnicodeString::fromUTF8("This CodeWizard is ahead of the latest release!"));
	}else{
//		displayToast(icu::UnicodeString::fromUTF8("This is the latest release!"));
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
	curr_removing_panel = false;
	curr_adding_panel = true;
}

void App::removing_panel() {
	rerender = true;
	curr_adding_panel = false;
	curr_removing_panel = true;
}

void App::nada_panel() {
	rerender = true;
	curr_removing_panel = false;
	curr_adding_panel = false;
}

LRESULT CALLBACK App::CustomWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_NCHITTEST: {
			// Define hit test areas (titlebar, resize borders, etc.)
			POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			ScreenToClient(hwnd, &pt);
			
			// Get window dimensions
			RECT rect;
			GetClientRect(hwnd, &rect);
			int windowWidth = rect.right - rect.left;
			int windowHeight = rect.bottom - rect.top;
			
			// Define border thickness for resize areas (only for windowed mode)
			const int BORDER_THICKNESS = 8;
			
			bool isZoomed = IsZoomed(hwnd);
			
			// Check for titlebar area first (works for both maximized and windowed)
			if ((pt.y > BORDER_THICKNESS || isZoomed) && pt.y < tb->t_h && tb->is_out_of_child(pt.x)) {
				return HTCAPTION; // Makes it draggable
			}
			
			// Skip resize border hit testing when maximized/fullscreen
			if (isZoomed) {
				return HTCLIENT; // Everything else is client area when maximized
			}
			
			// Check for corner resize areas first (they take priority)
			// Top-left corner
			if (pt.x < BORDER_THICKNESS && pt.y < BORDER_THICKNESS) {
				return HTTOPLEFT;
			}
			// Top-right corner
			if (pt.x > windowWidth - BORDER_THICKNESS && pt.y < BORDER_THICKNESS) {
				return HTTOPRIGHT;
			}
			// Bottom-left corner
			if (pt.x < BORDER_THICKNESS && pt.y > windowHeight - BORDER_THICKNESS) {
				return HTBOTTOMLEFT;
			}
			// Bottom-right corner
			if (pt.x > windowWidth - BORDER_THICKNESS && pt.y > windowHeight - BORDER_THICKNESS) {
				return HTBOTTOMRIGHT;
			}
			
			// Check for edge resize areas
			// Left edge
			if (pt.x < BORDER_THICKNESS) {
				return HTLEFT;
			}
			// Right edge
			if (pt.x > windowWidth - BORDER_THICKNESS) {
				return HTRIGHT;
			}
			// Top edge
			if (pt.y < BORDER_THICKNESS) {
				return HTTOP;
			}
			// Bottom edge
			if (pt.y > windowHeight - BORDER_THICKNESS) {
				return HTBOTTOM;
			}
			
			// Default to client area
			return HTCLIENT;
		}
		
		case WM_PAINT: {
			rerender = true;
			DoFullRenderWithoutInput();
			ValidateRect(hwnd, NULL);
			return 0;
		}

		case WM_SETCURSOR: {
			// Skip custom cursor handling when maximized/fullscreen
			if (IsZoomed(hwnd)) {
				return CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
			}
			
			// LOWORD(lParam) is the hit test code the system last determined
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
		
			// Work area is the monitor minus taskbar/docked bars.
			const RECT& rcWork = mi.rcWork;
			const RECT& rcMon  = mi.rcMonitor;
		
			// ptMaxPosition is RELATIVE TO THE MONITOR (not virtual desktop).
			mmi->ptMaxPosition.x = rcWork.left - rcMon.left;
			mmi->ptMaxPosition.y = rcWork.top  - rcMon.top;
		
			mmi->ptMaxSize.x = rcWork.right  - rcWork.left;
			mmi->ptMaxSize.y = rcWork.bottom - rcWork.top;
		
			// Optional but often helps prevent “extra” stretching:
			mmi->ptMaxTrackSize = mmi->ptMaxSize;
			return 0;
		}case WM_MOVING: {
		}case WM_SIZING: {
			// Let Windows do its thing first
			CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
		
			// lParam is a RECT* for the new window position
			RECT* rc = reinterpret_cast<RECT*>(lParam);
			int newW = rc->right  - rc->left;
			int newH = rc->bottom - rc->top;
		
			// Force our OpenGL viewport and projection to update
			App::resize_callback(App::window, newW, newH);
			InvalidateRect(hwnd, NULL, FALSE);
		
			// Consume the message
			return TRUE;
		}case WM_DPICHANGED: {
			// Windows tells you the correct new window rect in lParam
			const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
			SetWindowPos(hwnd, NULL,
				suggested->left,
				suggested->top,
				suggested->right - suggested->left,
				suggested->bottom - suggested->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}
	}
	
	return CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
}

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
	// Fill Logic (Original implementation)
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

void App::DoFullRenderWithoutInput() {
	double currentTime = glfwGetTime();
	frameCount++;
	
	if (currentTime - lastTime >= 5.0) {
		float fps = (double)frameCount/(currentTime-lastTime);
		
		std::string fps_str = std::to_string(fps);
		
		std::cout << "FPS: " << fps_str << std::endl;
		frameCount = 0;
		lastTime = currentTime;
		
		if (settings->getValue("show_fps", false)) {
			displayText(icu::UnicodeString::fromUTF8("FPS: " + fps_str));
		}
	}
	
	if (moveMouseToX != -1 && moveMouseToY != -1) {
		glfwSetCursorPos(window, moveMouseToX, moveMouseToY);
		moveMouseToX = -1;
		moveMouseToY = -1;
	}
	
	rendering_add_rect = false;
	rendering_rem_rect = false;
	
	expectedCursorType = -1; // must be reset every position call
	std::lock_guard<std::mutex> lock(canMakeChanges); // this prevents separate threads (the lsp clients) from messing with shit while positioning/rendering
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
	
	lastUpdate = currentTime;
	
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); // we need to overwrite everything now (rgb and a)
	
	if (settings->getValue("use_transparency", false)) {
		glClearColor(bgcolor->r, bgcolor->g, bgcolor->b, 0.65f); // reduce opacity to .65
	}else{
		glClearColor(bgcolor->r, bgcolor->g, bgcolor->b, 1.0f);
	}
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
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
	
	glfwSwapBuffers(window);
}

void App::MoveWidget(Widget* w, Widget* new_parent) {
	rerender = true;
	
	RemoveWidgetFromParent(w);
	w->parent = new_parent;
	new_parent->children.push_back(w);
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
					displayToast(icu::UnicodeString::fromUTF8("Finished Executing Macro"));
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
	
	int mx = mouseX;
	int my = mouseY;
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_mouse_button_event(button, action, mods)) { return; }
	}
	
	if (commandBox->parent != nullptr) {
		if (mx >= commandBox->t_x && mx <= commandBox->t_x+commandBox->t_w && my >= commandBox->t_y && my <= commandBox->t_y+commandBox->t_h) {
			commandBox->on_mouse_button_event(button, action, mods);
			return;
		}
	}
	if (filesList->parent != nullptr) {
		if (mx >= filesList->t_x && mx <= filesList->t_x+filesList->t_w && my >= filesList->t_y && my <= filesList->t_y+filesList->t_h) {
			filesList->on_mouse_button_event(button, action, mods);
			return;
		}else if (action == GLFW_PRESS) {
			closeFilesList();
			return;
		}
	}
	if (REQUESTING_STRING) {
		if (mx >= STRING_REQUEST_TEXTEDIT->t_x && mx <= STRING_REQUEST_TEXTEDIT->t_x+STRING_REQUEST_TEXTEDIT->t_w && my >= STRING_REQUEST_TEXTEDIT->t_y && my <= STRING_REQUEST_TEXTEDIT->t_y+STRING_REQUEST_TEXTEDIT->t_h) {
			STRING_REQUEST_TEXTEDIT->on_mouse_button_event(button, action, mods);
			return;
		}else if (action == GLFW_PRESS){
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_RECTANGLE);
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			RemoveWidgetFromParent(STRING_REQUEST_LABEL);
			setActiveLeafNode(nullptr);
		}else{
			return;
		}
	}
	
	if (action == GLFW_PRESS && activeLeafNode == commandPalette && (mx < commandPalette->t_x || my < commandPalette->t_y || mx > commandPalette->t_x+commandPalette->t_w || my > commandPalette->t_y+commandPalette->t_h)) {
		setActiveLeafNode(beforeCommandLeafNode);
	}
	
	if (rootelement) { rootelement->on_mouse_button_event(button, action, mods); }
}

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	rerender = true;
	
	if (recording_macro) {
		if ((key == GLFW_KEY_F12 || key == GLFW_KEY_F11) && action == GLFW_PRESS) {
			recording_macro = false;
			displayToast(icu::UnicodeString::fromUTF8("Macro Recording Over ("+std::to_string(keyboard_events.size())+")"));
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
			displayToast(icu::UnicodeString::fromUTF8("Stopped Macro Replay"));
			glfwSwapInterval(1); // Enable vsync
			replaying_macro = false;
		}else{
			displayToast(icu::UnicodeString::fromUTF8("Starting Macro Recording"));
			recording_macro = true;
			keyboard_events.clear();
			keyboard_events.push_back({});
		}
		return;
	}
	
	if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
		if (!replaying_macro) {
			if (keyboard_events.size() == 0) {
				displayToast(icu::UnicodeString::fromUTF8("No Recorded Keystrokes"));
				return;
			}
			displayToast(icu::UnicodeString::fromUTF8("Replaying Macro Recording"));
			
			ON_STRING_GIVEN = [&](icu::UnicodeString str){
				setActiveLeafNode(before_reps_request);
				
				if (str.length() == 0) {
					displayToast(icu::UnicodeString::fromUTF8("Canceled"));
					return;
				}
				
				std::string as_string;
				str.toUTF8String(as_string);
				
				try {
					rep_count = std::stoi(as_string);
				}catch(const std::invalid_argument& e) {
					displayToast(icu::UnicodeString::fromUTF8("Invalid Number, Canceled"));
					return;
				}
				
				if (rep_count < 0) {
					displayToast(icu::UnicodeString::fromUTF8("Invalid Number, Canceled"));
					return;
				}else if (rep_count == 0) {
					rep_count = -1;
				}
				
				glfwSwapInterval(0); // Enable vsync
				replaying_macro = true;
				current_step = 0;
			};
			REQUESTING_STRING = true;
			STRING_REQUEST_TEXTEDIT->setFullText(icu::UnicodeString());
			STRING_REQUEST_TEXTEDIT->mode = 'i';
			STRING_REQUEST_LABEL->setFullText(icu::UnicodeString::fromUTF8("Number of Repetitions (0 for Infinite)?"));
			MoveWidget(STRING_REQUEST_RECTANGLE, rootelement);
			MoveWidget(STRING_REQUEST_TEXTEDIT, rootelement);
			MoveWidget(STRING_REQUEST_LABEL, rootelement);
			before_reps_request = activeLeafNode;
			setActiveLeafNode(STRING_REQUEST_TEXTEDIT);
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
			if (ON_STRING_GIVEN) {
				ON_STRING_GIVEN(STRING_REQUEST_TEXTEDIT->getFullText());
			}
			return;
		}if (key == GLFW_KEY_ESCAPE && (action == GLFW_PRESS || action == GLFW_REPEAT) && (STRING_REQUEST_TEXTEDIT->mode == 'n' || !settings->getValue("use_vim", false))) {
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_RECTANGLE);
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			RemoveWidgetFromParent(STRING_REQUEST_LABEL);
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
		return;
	}if (action == GLFW_PRESS && key == GLFW_KEY_O && control && shift) {
		std::string fldr = settings->getValue("current_folder", std::string());
		
		const char * fpr = tinyfd_selectFolderDialog(
			"Open folder?",
			fldr.c_str()
		);
		
		if (fpr) {
			setFolder(fpr);
		}
		commandUnfocused();
		return;
	}else if (action == GLFW_PRESS && key == GLFW_KEY_S && control && !shift) {
		displayText(icu::UnicodeString::fromUTF8("Saving..."));
		save();
	}else if (action == GLFW_PRESS && key == GLFW_KEY_F5) {
		displayText(icu::UnicodeString::fromUTF8("Saving..."));
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
		icu::UnicodeString selectedStr = icu::UnicodeString::fromUTF8("");
		if (auto te = dynamic_cast<TextEdit*>(activeLeafNode)) {
			selectedStr = te->getSelectedText(te->cursors[0]);
		}
		
		setActiveLeafNode(commandPalette);
		auto cp = dynamic_cast<TextEdit*>(commandPalette);
		cp->setFullText("&");
		cp->cursors = { {0, 1, 0, 1, 1} };
		cp->mode = 'i';
		
		if (selectedStr.length() != 0) {
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
		std::string default_font_path = getExecutableDir()+"\\cascadia\\CascadiaCode-Regular.ttf";
		std::string font_path = App::settings->getValue("font_path", default_font_path);
		bool success = TextRenderer::init_font(font_path.c_str());
		
		if (!success) {
			TextRenderer::init_font(default_font_path.c_str());
		}
		
		displayText(icu::UnicodeString::fromUTF8(std::to_string(new_v)));
	}else if (action == GLFW_PRESS && key == GLFW_KEY_MINUS && control) {
		float new_v = App::settings->getValue("font_size", 23.0f) - 1;
		
		if (new_v < 8) {
			new_v = 8;
		}
		
		settings->setValue("font_size", new_v);
		
		TextRenderer::set_font_size(new_v);
		std::string default_font_path = getExecutableDir()+"\\cascadia\\CascadiaCode-Regular.ttf";
		std::string font_path = App::settings->getValue("font_path", default_font_path);
		bool success = TextRenderer::init_font(font_path.c_str());
		
		if (!success) {
			TextRenderer::init_font(default_font_path.c_str());
		}
		
		displayText(icu::UnicodeString::fromUTF8(std::to_string(new_v)));
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

void App::setFolder(std::string fpr) {
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
	
	int mx = mouseX;
	int my = mouseY;
	
	if (helpMenu->parent != nullptr) {
		if (helpMenu->on_scroll_event(-xpos, -ypos)) { return; }
	}
	
	if (commandBox->parent != nullptr) {
		if (mx >= commandBox->t_x && mx <= commandBox->t_x+commandBox->t_w && my >= commandBox->t_y && my <= commandBox->t_y+commandBox->t_h) {
			commandBox->on_scroll_event(-xpos, -ypos);
			return;
		}
	}
	if (filesList->parent != nullptr) {
		if (mx >= filesList->t_x && mx <= filesList->t_x+filesList->t_w && my >= filesList->t_y && my <= filesList->t_y+filesList->t_h) {
			filesList->on_scroll_event(-xpos, -ypos);
			return;
		}
	}
	if (REQUESTING_STRING) {
		if (mx >= STRING_REQUEST_TEXTEDIT->t_x && mx <= STRING_REQUEST_TEXTEDIT->t_x+STRING_REQUEST_TEXTEDIT->t_w && my >= STRING_REQUEST_TEXTEDIT->t_y && my <= STRING_REQUEST_TEXTEDIT->t_y+STRING_REQUEST_TEXTEDIT->t_h) {
			STRING_REQUEST_TEXTEDIT->on_scroll_event(-xpos, -ypos);
			return;
		}
	}
	
	if (rootelement) { rootelement->on_scroll_event(-xpos, -ypos); }
}

void App::resize_callback(GLFWwindow* window, int width, int height) {
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
	
	rerender = true;
	// forceWaitTime = true;
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
	HWND hwnd = glfwGetWin32Window(window);
	if (IsZoomed(hwnd)) {
		// already maximized -> restore
		ShowWindow(hwnd, SW_RESTORE);
	} else {
		// not maximized -> maximize
		ShowWindow(hwnd, SW_MAXIMIZE);
	}
}

void App::commandUnfocused() {
	if (auto com_p = dynamic_cast<TextEdit*>(commandPalette)) {
		if (auto editor = dynamic_cast<Editor*>(activeEditor)) {
			com_p->setFullText(editor->getPaletteName());
		}else{
			std::string folder = settings->getValue("current_folder", getExecutableDir());
			std::filesystem::path p(folder);
			com_p->setFullText(icu::UnicodeString::fromUTF8(p.filename().string()));
		}
	}
	
	// here let's remove the cp_listbox from the rootwidget
	if (commandBox && commandBox->parent != nullptr) {
		RemoveWidgetFromParent(commandBox);
	}
}

void App::closeFilesList() {
	if (filesList && filesList->parent != nullptr) {
		RemoveWidgetFromParent(filesList);
	}
}

void App::openFilesList() {
	if (!filesList) { return; }
	
	if (filesList->parent == nullptr) {
		App::MoveWidget(filesList, rootelement);
	}
	
	files_in_box = rootelement->getOpenFiles(false);
	
	if (auto lb = dynamic_cast<ListBox*>(filesList)) {
		std::vector<icu::UnicodeString> items;
		for (auto v : files_in_box) {
			items.push_back(icu::UnicodeString::fromUTF8(v[0]));
		}
		
		lb->setElements(items);
		lb->toshow = std::min(12, (int)items.size());
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
		
		if (data.length() == 0){
			return;
		}
		
		std::string str;
		data.toUTF8String(str);
		
		SetClipboardText(str);
		cmdpl->setFullText(data);
		cmdpl->cursors[0].head_line = cmdpl->lines.size()-1;
		cmdpl->cursors[0].head_char = cmdpl->lines[cmdpl->cursors[0].head_line].line_text.length();
		
		return;
	}else if (cur_type == 2){
		if (beforeCommandLeafNode) {
			setActiveLeafNode(beforeCommandLeafNode);
		}
		
		auto itm = storedsearches[cur_sel];
		
		std::filesystem::path p(itm.path);
		openFromCMD(itm.path, p.filename().string(), itm.line);
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
		if (filepath == ":Git Push") {
			ON_STRING_GIVEN = [&](icu::UnicodeString str){
				if (str.length() == 0) {
					return;
				}
				
				std::string mes;
				str.toUTF8String(mes);
				
				std::string folder = settings->getValue("current_folder", getExecutableDir());
				launchCommandNonBlocking("cd /d "+folder+" && git add . && git commit -m \""+mes+"\" && git push");
			};
			REQUESTING_STRING = true;
			STRING_REQUEST_TEXTEDIT->setFullText(icu::UnicodeString());
			STRING_REQUEST_TEXTEDIT->mode = 'i';
			STRING_REQUEST_LABEL->setFullText(icu::UnicodeString::fromUTF8("Git Commit Message?"));
			MoveWidget(STRING_REQUEST_RECTANGLE, rootelement);
			MoveWidget(STRING_REQUEST_TEXTEDIT, rootelement);
			MoveWidget(STRING_REQUEST_LABEL, rootelement);
			setActiveLeafNode(STRING_REQUEST_TEXTEDIT);
		}else if (filepath == ":Git Pull") {
			std::string folder = settings->getValue("current_folder", getExecutableDir());
			launchCommandNonBlocking("cd /d "+folder+" && git pull");
		}else if (filepath == ":Git Force Pull") {
			std::string folder = settings->getValue("current_folder", getExecutableDir());
			launchCommandNonBlocking(
				"pushd \"" + folder + "\" && "
				"git merge --abort 2>nul & git rebase --abort 2>nul & git cherry-pick --abort 2>nul & "
				"git fetch --prune origin && "
				"git reset --hard @{u} && "
				"popd"
			);
		}else if (filepath == ":Help") {
			MoveWidget(helpMenu, rootelement);
		}else if (filepath == ":Save Theme Settings To File") {
			const char * fp = tinyfd_saveFileDialog(
				"Save as?", // dialog title
				"CodeWizard2Theme.json", // default path and filename
				0, NULL, // filter count and filters
				0 // allow multiple selections (0 = no)
			);
			
			if (fp) {
				std::string filePath(fp);
				
				std::string tosave = settings->getSubSet({"dark_mode", "c_comments_color", "c_functs_color", "c_keywords_color", "c_literals_color", "c_punctuation_color", "c_strings_color", "c_tint_color", "c_types_color", "c_vars_color"});
				std::string err;
				if (!atomicWriteReplace(filePath, tosave, &err)) {
					displayToast(icu::UnicodeString::fromUTF8("Failed to write file: "+err));
				}else{
					displayToast(icu::UnicodeString::fromUTF8("Saved theme to file!"));
				}
			}
		}else if (filepath == ":Load Theme Settings From File") {
			const char * fp = tinyfd_openFileDialog(
				"Select theme",    // dialog title
				"",                 // default path and filename
				0, NULL, NULL,      // filter count and filters
				0                   // allow multiple selections (0 = no)
			);
			
			if (fp) {
				std::string filePath(fp);
				bool worked = false;
				icu::UnicodeString text = readFileToUnicodeString(filePath, worked);
				if (!worked) {
					displayToast(icu::UnicodeString::fromUTF8("Could not read file. ")+text);
				}else{
					std::string str;
					text.toUTF8String(str);
					
					if (!settings->bringInSubset(str)) {
						displayToast(icu::UnicodeString::fromUTF8("Could not load settings."));
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
						displayToast(icu::UnicodeString::fromUTF8("Done!"));
						
						App::rootelement->executeAction(WidgetActionType::SETTINGS_CHANGE);
					}
				}
			}
		}else if(filepath == ":Test Toast Box"){
			displayToast(icu::UnicodeString::fromUTF8("Example Toast Message."));
		}else if(filepath == ":Test Text Line"){
			displayText(icu::UnicodeString::fromUTF8("Example Text Line Message."));
		}else if (filepath == ":Restart Language Servers (LSPs)") {
			restartLSPs();
		}else if (filepath == ":Open `languages.json` file") {
			std::string path = settings->getLocalAppDataPath() + "\\CodeWizard\\languages.json";
			openFromCMD(path, "languages.json");
			displayToast(icu::UnicodeString::fromUTF8("Remember to reopen CodeWizard after making changes."));
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
	std::cout << "Opening from cmd..." << std::endl;
	
	FileInfo* finfo = new FileInfo();
	finfo->filepath = filepath;
	finfo->filename = filename;
	
	if (auto edtr = dynamic_cast<Editor*>(rootelement->fileOpen(filepath))) { // first check if *an* editor currently has it open
		if (line != -1) {
			edtr->fileOpenRequested(finfo, line, 0, line, 0);
		}else{
			edtr->fileOpenRequested(finfo);
		}
	}else if (auto edtr = dynamic_cast<Editor*>(beforeCommandLeafNode)) { // then did we just come from an editor?
		if (line != -1) {
			edtr->fileOpenRequested(finfo, line, 0, line, 0);
		}else{
			edtr->fileOpenRequested(finfo);
		}
	}else if (auto edtr = dynamic_cast<Editor*>(activeEditor)) {
		if (line != -1) {
			edtr->fileOpenRequested(finfo, line, 0, line, 0);
		}else{
			edtr->fileOpenRequested(finfo);
		}
	}else {
		Widget* wdgt = rootelement->getFirstEditor();
		if (auto edtr = dynamic_cast<Editor*>(wdgt)) {
			if (line != -1) {
				edtr->fileOpenRequested(finfo, line, 0, line, 0);
			}else{
				edtr->fileOpenRequested(finfo);
			}
		}else{
			std::cout << "Fuck. Just fuck." << std::endl;
			// we have no choice in this matter (there is no open editor on which we can call.)
		}
	}
}

void App::indexFiles() {
	std::string rootPath = settings->getValue("current_folder", getExecutableDir());
	
	INDEXED_FILES.indexedNames.clear();
	INDEXED_FILES.displayPaths.clear();
	INDEXED_FILES.fullPaths.clear();
	INDEXED_FILES.currentlyshowing.clear();
	INDEXED_FILES.currentlyshowingtype.clear();
	
	std::size_t maxFiles              = settings->getValue("max_index_files", 2000);
	std::size_t maxDisplayChars       = (commandPalette->t_w-text_padding*2)/TextRenderer::get_text_width(1)-1;
	
	
	std::queue<std::string> dirs;
	dirs.push(rootPath);
	
	const std::size_t rootLen = rootPath.size() + 1; // for the ‘/’ or ‘\’
	std::size_t seen = 0;
	
	std::set<std::string> dontshowagain;
	
	files_in_box = rootelement->getOpenFiles(false);
	for (auto fInfo : files_in_box) {
		if (fInfo[1] == "") {
			continue;
		}
		
		std::string absPath = fInfo[1];
		INDEXED_FILES.fullPaths.push_back(absPath);
		INDEXED_FILES.displayPaths.push_back(icu::UnicodeString::fromUTF8(">" + fInfo[0]));
		INDEXED_FILES.indexedNames.push_back(fInfo[0]);
		dontshowagain.insert(absPath);
	}
	
	// 1) BFS through the tree, up to maxFiles files
	while (!dirs.empty() && seen < maxFiles) {
		auto curDir = dirs.front(); 
		dirs.pop();
		
		std::error_code dirEc;
		std::filesystem::directory_iterator iter(curDir, dirEc);
		if (dirEc) {
			// Could be “permission denied”; just skip it
			continue;
		}
		
		for (auto& entry : iter) {
			if (seen >= maxFiles) break;

			if (entry.is_directory()) {
				if (entry.path().filename().string()[0] != '.') {
					dirs.push(entry.path().string());
				}
			}else if (entry.is_regular_file()) {
				std::string absPath = entry.path().string();
				if (dontshowagain.count(absPath)) {
					continue;
				}
				
				INDEXED_FILES.fullPaths.push_back(absPath);

				// Compute a relative display path, cropped to last maxDisplayChars
				std::string rel = absPath.size() > rootLen
								  ? absPath.substr(rootLen)
								  : absPath;
				if (rel.size() > maxDisplayChars) {
					rel = rel.substr(rel.size() - maxDisplayChars);
					// try to crop before the first slash so you don’t cut mid-folder
					auto slash = rel.find_first_of("/\\");
					if (slash != std::string::npos)
						rel = rel.substr(slash);
				}
				INDEXED_FILES.displayPaths.push_back(icu::UnicodeString::fromUTF8(rel));

				// Store the bare file name
				INDEXED_FILES.indexedNames.push_back(entry.path().filename().string());

				++seen;
			}
		}
	}
	
	static const std::vector<std::string> commands = {
		"Git Push","Git Pull","Git Force Pull","Help","Save Theme Settings To File","Load Theme Settings From File","Restart Language Servers (LSPs)","Open `languages.json` file","Test Toast Box","Test Text Line"
	};

	for (auto const& cmd : commands) {
		std::string tagged = ":" + cmd;
		INDEXED_FILES.fullPaths.push_back(tagged);
		INDEXED_FILES.indexedNames.push_back(tagged);
		INDEXED_FILES.displayPaths.push_back(icu::UnicodeString::fromUTF8(tagged));
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
	storedsearches.clear();
	
	std::vector<icu::UnicodeString> els;
	
	icu::UnicodeString searchfor = cp->getFullText();
	
	if (searchfor.length() >= 3 && searchfor.char32At(0) == U'&') {
		std::string loweredSearchfor;
		searchfor.toUTF8String(loweredSearchfor);
		loweredSearchfor = loweredSearchfor.substr(1);
		SearchResult res = searchAcrossFiles(loweredSearchfor);
		
		for (const auto& [key, matches] : res) {
			const auto& [filePath, fileName] = key;
			
			std::filesystem::path p(filePath);
			
			els.push_back(icu::UnicodeString::fromUTF8(p.filename().string()));
			
			StoredSearch itm = {filePath, 0};
			storedsearches.push_back(itm);
			
			INDEXED_FILES.currentlyshowing.push_back(storedsearches.size()-1);
			INDEXED_FILES.currentlyshowingtype.push_back(2);
			
			for (const auto& [lineNum, text] : matches) {
				els.push_back(icu::UnicodeString::fromUTF8("    "+text));
				
				StoredSearch itm = {filePath, lineNum-1};
				storedsearches.push_back(itm);
				
				INDEXED_FILES.currentlyshowing.push_back(storedsearches.size()-1);
				INDEXED_FILES.currentlyshowingtype.push_back(2);
			}
		}
	}
	
	auto res = calcExpression(searchfor);
	if (res.first){
		INDEXED_FILES.currentlyshowing.push_back(0);
		INDEXED_FILES.currentlyshowingtype.push_back(1);
		els.push_back(doubleToUnicodeString_pretty(res.second));
	}
	
	std::string str;
	searchfor.toUTF8String(str);
	
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
			edt->setFullText(icu::UnicodeString());
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
	rootelement->save();
}

icu::UnicodeString App::readFileToUnicodeString(const std::string& filename, bool& worked) {
	worked = false;
	
	
	for (int i = 0; i < 5; i++) {
		// Read file as binary
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			return icu::UnicodeString::fromUTF8("Failed to open file - file.is_open");
		}
	
		std::streamsize fileSize = file.tellg();
		if (fileSize < 0) {
			file.close();
			return icu::UnicodeString::fromUTF8("Failed to open file - fileSize < 0");
		}
	
		file.seekg(0, std::ios::beg);
		std::vector<char> buffer(static_cast<size_t>(fileSize));
		if (fileSize > 0 && !file.read(buffer.data(), fileSize)) {
			file.close();
			return icu::UnicodeString::fromUTF8("Failed to open file - couldn't read data");
		}
		file.close();
		
		// Empty file on disk → try again
		if (buffer.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(25));
			continue;
		}
		
		UErrorCode status = U_ZERO_ERROR;
		
		// Charset detector
		UCharsetDetector* detector = ucsdet_open(&status);
		if (U_FAILURE(status) || !detector) {
			return icu::UnicodeString::fromUTF8("Failed to open file - couldn't create charset detector");
		}
		
		ucsdet_setText(detector, buffer.data(), static_cast<int32_t>(fileSize), &status);
		if (U_FAILURE(status)) {
			ucsdet_close(detector);
			return icu::UnicodeString::fromUTF8("Failed to open file - couldn't set input data for charset detector");
		}
		
		int32_t matchCount = 0;
		const UCharsetMatch** matches = ucsdet_detectAll(detector, &matchCount, &status);
		if (U_FAILURE(status) || matchCount == 0) {
			ucsdet_close(detector);
			return icu::UnicodeString::fromUTF8("Failed to open file - no matches on charset detector");
		}
		
		icu::UnicodeString result;
		bool conversionSucceeded = false;
		
		// Try detected encodings
		for (int32_t i = 0; i < matchCount && !conversionSucceeded; ++i) {
			status = U_ZERO_ERROR;
			const char* encodingName = ucsdet_getName(matches[i], &status);
			if (U_FAILURE(status) || !encodingName) continue;
			
			int32_t confidence = ucsdet_getConfidence(matches[i], &status);
			if (U_FAILURE(status) || confidence < 10) continue;
			
			UConverter* converter = ucnv_open(encodingName, &status);
			if (U_FAILURE(status) || !converter) continue;
			
			int32_t targetCapacity = static_cast<int32_t>(fileSize * 2 + 1);
			std::vector<UChar> targetBuffer(static_cast<size_t>(targetCapacity));
			
			int32_t targetLength = ucnv_toUChars(
				converter,
				targetBuffer.data(),
				targetCapacity,
				buffer.data(),
				static_cast<int32_t>(fileSize),
				&status
			);
	
			ucnv_close(converter);
	
			if (U_SUCCESS(status)) {
				result = icu::UnicodeString(targetBuffer.data(), targetLength);
				conversionSucceeded = true;
			}
		}
		
		ucsdet_close(detector);
		
		// Fallback encodings
		if (!conversionSucceeded) {
			const char* fallbackEncodings[] = {
				"UTF-8","UTF-16","UTF-16BE","UTF-16LE",
				"UTF-32","UTF-32BE","UTF-32LE",
				"ISO-8859-1","Windows-1252","ASCII"
			};
	
			for (const char* enc : fallbackEncodings) {
				status = U_ZERO_ERROR;
				UConverter* converter = ucnv_open(enc, &status);
				if (U_FAILURE(status) || !converter) continue;
	
				int32_t targetCapacity = static_cast<int32_t>(fileSize * 2 + 1);
				std::vector<UChar> targetBuffer(static_cast<size_t>(targetCapacity));
	
				int32_t targetLength = ucnv_toUChars(
					converter,
					targetBuffer.data(),
					targetCapacity,
					buffer.data(),
					static_cast<int32_t>(fileSize),
					&status
				);
	
				ucnv_close(converter);
	
				if (U_SUCCESS(status)) {
					result = icu::UnicodeString(targetBuffer.data(), targetLength);
					conversionSucceeded = true;
					break;
				}
			}
		}
	
		// Final fallback: assume UTF-8 and sanity-check replacement chars
		if (!conversionSucceeded) {
			status = U_ZERO_ERROR;
			result = icu::UnicodeString::fromUTF8(icu::StringPiece(buffer.data(), static_cast<int32_t>(fileSize)));
	
			int32_t replacementCount = 0;
			const int32_t totalLength = result.length();
			for (int32_t i = 0; i < totalLength; ++i) {
				if (result.charAt(i) == 0xFFFD) replacementCount++;
			}
			if (totalLength > 0 && (replacementCount * 100 / totalLength) < 5) {
				conversionSucceeded = true;
			}
		}
		
		// Normalize (strip CR) on success
		if (conversionSucceeded) {
			icu::UnicodeString normalized;
			for (int32_t i = 0; i < result.length(); ++i) {
				UChar ch = result.charAt(i);
				if (ch != 0x000D) normalized.append(ch);
			}
			result = normalized;
		}
		
	
		if (result.length() != 0 || !conversionSucceeded) {
			worked = conversionSucceeded;
			return result;
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
	
	worked = true;
	return icu::UnicodeString();
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
	
	
	
	// Build the full parameter string: "/k \"your command\""
	std::string params = "/k \"" + command + "\"";

	// Launch cmd.exe in a new console window
	HINSTANCE result = ShellExecuteA(
		/*hwnd=*/       NULL,
		/*operation=*/  "open",
		/*file=*/       "C:\\Windows\\System32\\cmd.exe",
		/*parameters=*/ params.c_str(),
		/*directory=*/  NULL,
		/*show cmd=*/   SW_SHOW
	);

	// Error check: return value > 32 indicates success
	if ((INT_PTR)result <= 32) {
		throw std::runtime_error(
			"Failed to launch cmd.exe (error code " +
			std::to_string((INT_PTR)result) + ")"
		);
	}
}

void searchTheseFiles(const std::string& st, std::vector<std::string> files, SearchResult* res) {
	unsigned char frstchr = st[0];
	bool works;
	unsigned char hc;
	int stlen = (int)st.size();
	
	for (size_t idx = 0; idx < files.size(); ++idx) {
		const auto& path = files[idx];
		if (isBinaryFile(path))
			continue;

		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in) continue;
		auto size = in.tellg();
		in.seekg(0);
		
		std::string buf;
		buf.resize(size);
		in.read(&buf[0], size);
		
		int buflen = (int)buf.size();
		
		SearchMatchVec matches;
		int lineNum = 1;
		int lineStart = 0;
		bool waitingforline = false;
		
		for (int i1 = 0; i1 < buflen-stlen+1; i1++) {
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
				
				matches.emplace_back(lineNum, trim(buf.substr(lineStart, endLine-lineStart)));
				waitingforline = true;
				works = false;
			}
		}
		
		if (!matches.empty()) {
			res->emplace(std::make_pair(path, files[idx]), std::move(matches));
		}
	}
}

SearchResult App::searchAcrossFiles(const std::string& searchTerm) {
	SearchResult out;
	
	auto st = toLower(searchTerm);
	
	// limit to first 300 (or fewer) files
	size_t maxlen = std::min<size_t>(300, INDEXED_FILES.fullPaths.size());
	
	std::vector<std::string>  torun;
	std::vector<std::thread>  threads;
	std::vector<SearchResult*> reses;
	
	for (size_t idx = 0; idx < maxlen; ++idx) { // we're going to separate this into different groups of files to search
		if (torun.size() == 30) {
			reses.push_back(new SearchResult());
			threads.emplace_back(searchTheseFiles, st, torun, reses.back());
			torun.clear();
		}
		torun.push_back(INDEXED_FILES.fullPaths[idx]);
	}
	if (torun.size() != 0) {
		reses.push_back(new SearchResult());
		threads.emplace_back(searchTheseFiles, st, torun, reses.back());
		torun.clear();
	}

	for (int i = 0; i < threads.size(); i++) {
		threads[i].join();
		for (const auto& pair : *reses[i]) {
			out[pair.first] = pair.second;
		}
		delete reses[i];
	}
	
	return out;
}

void App::setTintedColor(Color* tint_c, Color* c, float b) {
	if (tint_c->r == 1 && tint_c->g == 1 && tint_c->b == 1) {
		c->r = b;
		c->g = b;
		c->b = b;
		return;
	}
	
	float tcb = tint_c->r*0.299+tint_c->g*0.587+tint_c->b*0.114;
	
	if (tcb == 0) {
		tint_c->r = 0.1;
		tint_c->g = 0.1;
		tint_c->b = 0.1;
		
		tcb = 0.1;
	}else if (tcb < 0.1) {
		tint_c->r *= 0.1/tcb;
		tint_c->g *= 0.1/tcb;
		tint_c->b *= 0.1/tcb;
		
		tcb = 0.1;
	}
	
	float scale = (b/tcb+b*2)/3;
	
	float new_r = fmin(255.0, tint_c->r*scale);
	float new_g = fmin(255.0, tint_c->g*scale);
	float new_b = fmin(255.0, tint_c->b*scale);
	
	c->r = new_r;
	c->g = new_g;
	c->b = new_b;
}

void App::updateFromTintColor(Theme* t) {
	if (darkmode) {
		setTintedColor(t->tint_color, t->main_background_color,    0.098039);
		setTintedColor(t->tint_color, t->extras_background_color,  0.164706);
		setTintedColor(t->tint_color, t->hover_background_color,   0.26);
		setTintedColor(t->tint_color, t->main_text_color,          1.0);
		setTintedColor(t->tint_color, t->border,                   0.35);
		setTintedColor(t->tint_color, t->syntax_colors[0],         1.0);
		setTintedColor(t->tint_color, t->darker_background_color,  0.05);
		setTintedColor(t->tint_color, t->overlay_background_color, 0.12);
		setTintedColor(t->tint_color, t->lesser_text_color,        0.392157);
	}else{
		setTintedColor(t->tint_color, t->main_background_color,    0.9);
		setTintedColor(t->tint_color, t->extras_background_color,  0.8);
		setTintedColor(t->tint_color, t->hover_background_color,   0.7);
		setTintedColor(t->tint_color, t->main_text_color,          0.0);
		setTintedColor(t->tint_color, t->border,                   0.6);
		setTintedColor(t->tint_color, t->syntax_colors[0],         0.0);
		setTintedColor(t->tint_color, t->darker_background_color,  0.95);
		setTintedColor(t->tint_color, t->overlay_background_color, 0.8);
		setTintedColor(t->tint_color, t->lesser_text_color,        0.4);
	}
}

void App::displayToast(icu::UnicodeString text) {
	if (auto toaster = dynamic_cast<Toast*>(toastBox)) {
		toaster->displayMessage(text);
	}
	time_till_regular = 2;
}

void App::displayText(icu::UnicodeString text) {
	if (auto sn = dynamic_cast<ScrollNotify*>(scrollNotifyBox)) {
		sn->displayMessage(text);
	}
	time_till_regular = 2;
}