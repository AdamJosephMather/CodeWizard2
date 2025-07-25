#include "application.h"
#include <GLFW/glfw3.h>
#include <queue>
#include <unicode/unistr.h>
#include <unicode/ustream.h>
#include <iostream>
#include "codeedit.h"
#include "editor.h"
#include "helper_types.h"
#include <vector>
#include "panel_holder.h"
#include "text_renderer.h"
#include <cmath>
#include "tinyfiledialogs.h"
#include "titlebar.h"
#include "languageserverclient.h"
#include "textedit.h"

#include <windows.h>
#include <windowsx.h>  // This header contains GET_X_LPARAM and GET_Y_LPARAM
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <fstream>
#include <unicode/ucsdet.h>   // CharsetDetector
#include <unicode/ucnv.h>     // UConverter, ucnv_open/close

#include <stb_image.h>

bool App::REQUESTING_STRING = false;
App::StringGivenFunc App::ON_STRING_GIVEN = nullptr;
TextEdit* STRING_REQUEST_TEXTEDIT = nullptr;

SettingsManager* App::settings = new SettingsManager();
std::mutex App::canMakeChanges;
FileIndexResult App::INDEXED_FILES = FileIndexResult();
std::vector<StoredSearch> App::storedsearches = {};

Widget* App::commandPalette = nullptr;
Widget* App::commandBox = nullptr;

int App::WINDOW_WIDTH = 1200;
int App::WINDOW_HEIGHT = 800;
std::string App::WINDOW_TITLE = "CodeWizard";

int App::mouseX = 0;
int App::mouseY = 0;
double M_PI = 3.14159265358979323846;

bool (*App::on_key_event)(int key, int scancode, int action, int mods) = nullptr;
bool (*App::on_char_event)(unsigned int codepoint) = nullptr;
bool (*App::on_mouse_button_event)(int button, int action, int mods) = nullptr;
bool (*App::on_mouse_move_event)() = nullptr;
bool (*App::on_scroll_event)(double xchange, double ychange) = nullptr;
bool (*App::on_resize_event)(int width, int height) = nullptr;

GLFWwindow* App::window = nullptr;
Widget* App::rootelement = nullptr;

int App::text_padding = 5;

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
int App::time_till_regular = 5;
bool App::forceWaitTime = false;
double App::lastUpdate = 0;

std::unordered_map<std::string, Language> App::languagemap = {};
std::unordered_map<std::string, LanguageServerClient*> App::lsp_client_map = {};

bool App::Init() {
	STRING_REQUEST_TEXTEDIT = new TextEdit(nullptr, [&](Widget* w){
		w->t_x = w->t_x+w->t_w/2-(w->t_w/4);
		w->t_w /= 2;
		int new_h = TextRenderer::get_text_height()+text_padding*2;
		w->t_y = w->t_y+w->t_h/2-(new_h/2);
		w->t_h = new_h;
	});
	
	settings->loadSettings();
	WINDOW_WIDTH = settings->getValue("window_width", 1200);
	WINDOW_HEIGHT = settings->getValue("window_height", 800);
	
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return false;
	}
	
	glfwWindowHint(GLFW_SAMPLES, 4); // request 4 samples
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
	
	window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE.c_str(), nullptr, nullptr);

	if (!window) {
		std::cerr << "Failed to create window\n";
		glfwTerminate();
		return false;
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

	int x = settings->getValue("window_x", screenWidth / 2 - WINDOW_WIDTH / 2);
	int y = settings->getValue("window_y", screenHeight / 2 - WINDOW_HEIGHT / 2);
	
	glfwSetWindowPos(window, x, y);
		
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
	
	// Now grab the HWND and force a resize border
	HWND hwnd = glfwGetWin32Window(window);

	DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

	// Chain your custom proc
	originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)CustomWndProc);

	// Grab the existing style
	LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);

	// Re-add the bits Windows needs for snapping + Win+Arrows:
	style |= ( WS_CAPTION 
			 | WS_THICKFRAME    // sizing border
			 | WS_MINIMIZEBOX 
			 | WS_MAXIMIZEBOX 
			 | WS_SYSMENU );    // optional, for system menu

	SetWindowLongPtr(hwnd, GWL_STYLE, style);

	// Force Windows to re-evaluate the non-client area:
	SetWindowPos(hwnd, nullptr, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
	
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
	
	return true;
}

