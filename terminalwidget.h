#pragma once

#include <GLFW/glfw3.h>
#include "terminal.h"
#include "widget.h"
#include "checkbox.h"
#include <sio_client.h>
#include "contextmenu.h"

class TerminalWidget : public Widget {
public:
	Terminal* term = nullptr;
	
	TerminalWidget(Widget* parent);
	
	Widget* findTerminal();
	
	ContextMenu* contextmenu;
	
	bool settingup = true;
	
	int prev_w_cells = 0;
	int prev_h_cells = 0;
	
	void position(int x, int y, int w, int h);
	void render();
	
	void runCommand(std::string command);
	
	virtual bool on_key_event(int key, int scancode, int action, int mods);
	virtual bool on_char_event(unsigned int keycode);
	virtual bool on_mouse_button_event(int button, int action, int mods);
	virtual bool on_mouse_move_event();
	virtual bool on_scroll_event(double xchange, double ychange);
	
	// New:
	bool selecting = false;
	int sel_doc_r0 = -1, sel_c0 = -1;  // anchor (document line id, column)
	int sel_doc_r1 = -1, sel_c1 = -1;  // cursor (document line id, column)
	static inline void normalize_sel(int& r0,int& c0,int& r1,int& c1) {
		if (r0 > r1 || (r0 == r1 && c0 > c1)) { std::swap(r0,r1); std::swap(c0,c1); }
	}
	bool cell_in_selection(int screen_r, int c) const;
	std::string selection_text() const;
	void clear_selection();
	
	bool rounded = true;
	
	void run();
	
	void executeAction(WidgetActionType typ);
	
	std::string get_last_n_doc_lines(int n);
private:
	std::recursive_mutex m_term_mutex;
	void cell_from_cursor(int& row, int& col);
	void apply_pending_resize();
	
	std::shared_ptr<sio::client> ajm_asv3_client;
	CheckBox* ajm_asv3_tm = nullptr;
	
	void ajm_set_asv3(bool connect);
	void reset_client();
	void schedule_reconnect(std::chrono::milliseconds delay);
	
	std::atomic<bool> reconnect_scheduled{false};
	std::atomic<uint64_t> client_generation{0};
	
	std::string ASSISTANT_V3_ID = "";
	
	int old_tx = -1;
	int old_ty = -1;
	int old_tw = -1;
	int old_th = -1;
	int needsRerender = 2;

	// Window dragging can produce dozens of column changes in a few
	// milliseconds. Apply only the final geometry after a short quiet period so
	// ConPTY and libghostty do not repeatedly reflow the same scrollback.
	int pending_w_cells = 0;
	int pending_h_cells = 0;
	bool resize_pending = false;
	double resize_due_at = 0.0;
};
