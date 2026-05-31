#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"
#include "helper_types.h"
#include "textedit.h"

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
	std::vector<double> x;
	std::vector<double> y;
	Button* scatterButton;
	Button* removeButton;
	
	virtual ~DataType() = default;
};

struct PastedData : DataType {
	TextEdit* x_edit;
	TextEdit* y_edit;
};

struct FileData : DataType {
	icu::UnicodeString filename;
	std::string filepath;
	Button* refresh;
};

class GraphWindow : public Widget {
public:
	GraphWindow(Widget* parent);
	
	void render() override;
	
	bool on_mouse_button_event(int button, int action, int mods) override;
	bool on_mouse_move_event() override;
	void position(int x, int y, int w, int h) override;
	
	void executeAction(WidgetActionType typ) override;
private:
	std::vector<Color*> colorsList;
	std::vector<std::unique_ptr<Drawable>> drawables = {};
	std::vector<std::unique_ptr<DataType>> allData = {};
	
	void setColors();
	void recalculateDrawables();
	
	std::pair<int,int> fromScaledToPixels(double x, double y);
	std::pair<double,double> fromPixelsToScaled(int x, int y);
	
	double xmin = 0;
	double ymin = 0;
	double xmax = 10;
	double ymax = 10;
	
	Button* reset;
	
	int screenHeight = 0;
	int screenStart = 0;
	
	double scroll = 0;
	
	bool selectingSquare = false;
	int squareStartX = 0;
	int squareStartY = 0;
};
