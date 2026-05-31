#include "graphwindow.h"
#include "application.h"
#include "text_renderer.h"

GraphWindow::GraphWindow(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("GraphWindow");
	
	colorsList = {};
	for (int i = 0; i < 10; i++) {
		colorsList.push_back(MakeColor(0, 0, 0));
	}
	setColors();
	
	before_self_close = [&](){
		for (size_t i = 0; i < colorsList.size(); ++i) {
			delete colorsList[i]; 
		}
		colorsList.clear();
	};
	
	reset = new Button(this, icu::UnicodeString::fromUTF8("Reset View"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+App::text_padding;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
		recalculateDrawables();
	});
	reset->border = true;
	reset->rounded = true;
	
//	mathInput = new TextEdit(this, [&](Widget*){
//		mathInput->t_x = t_x;
//		mathInput->t_y = t_y;
//		mathInput->t_w = (t_w*2)/3-1;
//		mathInput->t_h = t_h;
//	});
//	mathInput->rounded = true;
//	mathInput->border = true;
//	mathInput->scrollbar_horizontal = true;
//	mathInput->scrollbar_vertical = true;
//	mathInput->contextmenu->is_visible_3 = true;
	
	
	auto line = std::make_unique<PastedData>();
	for (int x = -2; x <= 12; x++) {
		line->x.push_back(x);
		line->y.push_back((x-3)*(x-2)*(x-9) + 3);
	}
	line->scatter = false;
	allData.push_back(std::move(line));
	
	auto line2 = std::make_unique<PastedData>();
	for (int x = -2; x <= 12; x++) {
		line2->x.push_back(x);
		line2->y.push_back(x*x);
	}
	line2->scatter = true;
	allData.push_back(std::move(line2));
	
	recalculateDrawables();
}

void GraphWindow::position(int x, int y, int w, int h) {
	screenHeight = h*.6;
	screenStart = reset->t_y+reset->t_h+App::text_padding;
	
	Widget::position(x, y, w, h);
}

void GraphWindow::recalculateDrawables() {
	drawables.clear();
	
	bool setFirst = true;
	
	for (size_t i = 0; i < allData.size(); i++) {
		if (allData[i]->x.size() != allData[i]->y.size()) {
			App::displayToast(icu::UnicodeString::fromUTF8("Ommitting data, non-matching x/y count"));
			continue;
		}
		
		for (size_t j = 0; j < allData[i]->x.size(); j++) {
			double x = allData[i]->x[j];
			double y = allData[i]->y[j];
			
			if (setFirst) {
				xmin = x;
				xmax = x;
				ymin = y;
				ymax = y;
				setFirst = false;
			}
			if (xmin > x) {
				xmin = x;
			}
			if (xmax < x) {
				xmax = x;
			}
			if (ymin > y) {
				ymin = y;
			}
			if (ymax < y) {
				ymax = y;
			}
			
			if (allData[i]->scatter) {
				auto circle = std::make_unique<DrawCircle>();
				circle->x = x;
				circle->y = y;
				circle->c = colorsList[i%10];
				drawables.push_back(std::move(circle));
			}else if (j != 0){
				auto line = std::make_unique<DrawLine>();
				line->x = allData[i]->x[j-1];
				line->y = allData[i]->y[j-1];
				line->endX = x;
				line->endY = y;
				line->c = colorsList[i%10];
				drawables.push_back(std::move(line));
			}
		}
	}
	
	if (xmin == xmax) {
		xmin -= 1;
		xmax -= 1;
	}else{
		double changeBy = (xmax-xmin)*0.1;
		xmin -= changeBy;
		xmax += changeBy;
	}
	
	if (ymin == ymax) {
		ymin -= 1;
		ymax -= 1;
	}else{
		double changeBy = (ymax-ymin)*0.1;
		ymin -= changeBy;
		ymax += changeBy;
	}
}

std::pair<int,int> GraphWindow::fromScaledToPixels(double x, double y) {
	int xOut = t_x + App::text_padding + (x - xmin) * ((t_w - 2 * App::text_padding) / (xmax - xmin));
	int yOut = (screenStart + screenHeight) - (y - ymin) * (screenHeight / (ymax - ymin));
	return {xOut, yOut};
}

std::pair<double,double> GraphWindow::fromPixelsToScaled(int x, int y) {
	double xOut = xmin + (x - (t_x + App::text_padding)) * ((xmax - xmin) / (t_w - 2 * App::text_padding));
	double yOut = ymin + (-(y - (screenStart + screenHeight)) * ((ymax - ymin) / screenHeight));
	return {xOut, yOut};
}

