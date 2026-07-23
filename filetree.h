#pragma once

#include "helper_types.h"
#include "widget.h"
#include <atomic>
#include <mutex>
#include <thread>

struct TreeStructure {
	MST::MonoString name;
	std::string path;
	bool is_folder = false;
	
	std::vector<TreeStructure*> childrenFolders = {};
	std::vector<TreeStructure*> childrenFiles = {};
};

struct Visual {
	int x;
	int y;
	int w;
	int h;
	
	MST::MonoString name;
	TreeStructure* ts = nullptr;
	
	bool is_folder = false;
};

class FileTree : public Widget {
public:
	FileTree(Widget* parent);
	~FileTree() override;
	
	std::mutex tree_mutex;
	
	TreeStructure* root = nullptr;
	std::vector<Visual> toRender = {};
	
	std::vector<std::string> openpaths = {};
	
	double scrolled_to_vert = 0; // measured in lines
	double scrolled_to_horz = 0;
	double max_scroll_vert = 0;
	double max_scroll_horz = 0;
	
	bool rounded = true;
	
	Color* back_color;
	
	int elHeighto = 0;
	
	GLuint folderIcon;
	GLuint fileIcon;
	
	
//	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h);
	void render();
	void save();
	
	void fillOutTree(TreeStructure* el);
	void deleteTree(TreeStructure* el);
	void refreshAsync();
	
	double createVisuals(double pos, double depth, TreeStructure* el);
private:
	void fillOutTreeImpl(TreeStructure* el, const std::vector<std::string>& expanded, const std::shared_ptr<FileBackend>& backend);
	std::atomic<bool> refresh_in_progress{false};
	std::thread refresh_thread;
};
