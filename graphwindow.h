#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"
#include "helper_types.h"
#include "textedit.h"

struct OldRenderState {
	int t_x = -1;
	int t_y = -1;
	int t_w = -1;
	int t_h = -1;
	int b_h = -1;
	int b_y = -1;
};

struct Drawable {
	double x; // in scale
	double y; // in scale
	Color* c;
	
	virtual ~Drawable() = default;
};

struct DrawCircle : Drawable {}; // don't actually need more data. Just want it anyways.

struct DrawLine : Drawable {
	double endX;
	double endY;
};

struct DataType {
	bool scatter = false;
	std::vector<double> x = {};
	std::vector<double> y = {};
	Button* scatterButton = nullptr;
	Button* removeButton = nullptr;
	int id;
	
	std::vector<double> modified_x = {};
	std::vector<double> modified_y = {};
	
	Color* c;
	
	virtual ~DataType() = default;
	
	bool file = false;
	
	// for files
	icu::UnicodeString filename;
	std::string filepath;
	
	// for non files
	TextEdit* x_edit = nullptr;
	TextEdit* y_edit = nullptr;
};

class GraphWindow : public Widget {
public:
	GraphWindow(Widget* parent);
	
	void render() override;
	
	bool on_mouse_button_event(int button, int action, int mods) override;
	bool on_mouse_move_event() override;
	bool on_scroll_event(double xchange, double ychange) override;
	bool on_key_event(int key, int scancode, int action, int mods) override;
	void position(int x, int y, int w, int h) override;
	
	void executeAction(WidgetActionType typ) override;
private:
	std::vector<Color*> colorsList;
	std::vector<std::unique_ptr<Drawable>> drawables = {};
	std::vector<DataType> allData = {};
	
	OldRenderState OLDSTATE = {};
	
	bool rerender = true;
	
	void setColors();
	void addThing(bool isfile, std::string filepath="");
	void updateInfoFor(int id);
	void recalculateDrawables(bool changeMinsMax=true);
	void recalculateDisplayedValues();
	std::vector<double> getVals(icu::UnicodeString text, bool& allgood);
	
	std::vector<std::filesystem::path> get_files_in_directory(const std::filesystem::path& dir_path);
	
	std::pair<int,int> fromScaledToPixels(double x, double y);
	std::pair<double,double> fromPixelsToScaled(int x, int y);
	
	double xmin = 0;
	double ymin = 0;
	double xmax = 10;
	double ymax = 10;
	
	Button* reset;
	Button* addCords;
	Button* addFile;
	Button* addFolder;
	TextEdit* averageText;
	
	int screenHeight = 0;
	int screenStart = 0;
	
	int id_glob = 0;
	int EXISTING_VAL = 0;
	
	int TOTAL_ITEM_HEIGHT = 0;
	
	double scroll = 0;
	
	bool selectingSquare = false;
	bool canSelect = false;
	int squareStartX = 0;
	int squareStartY = 0;
};