void GraphWindow::render() {
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.overlay_background_color, true, 5);
	
	reset->render();
	
	int widths = t_w-2*App::text_padding;
	int startX = t_x+App::text_padding;
	
	
	if (App::mouseX >= startX && App::mouseX <= startX+widths && App::mouseY >= screenStart && App::mouseY <= screenStart+screenHeight) {
		auto loc = fromPixelsToScaled(App::mouseX, App::mouseY);
		
		auto xStr = doubleToUnicodeString_pretty(loc.first);
		auto yStr = doubleToUnicodeString_pretty(loc.second);
		auto str = xStr + icu::UnicodeString::fromUTF8(", ")+yStr;
		
		int width = TextRenderer::get_text_width(str.length());
		
		TextRenderer::draw_text(t_x + t_w - width - App::text_padding, t_y + App::text_padding, str, App::theme.main_text_color);
	}
	
	App::DrawRect(startX, screenStart, widths, screenHeight, App::theme.main_background_color);
	
	// draw graphs here
	
	App::runWithSKIZ(startX, screenStart, widths, screenHeight, [&](){
		auto lineVals = fromScaledToPixels(0, 0);
		
		App::DrawLine(startX, lineVals.second, widths+startX, lineVals.second, 1, App::theme.main_text_color);
		App::DrawLine(lineVals.first, screenStart, lineVals.first, screenStart+screenHeight, 1, App::theme.main_text_color);
		
		for (const auto& d : drawables) {
			auto xy = fromScaledToPixels(d->x, d->y);
			
			if (auto circle = dynamic_cast<DrawCircle*>(d.get())) {
				App::DrawCircle(xy.first, xy.second, App::text_padding, 10, circle->c);
			}else if (auto line = dynamic_cast<DrawLine*>(d.get())) {
				auto xy2 = fromScaledToPixels(line->endX, line->endY);
				App::DrawLine(xy.first, xy.second, xy2.first, xy2.second, (float)App::text_padding/2, line->c);
			}
		}
		
		if (selectingSquare) {
			int x2 = fmax(startX, fmin(App::mouseX, startX+widths));
			int y2 = fmax(screenStart, fmin(App::mouseY, screenStart+screenHeight));
			App::DrawRect(squareStartX, squareStartY, x2-squareStartX, y2-squareStartY, App::theme.add_panel);
			App::DrawBorder(squareStartX, squareStartY, x2-squareStartX, y2-squareStartY, App::theme.border);
		}
	});
	
	App::DrawInverseRoundedRect(startX, screenStart, widths, screenHeight, App::text_padding, App::theme.overlay_background_color, 5);
	App::DrawRoundBorder(startX, screenStart, widths, screenHeight, App::theme.border, 5, App::text_padding);
	
	// now we'll draw the data panel
	
	int dataStart = screenStart+screenHeight+App::text_padding;
	int dataHeight = (t_y+t_h) - dataStart - App::text_padding;
	
	App::DrawRect(startX, dataStart, widths, dataHeight, App::theme.main_background_color);
	
	// draw data panel here
	
	App::runWithSKIZ(startX, dataStart, widths, dataHeight, [&](){
		
	});
	
	App::DrawInverseRoundedRect(startX, dataStart, widths, dataHeight, App::text_padding, App::theme.overlay_background_color, 5);
	App::DrawRoundBorder(startX, dataStart, widths, dataHeight, App::theme.border, 5, App::text_padding);
}

bool GraphWindow::on_mouse_button_event(int button, int action, int mods) {
	int widths = t_w-2*App::text_padding;
	int startX = t_x+App::text_padding;
	
	if (selectingSquare && action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_1) {
		auto xy1 = fromPixelsToScaled(fmax(startX, fmin(App::mouseX, startX+widths)), fmax(screenStart, fmin(App::mouseY, screenStart+screenHeight)));
		auto xy2 = fromPixelsToScaled(squareStartX, squareStartY);
		
		xmin = fmin(xy1.first, xy2.first);
		xmax = fmax(xy1.first, xy2.first);
		ymin = fmin(xy1.second, xy2.second);
		ymax = fmax(xy1.second, xy2.second);
		
		selectingSquare = false;
	}
	
	return Widget::on_mouse_button_event(button, action, mods);
}

bool GraphWindow::on_mouse_move_event() {
	bool clicking = glfwGetMouseButton(App::window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	
	int widths = t_w-2*App::text_padding;
	int startX = t_x+App::text_padding;
	
	if (selectingSquare && !clicking) {
		auto xy1 = fromPixelsToScaled(fmax(startX, fmin(App::mouseX, startX+widths)), fmax(screenStart, fmin(App::mouseY, screenStart+screenHeight)));
		auto xy2 = fromPixelsToScaled(squareStartX, squareStartY);
		
		xmin = fmin(xy1.first, xy2.first);
		xmax = fmax(xy1.first, xy2.first);
		ymin = fmin(xy1.second, xy2.second);
		ymax = fmax(xy1.second, xy2.second);
		
		selectingSquare = false;
	}else if (clicking && !selectingSquare && App::mouseX >= startX && App::mouseX <= startX+widths && App::mouseY >= screenStart && App::mouseY <= screenStart+screenHeight) {
		selectingSquare = true;
		squareStartX = App::mouseX;
		squareStartY = App::mouseY;
	}
	
	return Widget::on_mouse_move_event();
}

void GraphWindow::executeAction(WidgetActionType t) {
	if (t == WidgetActionType::THEME_CALCULATED) {
		setColors();
	}
}

void GraphWindow::setColors() {
	std::vector<std::vector<int>> cols;
	
	if (!App::darkmode) {
		cols = {
			{31, 119, 180},
			{227, 26, 28},
			{44, 160, 44},
			{255, 127, 14},
			{148, 103, 189},
			{140, 86, 75},
			{227, 119, 194},
			{127, 127, 127},
			{188, 189, 34},
			{23, 190, 207}
		};
	}else{
		cols = {
			{102, 197, 255},
			{255, 107, 107},
			{114, 211, 114},
			{255, 179, 102},
			{201, 166, 235},
			{204, 150, 139},
			{247, 178, 226},
			{200, 200, 200},
			{230, 231, 111},
			{106, 231, 242}
		};
	}
	
	for (int i = 0; i < 10; i++) {
		colorsList[i]->r = (float)cols[i][0]/255;
		colorsList[i]->g = (float)cols[i][1]/255;
		colorsList[i]->b = (float)cols[i][2]/255;
	}
}