LanguageServerClient* App::getLSP(std::string lsp_command) {
	std::cout << "getLSP - " << lsp_command << "\n";
	
	if (lsp_client_map.find(lsp_command) != lsp_client_map.end()) {
		return lsp_client_map[lsp_command];
	}
	lsp_client_map[lsp_command] = new LanguageServerClient(lsp_command, [](const std::string& msg) {
		std::cout << "LOG_LSP: " << msg << std::endl;
	});
	
	std::string folder = settings->getValue("current_folder", getExecutableDir());
	std::cout << "Loading lsp with folder: " << folder << std::endl;
	lsp_client_map[lsp_command]->initialize(folder);
	
	return lsp_client_map[lsp_command];
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

		case WM_SETCURSOR:
		{
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
		}
		
		case WM_NCCALCSIZE: {
			if (!wParam) break;
			
			// Check if window is maximized
			if (IsZoomed(hwnd)) {
				NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
				
				// Get monitor info for the monitor containing this window
				HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFO mi = { sizeof(MONITORINFO) };
				GetMonitorInfo(hMonitor, &mi);
				
				// Adjust the window rectangle to fit within the work area
				// This prevents the window from extending beyond screen boundaries
				params->rgrc[0] = mi.rcWork;
				
				return 0;
			}
			
			// For non-maximized windows, return 0 to remove default frame
			return 0;
		}
			
		case WM_GETMINMAXINFO:
		{
			auto mmi = reinterpret_cast<LPMINMAXINFO>(lParam);
		
			// get the monitor the window is mostly on
			HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		
			MONITORINFO mi{ sizeof(mi) };
			GetMonitorInfo(hMon, &mi);
		
			// your minimum tracking size
			mmi->ptMinTrackSize.x = 300;
			mmi->ptMinTrackSize.y = 200;
		
			// compute the maximized size and position *relative* to this monitor
			RECT&  work    = mi.rcWork;
			RECT&  monitor = mi.rcMonitor;
		
			mmi->ptMaxSize.x     = work.right  - work.left;
			mmi->ptMaxSize.y     = work.bottom - work.top;
		
			mmi->ptMaxPosition.x = work.left   - monitor.left;
			mmi->ptMaxPosition.y = work.top    - monitor.top;
		
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

void App::DrawRoundedRect(float x, float y, float w, float h, float radius, Color* color, int segments) {
	if (radius <= 0.0f) {
		// Fallback to plain rectangle
		DrawRect((int)x, (int)y, (int)w, (int)h, color);
		return;
	}

	glColor4f(color->r, color->g, color->b, color->a);
	
	// 1) Draw the center and side/quadrant straight regions as quads
	glBegin(GL_QUADS);
	  // Center
	  glVertex2f(x + radius,     y);
	  glVertex2f(x + w - radius, y);
	  glVertex2f(x + w - radius, y + h);
	  glVertex2f(x + radius,     y + h);

	  // Left strip
	  glVertex2f(x,               y + radius);
	  glVertex2f(x + radius,      y + radius);
	  glVertex2f(x + radius,      y + h - radius);
	  glVertex2f(x,               y + h - radius);

	  // Right strip
	  glVertex2f(x + w - radius,  y + radius);
	  glVertex2f(x + w,           y + radius);
	  glVertex2f(x + w,           y + h - radius);
	  glVertex2f(x + w - radius,  y + h - radius);
	glEnd();

	// 2) Draw the four quartercircles
	// bottomleft corner: from 180 to 270
	drawCorner(x + radius, y + radius, M_PI, 1.5f * M_PI, segments, radius);

	// bottomright corner: from 270 to 360 (or 90 to 0)
	drawCorner(x + w - radius, y + radius, 1.5f * M_PI, 2.0f * M_PI, segments, radius);

	// topright corner: from   0 to  90
	drawCorner(x + w - radius, y + h - radius, 0.0f, 0.5f * M_PI, segments, radius);

	// top left corner: from  90 to 180
	drawCorner(x + radius,     y + h - radius, 0.5f * M_PI, M_PI, segments, radius);
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

void App::DoFullRenderWithoutInput() {
	//framerate
	
	double currentTime = glfwGetTime();
	frameCount++;
	
	if (currentTime - lastTime >= 5.0) {
		std::cout << "FPS: " << (double)frameCount/(currentTime-lastTime) << std::endl;
		frameCount = 0;
		lastTime = currentTime;
	}
	
	
	std::lock_guard<std::mutex> lock(canMakeChanges); // this prevents separate threads (the lsp clients) from messing with shit while positioning/rendering
	if (rootelement) {
		rootelement->position(0, tb->t_h, WINDOW_WIDTH, WINDOW_HEIGHT-tb->t_h);
	}
	
	//render
	
	if (!rerender && time_till_regular == 0) {
		if (forceWaitTime) { // we don't want to hold up operations happening over multiple frames (think highlighting out of view lines that we don't need to re-render for)
			int time = 10;
			
			if (currentTime-lastUpdate > 5) { 
				time = 70; // go to 'sleep' if we haven't updated anything in the last x seconds this is roughly 15 fps
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
	
	glClearColor(bgcolor->r, bgcolor->g, bgcolor->b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	
	glEnable(GL_SCISSOR_TEST);
	
	SKIZ_X = 0;
	SKIZ_Y = 0;
	SKIZ_W = WINDOW_WIDTH;
	SKIZ_H = WINDOW_HEIGHT;
	
	glScissor(SKIZ_X, SKIZ_Y, SKIZ_W, SKIZ_H);
	
	if (rootelement) {
		rootelement->render();
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
	STRING_REQUEST_TEXTEDIT->background_color = theme.overlay_background_color;
	commandUnfocused(); // sets the command bar text to start with
	
	// Main loop
	while (!glfwWindowShouldClose(window)) {
		// Handle input/events
		rerender = false;
		forceWaitTime = true;
		glfwPollEvents();
		DoFullRenderWithoutInput();
	}
	
	save(); // save before exit.
	
	if (auto pe = dynamic_cast<PanelHolder*>(rootelement->children[0])) {
		settings->saveConfig(pe->saveConfiguration());
	}

	bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
	settings->setValue("window_maximized", maximized);

	if (!maximized) {
		int x, y, w, h;
		glfwGetWindowPos(window, &x, &y);
		glfwGetWindowSize(window, &w, &h);
		settings->setValue("window_x", x);
		settings->setValue("window_y", y);
		settings->setValue("window_width", w);
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
	
	if (rootelement) {
		rootelement->on_mouse_move_event();
	}
}

void App::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
	rerender = true;
	
	// action - GLFW_PRESS/GLFW_RELEASE/GLFW_REPEAT
	// button - GLFW_MOUSE_BUTTON(LEFT/RIGHT)
	
	if (on_mouse_button_event) {
		if (on_mouse_button_event(button, action, mods)) { return; };
	}
	
	int mx = mouseX;
	int my = mouseY;
	
	if (commandBox->parent != nullptr) {
		if (mx >= commandBox->t_x && mx <= commandBox->t_x+commandBox->t_w && my >= commandBox->t_y && my <= commandBox->t_y+commandBox->t_h) {
			commandBox->on_mouse_button_event(button, action, mods);
			return;
		}
	}
	if (REQUESTING_STRING) {
		if (mx >= STRING_REQUEST_TEXTEDIT->t_x && mx <= STRING_REQUEST_TEXTEDIT->t_x+STRING_REQUEST_TEXTEDIT->t_w && my >= STRING_REQUEST_TEXTEDIT->t_y && my <= STRING_REQUEST_TEXTEDIT->t_y+STRING_REQUEST_TEXTEDIT->t_h) {
			STRING_REQUEST_TEXTEDIT->on_mouse_button_event(button, action, mods);
			return;
		}else{
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			setActiveLeafNode(nullptr);
		}
	}
	
	if (rootelement) { rootelement->on_mouse_button_event(button, action, mods); }
}

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	rerender = true;
	
	bool control = ((mods & GLFW_MOD_CONTROL) != 0);
	bool shift = ((mods & GLFW_MOD_SHIFT) != 0);
	
	if (on_key_event) {
		if (on_key_event(key, scancode, action, mods)) { return; };
	}
	
	if (REQUESTING_STRING) {
		if (key == GLFW_KEY_ENTER && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
			setActiveLeafNode(nullptr);
			if (ON_STRING_GIVEN) {
				ON_STRING_GIVEN(STRING_REQUEST_TEXTEDIT->getFullText());
			}
			return;
		}if (key == GLFW_KEY_ESCAPE && (action == GLFW_PRESS || action == GLFW_REPEAT) && (STRING_REQUEST_TEXTEDIT->mode == 'n' || !settings->getValue("use_vim", false))) {
			REQUESTING_STRING = false;
			RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
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
			std::string fp = fpr;
			
			std::string oldfolder = settings->getValue("current_folder", getExecutableDir());
			for (auto lsp : lsp_client_map){
				if (lsp.second) {
					lsp.second->changeFolder(oldfolder, fp);
				}
			}
			
			settings->setValue("current_folder", fp);
		}
		commandUnfocused();
		return;
	}else if (action == GLFW_PRESS && key == GLFW_KEY_S && control && !shift) {
		save();
	}else if (action == GLFW_PRESS && key == GLFW_KEY_F5) {
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
		setActiveLeafNode(commandPalette);
		auto cp = dynamic_cast<TextEdit*>(commandPalette);
		cp->setFullText("&");
		cp->cursors = { {0, 1, 0, 1, 1} };
		cp->mode = 'i';
		return;
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

void App::character_callback(GLFWwindow* window, unsigned int codepoint) {
	rerender = true;
	
	if (on_char_event) {
		on_char_event(codepoint);
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
	
	if (commandBox->parent != nullptr) {
		if (mx >= commandBox->t_x && mx <= commandBox->t_x+commandBox->t_w && my >= commandBox->t_y && my <= commandBox->t_y+commandBox->t_h) {
			commandBox->on_scroll_event(-xpos, -ypos);
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
	forceWaitTime = true;
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
		App::RemoveWidgetFromParent(commandBox);
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
	
	std::cout << "Selected: " << cur_sel << " Cur type: " << cur_type << std::endl;
	
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
	
	if (filepath.at(0) == ':') { // it's a command
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
			MoveWidget(STRING_REQUEST_TEXTEDIT, rootelement);
			setActiveLeafNode(STRING_REQUEST_TEXTEDIT);
		}else if (filepath == ":Git Pull") {
			std::string folder = settings->getValue("current_folder", getExecutableDir());
			launchCommandNonBlocking("cd /d "+folder+" && git pull");
		}else if (filepath == ":Git Force Pull") {
			std::string folder = settings->getValue("current_folder", getExecutableDir());
			launchCommandNonBlocking("cd /d "+folder+" && git reset --hard && git pull");
		}
		
		return;
	}
	
	openFromCMD(filepath, INDEXED_FILES.indexedNames[cur_sel]);
}

void App::openFromCMD(std::string filepath, std::string filename, int line) {
	std::cout << "Opening from cmd..." << std::endl;
	
	FileInfo* finfo = new FileInfo();
	finfo->filepath = filepath;
	finfo->filename = filename;
	finfo->ondisk = true;
	
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
		"Git Push","Git Pull","Git Force Pull"
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
			
			StoredSearch itm = {p.filename().string(), 0};
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
		els.push_back(doubleToUnicodeString(res.second));
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
		RemoveWidgetFromParent(STRING_REQUEST_TEXTEDIT);
	}
	
	if (w == commandPalette && activeLeafNode != commandPalette) {
		beforeCommandLeafNode = activeLeafNode;
	}
	
	rerender = true;
	activeLeafNode = w;
	activeEditor = nullptr;
	
	if (!w) {
		return;
	}
	
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
	
	if (w != commandPalette) {
		commandUnfocused();
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

void App::runWithSKIZ(int nx, int ny, int nw, int nh, VoidFunction withskiz) {
	int was_s_x = App::SKIZ_X;
	int was_s_y = App::SKIZ_Y;
	int was_s_w = App::SKIZ_W;
	int was_s_h = App::SKIZ_H;
	
	App::SKIZ_X = fmax(SKIZ_X, nx); // this could be broken, if we don't draw from left to right. because it's not actually the interception of the boxes
	App::SKIZ_Y = fmax(SKIZ_Y, ny);
	App::SKIZ_W = fmin(SKIZ_W, nw);
	App::SKIZ_H = fmin(SKIZ_H, nh);
	
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

#include <unicode/ucsdet.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>
#include <unicode/unistr.h>
#include <fstream>
#include <vector>
#include <iostream>

icu::UnicodeString App::readFileToUnicodeString(const std::string& filename, bool& worked) {
	worked = false;
	// First, read the entire file as binary data
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		return icu::UnicodeString::fromUTF8("Failed to open file - file.is_open");
	}	
	
	// Get file size and read all data
	std::streamsize fileSize = file.tellg();
	if (fileSize < 0) {
		file.close();
		return icu::UnicodeString::fromUTF8("Failed to open file - fileSize < 0");;
	}
	
	file.seekg(0, std::ios::beg);
	std::vector<char> buffer(fileSize);
	if (!file.read(buffer.data(), fileSize)) {
		file.close();
		return icu::UnicodeString::fromUTF8("Failed to open file - couldn't read data");;
	}
	file.close();

	if (buffer.empty()) {
		worked = true;
		return icu::UnicodeString();
	}
	
	UErrorCode status = U_ZERO_ERROR;
	
	// Create charset detector
	UCharsetDetector* detector = ucsdet_open(&status);
	if (U_FAILURE(status)) {
		icu::UnicodeString::fromUTF8("Failed to open file - couldn't create charset detector");
	}
	
	// Set the input data for detection
	ucsdet_setText(detector, buffer.data(), static_cast<int32_t>(fileSize), &status);
	if (U_FAILURE(status)) {
		ucsdet_close(detector);
		icu::UnicodeString::fromUTF8("Failed to open file - couldn't set input data for charset detector");
	}
	
	// Detect all possible character sets
	int32_t matchCount = 0;
	const UCharsetMatch** matches = ucsdet_detectAll(detector, &matchCount, &status);
	if (U_FAILURE(status) || matchCount == 0) {
		ucsdet_close(detector);
		icu::UnicodeString::fromUTF8("Failed to open file - no matches on charset detector");
	}
	
	icu::UnicodeString result;
	bool conversionSucceeded = false;
	
	// Try each detected encoding in order of confidence
	for (int32_t i = 0; i < matchCount && !conversionSucceeded; ++i) {
		const char* encodingName = ucsdet_getName(matches[i], &status);
		if (U_FAILURE(status)) {
			continue;
		}
		
		int32_t confidence = ucsdet_getConfidence(matches[i], &status);
		if (U_FAILURE(status)) {
			continue;
		}
		
		// Skip very low confidence matches (below 10%)
		if (confidence < 10) {
			continue;
		}
		
		// Try to convert using this encoding
		status = U_ZERO_ERROR;
		UConverter* converter = ucnv_open(encodingName, &status);
		if (U_FAILURE(status)) {
			continue;
		}
		
		// Calculate target buffer size (worst case: each byte becomes a surrogate pair)
		int32_t targetCapacity = static_cast<int32_t>(fileSize * 2 + 1);
		std::vector<UChar> targetBuffer(targetCapacity);
		
		// Convert from detected encoding to UTF-16
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
	
	// If automatic detection failed, try some common encodings manually
	if (!conversionSucceeded) {
		const char* fallbackEncodings[] = {
			"UTF-8",
			"UTF-16",
			"UTF-16BE",
			"UTF-16LE",
			"UTF-32",
			"UTF-32BE", 
			"UTF-32LE",
			"ISO-8859-1",
			"Windows-1252",
			"ASCII"
		};
		
		for (const char* encoding : fallbackEncodings) {
			status = U_ZERO_ERROR;
			UConverter* converter = ucnv_open(encoding, &status);
			if (U_FAILURE(status)) {
				continue;
			}
			
			int32_t targetCapacity = static_cast<int32_t>(fileSize * 2 + 1);
			std::vector<UChar> targetBuffer(targetCapacity);
			
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
	
	// Final fallback: try to create UnicodeString assuming UTF-8
	if (!conversionSucceeded) {
		status = U_ZERO_ERROR;
		result = icu::UnicodeString::fromUTF8(icu::StringPiece(buffer.data(), fileSize));
		// Check if the conversion was successful by looking for replacement characters
		// This is a heuristic - if we have too many replacement characters, it probably failed
		int32_t replacementCount = 0;
		int32_t totalLength = result.length();
		for (int32_t i = 0; i < totalLength; ++i) {
			if (result.charAt(i) == 0xFFFD) { // Unicode replacement character
				replacementCount++;
			}
		}
		
		// If more than 5% of characters are replacement characters, consider it failed
		if (totalLength > 0 && (replacementCount * 100 / totalLength) < 5) {
			conversionSucceeded = true;
		}
	}
	
	if (conversionSucceeded) {
		icu::UnicodeString normalized;
		int32_t length = result.length();
		
		for (int32_t i = 0; i < length; ++i) {
			UChar ch = result.charAt(i);
			if (ch != 0x000D) { // Skip CR characters
				normalized.append(ch);
			}
		}
		result = normalized;
	}
	
	worked = conversionSucceeded;
	return result;
}

void App::launchCommandNonBlocking(const std::string& command) {
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
	for (size_t idx = 0; idx < files.size(); ++idx) {
		const auto& path = files[idx];
		if (isBinaryFile(path)) 
			continue;

		std::ifstream in(path);
		if (!in.is_open()) 
			continue;

		SearchMatchVec matches;
		std::string line;
		int lineNum = 1;

		while (std::getline(in, line)) {
			if (caseInsensitiveFindAlreadyLowered(line, st)) {
				matches.emplace_back(lineNum, trim(line));
			}
			++lineNum;
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
	float scale = (b/tcb+b*2)/3;
	
	float new_r = fmin(255.0, tint_c->r*scale);
	float new_g = fmin(255.0, tint_c->g*scale);
	float new_b = fmin(255.0, tint_c->b*scale);
	
	c->r = new_r;
	c->g = new_g;
	c->b = new_b;
}

void App::updateFromTintColor(Theme* t) {
	setTintedColor(t->tint_color, t->main_background_color,    0.098039);
	setTintedColor(t->tint_color, t->extras_background_color,  0.164706);
	setTintedColor(t->tint_color, t->hover_background_color,   0.23);
	setTintedColor(t->tint_color, t->main_text_color,          1.0);
	setTintedColor(t->tint_color, t->syntax_colors[0],         1.0);
	setTintedColor(t->tint_color, t->darker_background_color,  0.05);
	setTintedColor(t->tint_color, t->overlay_background_color, 0.12);
	setTintedColor(t->tint_color, t->lesser_text_color,        0.392157);
	
	t->border = MakeColor(0, 0, 0);
}