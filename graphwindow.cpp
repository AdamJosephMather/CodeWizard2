#include "graphwindow.h"
#include "application.h"
#include "text_renderer.h"
#include "tinyfiledialogs.h"

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
	
	addCords = new Button(this, icu::UnicodeString::fromUTF8("Add Coords"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+App::text_padding*2;
		btn->t_y = screenStart+screenHeight+App::text_padding*2 - scroll;
	},  [&](Button* btn){
		// onclick
		addThing(false);
	});
	addCords->border = true;
	addCords->rounded = true;
	
	addFile = new Button(this, icu::UnicodeString::fromUTF8("Add File"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = addCords->t_x+addCords->t_w+App::text_padding;
		btn->t_y = addCords->t_y;
	},  [&](Button* btn){
		// onclick
		addThing(true);
	});
	addFile->border = true;
	addFile->rounded = true;
	
	addFolder = new Button(this, icu::UnicodeString::fromUTF8("Add Folder"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = addFile->t_x+addFile->t_w+App::text_padding;
		btn->t_y = addFile->t_y;
	},  [&](Button* btn){
		// onclick
		std::string fldr = App::settings->getValue("current_folder", std::string());
		
		const char * fpr = tinyfd_selectFolderDialog(
			"Select folder",
			fldr.c_str()
		);
		
		if (!fpr) {
			return;
		}
		
		auto files = get_files_in_directory(fpr);
		
		for (auto f : files) {
			addThing(true, f.string());
		}
	});
	addFolder->border = true;
	addFolder->rounded = true;
	
	averageText = new TextEdit(this, [=](Widget* t){
		t->t_x = addFolder->t_x+addFolder->t_w+App::text_padding+TextRenderer::get_text_width(4);
		t->t_y = addFile->t_y;
		t->t_w = TextRenderer::get_text_width(20);
		t->t_h = TextRenderer::get_text_height()+2*App::text_padding;
	});
	averageText->rounded = true;
	averageText->border = true;
	averageText->setFullText(icu::UnicodeString::fromUTF8("1"));
	
	Button* addCords;
	Button* addFile;
	
	recalculateDrawables();
}

std::vector<std::filesystem::path> GraphWindow::get_files_in_directory(const std::filesystem::path& dir_path) {
	std::vector<std::filesystem::path> file_paths;
	
	try {
		if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
			for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
				// Filter out subdirectories; only keep files
				if (std::filesystem::is_regular_file(entry.status())) {
					// Convert path to an absolute full path
					file_paths.push_back(std::filesystem::absolute(entry.path()));
				}
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		std::cerr << "File system error: " << e.what() << '\n';
	}
	
	return file_paths;
}

void GraphWindow::addThing(bool isfile, std::string filepath) {
	DataType line;
	
	if (isfile) {
		line.file = true;
		
		std::string filePath;
		
		if (filepath == "") {
			const char * fp = tinyfd_openFileDialog(
				"Select a file",    // dialog title
				"",                 // default path and filename
				0, NULL, NULL,      // filter count and filters
				0                   // allow multiple selections (0 = no)
			);
			
			if (fp) {
				filePath = std::string(fp);
			}else{
				return;
			}
		}else{
			filePath = filepath;
		}
		
		std::filesystem::path fullPath = filePath;
		std::string filename = fullPath.filename().string();
		
		if (!fileExists(filePath) || isBinaryFile(filePath)) {
			return;
		}
		
		bool worked = true;
		icu::UnicodeString text = App::readFileToUnicodeString(filePath, worked);
		
		if (!worked) {
			return;
		}
		
		auto lines = splitByChar(text, U'\n');
		
		lines.erase( std::remove_if(lines.begin(), lines.end(), [](const auto& line) { return line.length() == 0; }), lines.end() );
		
		worked = false;
		auto line1Vals = getVals(lines[0], worked);
		
		if (!worked) {
			App::displayToast(icu::UnicodeString::fromUTF8("Couldn't parse line 1."));
			return;
		}
		
		if (lines.size() == 1) { // y only
			for (int i = 0; i < line1Vals.size(); i++) {
				line.x.push_back(i);
			}
			line.y = line1Vals;
		}else if (lines.size() == 2) { // assuming x and y
			worked = false;
			auto line2Vals = getVals(lines[1], worked);
			
			if (!worked) {
				App::displayToast(icu::UnicodeString::fromUTF8("Couldn't parse line 2."));
				return;
			}
			
			if (line1Vals.size() != line2Vals.size()) {
				App::displayToast(icu::UnicodeString::fromUTF8("X and Y Lines don't match size."));
				return;
			}
			
			line.x = line1Vals;
			line.y = line2Vals;
		}else {
			int expecting = line1Vals.size();
			
			line.x = {};
			line.y = {};
			
			if (expecting < 1 || expecting > 2) {
				App::displayToast(icu::UnicodeString::fromUTF8("Format unknown, see help."));
				return;
			}
			
			for (int lineNum = 0; lineNum < lines.size(); lineNum++) {
				auto lineUcode = lines[lineNum];
				
				worked = false;
				auto lineVals = getVals(lineUcode, worked);
				
				if (!worked) {
					App::displayToast(icu::UnicodeString::fromUTF8("Couldn't parse line "+std::to_string(lineNum+1)+"."));
					return;
				}
				
				if (lineVals.size() != expecting) {
					App::displayToast(icu::UnicodeString::fromUTF8("Line "+std::to_string(lineNum+1)+" doesn't have the right number of values."));
					return;
				}
				
				if (expecting == 2) {
					line.x.push_back(lineVals[0]);
					line.y.push_back(lineVals[1]);
				}else{
					line.x.push_back(lineNum);
					line.y.push_back(lineVals[0]);
				}
			}
		}
		
		if (line.x.size() == 0 || line.y.size() == 0) {
			App::displayToast(icu::UnicodeString::fromUTF8("No graphable data."));
			return;
		}
		
		line.filename = icu::UnicodeString::fromUTF8(filename);
		line.filepath = filePath;
	}
	
	int thisId = id_glob;
	line.id = thisId;
	id_glob += 1;
	
	Button* remBut;
	Button* scatBut;
	
	remBut = new Button(this, icu::UnicodeString::fromUTF8("Remove"), [=](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+App::text_padding*3;
		
		btn->t_y = addCords->t_y + addCords->t_h + App::text_padding*2 + (TOTAL_ITEM_HEIGHT + App::text_padding) * EXISTING_VAL;
		EXISTING_VAL += 1;
	}, [&, thisId](Button* btn){
		// onclick
		for (int i = 0; i < allData.size(); i++) {
			if (allData[i].id == thisId) {
				// delete associated widgets
				if (allData[i].x_edit) {
					App::deleteWidget(allData[i].x_edit);
					App::deleteWidget(allData[i].y_edit);
				}
				
				App::deleteWidget(allData[i].removeButton);
				App::deleteWidget(allData[i].scatterButton);
				
				allData.erase(allData.begin()+i); // erase the entrya
				break;
			}
		}
		
		recalculateDrawables(false);
	});
	remBut->border = true;
	remBut->rounded = true;
	
	scatBut = new Button(this, icu::UnicodeString::fromUTF8("Line"), [=](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = remBut->t_x+remBut->t_w+App::text_padding;
		btn->t_y = remBut->t_y;
	}, [&, thisId](Button* btn){
		// onclick
		for (int i = 0; i < allData.size(); i++) {
			if (allData[i].id == thisId) {
				if (allData[i].scatter) {
					btn->BUTTON_LABEL = icu::UnicodeString::fromUTF8("Line");
				}else {
					btn->BUTTON_LABEL = icu::UnicodeString::fromUTF8("Scatter");
				}
				
				allData[i].scatter = !allData[i].scatter;
				
				recalculateDrawables(false);
				break;
			}
		}
	});
	scatBut->border = true;
	scatBut->rounded = true;
	
	line.removeButton = remBut;
	line.scatterButton = scatBut;
	
	if (!isfile) {
		TextEdit* xed;
		TextEdit* yed;
		
		xed = new TextEdit(this, [=](Widget* t){
			t->t_x = remBut->t_x + 2*App::text_padding + TextRenderer::get_text_width(2);
			t->t_y = remBut->t_y+remBut->t_h+App::text_padding;
			t->t_w = t_w - 8*App::text_padding - TextRenderer::get_text_width(2);
			t->t_h = TextRenderer::get_text_height()+2*App::text_padding;
		});
		xed->rounded = true;
		xed->border = true;
		yed = new TextEdit(this, [=](Widget* t){
			t->t_x = xed->t_x;
			t->t_y = xed->t_y+xed->t_h+App::text_padding;
			t->t_w = xed->t_w;
			t->t_h = xed->t_h;
		});
		yed->rounded = true;
		yed->border = true;
		
		xed->onlinechange = [&, thisId](EditType typ, int lineindex){
			updateInfoFor(thisId);
		};
		yed->onlinechange = [&, thisId](EditType typ, int lineindex){
			updateInfoFor(thisId);
		};
		
		line.x_edit = xed;
		line.y_edit = yed;
	}
	
	line.c = colorsList[line.id%10];
	
	allData.push_back(line);
	
	if (isfile) {
		recalculateDrawables();
	}
}

bool GraphWindow::on_key_event(int key, int scancode, int action, int mods) {
	if (App::activeLeafNode == averageText && key == GLFW_KEY_ENTER) {
		if (action == GLFW_PRESS) {
			recalculateDrawables(false);
		}
		return true;
	}
	return Widget::on_key_event(key, scancode, action, mods);
}

std::vector<double> GraphWindow::getVals(icu::UnicodeString text, bool& allgood) {
	text = stripOfChar(text, U' ');
	text = stripOfChar(text, U'\n');
	text = stripOfChar(text, U'\t');
	auto sects = splitByChar(text, U',');
	
	std::vector<double> out = {};
	
	for (auto s : sects) {
		if (s.length() == 0) {
			continue;
		}
		
		bool worked = false;
		double xvl = unicodeStringToDouble_quick(s, worked);
		if (worked) {
			out.push_back(xvl);
		}else{
			allgood = false;
			return {};
		}
	}
	allgood = true;
	return out;
}

void GraphWindow::updateInfoFor(int id) {
	for (int i = 0; i < allData.size(); i++) {
		if (allData[i].id == id) {
			// this is a list of values, separated by commas and optional spaces
			
			icu::UnicodeString textX = allData[i].x_edit->getFullText();
			icu::UnicodeString textY = allData[i].y_edit->getFullText();
			
			allData[i].x = {};
			allData[i].y = {};
			
			bool worked = false;
			std::vector<double> newx = getVals(textX, worked);
			if (!worked) {
				recalculateDrawables();
				return;
			}
			worked = false;
			std::vector<double> newy = getVals(textY, worked);
			if (!worked) {
				recalculateDrawables();
				return;
			}
			
			if (newx.size() == newy.size() && newy.size() == 0) { // donothing
			}else if (newx.size() == 0){
				for (int i = 0; i < newy.size(); i++) {
					newx.push_back(i);
				}
			}else if (newy.size() == 0){
				for (int i = 0; i < newx.size(); i++) {
					newy.push_back(i);
				}
			}else if (newy.size() == newx.size()){ // donothing
			}else{
				recalculateDrawables();
				return; // this is not good, somthing is wrong.
			}
			
			allData[i].x = newx;
			allData[i].y = newy;
			
			break;
		}
	}
	
	recalculateDrawables();
}

void GraphWindow::position(int x, int y, int w, int h) {
	OldRenderState NEWSTATE = {x, y, w, h, reset->t_h, reset->t_y};
	if (OLDSTATE.t_x != NEWSTATE.t_x || OLDSTATE.t_y != NEWSTATE.t_y || OLDSTATE.t_w != NEWSTATE.t_w || OLDSTATE.t_h != NEWSTATE.t_h || OLDSTATE.b_h != NEWSTATE.b_h || OLDSTATE.b_y != NEWSTATE.b_y || selectingSquare) {
		rerender = true;
		OLDSTATE = NEWSTATE;
	}
	
	screenHeight = h*.6;
	screenStart = reset->t_y+reset->t_h+App::text_padding;
	
	// 1 button, 2 text edits. Ie, " B T T " -> 4*pad + 1*button + 2*textedit
	TOTAL_ITEM_HEIGHT = 4*App::text_padding + (TextRenderer::get_text_height() + App::text_padding*2) * 3;
	
	
	EXISTING_VAL = 0;
	Widget::position(x, y, w, h);
}

bool GraphWindow::on_scroll_event(double xchange, double ychange) {
	int widths = t_w-2*App::text_padding;
	int startX = t_x+App::text_padding;
	
	bool isShiftDown = (glfwGetKey(App::window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) || (glfwGetKey(App::window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
	
	if (App::mouseX >= startX && App::mouseX <= startX+widths && App::mouseY >= screenStart && App::mouseY <= screenStart+screenHeight) {
		double changeBy = (xmax-xmin)*0.1 * xchange; // always affect x by ychange. But, also affect it by xchange
		xmin -= changeBy;
		xmax += changeBy;
		
		changeBy = (xmax-xmin)*0.1 * ychange;
		xmin -= changeBy;
		xmax += changeBy;
		
		if (!isShiftDown) {
			changeBy = (ymax-ymin)*0.1 * ychange;
			ymin -= changeBy;
			ymax += changeBy;
		}
		
		rerender = true;
		
		return true;
	}else if (App::mouseX >= startX && App::mouseX <= startX+widths && App::mouseY >= screenStart+screenHeight && App::mouseY <= t_h+t_y){
		scroll += ychange * 6 * TextRenderer::get_text_height();
		
		int maxScroll = allData.size() * (TOTAL_ITEM_HEIGHT+App::text_padding);
		
		if (scroll < 0) {
			scroll = 0;
		}else if (scroll > maxScroll) {
			scroll = maxScroll;
		}
		
		return true;
	}
	
	return false;
}

void GraphWindow::recalculateDisplayedValues() {
	auto at = averageText->getFullText();
	std::string strval;
	at.toUTF8String(strval);
	
	int avVal = 1;
	
	try {
		avVal = std::stoi(strval);
	}catch (const std::exception& e) {
		avVal = 1;
		App::displayToast(icu::UnicodeString::fromUTF8("Couldn't parse average value."));
	}
	
	for (int i = 0; i < allData.size(); i++) {
		if (avVal <= 1 || allData[i].scatter) {
			allData[i].modified_x = allData[i].x;
			allData[i].modified_y = allData[i].y;
			continue;
		}
	
		const auto& x = allData[i].x;
		const auto& y = allData[i].y;
	
		allData[i].modified_x.clear();
		allData[i].modified_y.clear();
	
		int sourceSize = static_cast<int>(std::min(x.size(), y.size()));
	
		if (sourceSize < avVal) {
			continue;
		}
	
		int newSize = sourceSize - avVal + 1;
	
		allData[i].modified_x.reserve(newSize);
		allData[i].modified_y.reserve(newSize);
	
		double runningSum = 0.0;
	
		// Initial window
		for (int j = 0; j < avVal; j++) {
			runningSum += y[j];
		}
	
		allData[i].modified_y.push_back(runningSum / avVal);
	
		// Use the x value at the center of the window.
		allData[i].modified_x.push_back(x[avVal / 2]);
	
		// Slide the window forward one sample at a time.
		for (int j = avVal; j < sourceSize; j++) {
			runningSum += y[j];
			runningSum -= y[j - avVal];
	
			allData[i].modified_y.push_back(runningSum / avVal);
	
			// Center x value of current window: [j - avVal + 1, j]
			int centerIndex = j - avVal / 2;
			allData[i].modified_x.push_back(x[centerIndex]);
		}
	}
}

void GraphWindow::recalculateDrawables(bool changeEm) {
	rerender = true;
	drawables.clear();
	
	recalculateDisplayedValues();
	
	bool setFirst = changeEm;
	
	if (changeEm) {
		xmin = -10;
		xmax = 10;
		ymin = -10;
		ymax = 10;
	}
	
	for (size_t i = 0; i < allData.size(); i++) {
		if (allData[i].modified_x.size() != allData[i].modified_y.size()) {
			App::displayToast(icu::UnicodeString::fromUTF8("Ommitting data, non-matching x/y count"));
			continue;
		}
		
		for (size_t j = 0; j < allData[i].modified_x.size(); j++) {
			double x = allData[i].modified_x[j];
			double y = allData[i].modified_y[j];
			
			if (changeEm) {
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
			}
			
			if (allData[i].scatter) {
				auto circle = std::make_unique<DrawCircle>();
				circle->x = x;
				circle->y = y;
				circle->c = allData[i].c;
				drawables.push_back(std::move(circle));
			}else if (j != 0){
				auto line = std::make_unique<DrawLine>();
				line->x = allData[i].modified_x[j-1];
				line->y = allData[i].modified_y[j-1];
				line->endX = x;
				line->endY = y;
				line->c = allData[i].c;
				drawables.push_back(std::move(line));
			}
		}
	}
	
	if (changeEm) {
		if (xmin == xmax) {
			xmin -= 1;
			xmax += 1;
		}else{
			double changeBy = (xmax-xmin)*0.1;
			xmin -= changeBy;
			xmax += changeBy;
		}
		
		if (ymin == ymax) {
			ymin -= 1;
			ymax += 1;
		}else{
			double changeBy = (ymax-ymin)*0.1;
			ymin -= changeBy;
			ymax += changeBy;
		}
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
	if (rerender) {
		App::reclear = 2;
	}
	
	if (App::reclear != 0) {
		App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.overlay_background_color, true, 5);
	}else{
		App::DrawRect(t_x+App::text_padding, t_y+App::text_padding, t_w-2*App::text_padding, reset->t_h, App::theme.overlay_background_color);
	}
	
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
	
	if (App::reclear != 0) {
		App::DrawRect(startX, screenStart, widths, screenHeight, App::theme.main_background_color);
	}
	
	// draw graphs here
	
	App::runWithSKIZ(startX, screenStart, widths, screenHeight, [&](){
		if (App::reclear == 0) {
			return;
		}
		
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
	
	
	if (App::reclear != 0) {
		App::DrawInverseRoundedRect(startX, screenStart, widths, screenHeight, App::text_padding, App::theme.overlay_background_color, 5);
		App::DrawRoundBorder(startX, screenStart, widths, screenHeight, App::theme.border, 5, App::text_padding);
		rerender = false;
	}
	
	// now we'll draw the data panel
	
	int dataStart = screenStart+screenHeight+App::text_padding;
	int dataHeight = (t_y+t_h) - dataStart - App::text_padding;
	
	App::DrawRect(startX, dataStart, widths, dataHeight, App::theme.main_background_color);
	
	// draw data panel here
	
	App::runWithSKIZ(startX, dataStart, widths, dataHeight, [&](){
		addCords->render();
		addFile->render();
		addFolder->render();
		TextRenderer::draw_text(addFolder->t_x+addFolder->t_w+App::text_padding, averageText->t_y+App::text_padding, icu::UnicodeString::fromUTF8("SMA:"), App::theme.main_text_color);
		App::runWithSKIZ(averageText->t_x, averageText->t_y, averageText->t_w, averageText->t_h, [&](){
			averageText->render();
		});
		
		int itemsStart = t_x+App::text_padding*2;
		int itemsWidth = t_w-4*App::text_padding;
		
		for (int i = 0; i < allData.size(); i++) {
			int y = addCords->t_y+addCords->t_h+App::text_padding+(App::text_padding+TOTAL_ITEM_HEIGHT)*i;
			
			App::DrawRoundedRect(itemsStart, y, itemsWidth, TOTAL_ITEM_HEIGHT, App::text_padding, App::theme.darker_background_color, false, 5);
			App::DrawRoundBorder(itemsStart, y, itemsWidth, TOTAL_ITEM_HEIGHT, allData[i].c, App::text_padding, 5);
			
			allData[i].removeButton->render();
			allData[i].scatterButton->render();
			if (allData[i].x_edit) {
				TextRenderer::draw_text(allData[i].x_edit->t_x-App::text_padding-TextRenderer::get_text_width(2), allData[i].x_edit->t_y+App::text_padding, icu::UnicodeString::fromUTF8("x:"), App::theme.main_text_color);
				App::runWithSKIZ(allData[i].x_edit->t_x, allData[i].x_edit->t_y, allData[i].x_edit->t_w, allData[i].x_edit->t_h, [&](){
					allData[i].x_edit->render();
				});
				TextRenderer::draw_text(allData[i].y_edit->t_x-App::text_padding-TextRenderer::get_text_width(2), allData[i].y_edit->t_y+App::text_padding, icu::UnicodeString::fromUTF8("y:"), App::theme.main_text_color);
				App::runWithSKIZ(allData[i].y_edit->t_x, allData[i].y_edit->t_y, allData[i].y_edit->t_w, allData[i].y_edit->t_h, [&](){
					allData[i].y_edit->render();
				});
			}else if (allData[i].file) {
				int offset = (float)(TOTAL_ITEM_HEIGHT+allData[i].removeButton->t_h - App::text_padding - TextRenderer::get_text_height())/2;
				
				TextRenderer::draw_text(itemsStart+offset - App::text_padding - allData[i].removeButton->t_h, y + offset, allData[i].filename, App::theme.main_text_color);
			}
		}
	});
	
	App::DrawInverseRoundedRect(startX, dataStart, widths, dataHeight, App::text_padding, App::theme.overlay_background_color, 5);
	App::DrawRoundBorder(startX, dataStart, widths, dataHeight, App::theme.border, 5, App::text_padding);
	
	App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color, 5);
	App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
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
		canSelect = false;
		rerender = true;
	}else if (App::mouseX >= startX && App::mouseX <= startX+widths && App::mouseY >= screenStart && App::mouseY <= screenStart+screenHeight) {
		if (!selectingSquare && action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_1) {
			squareStartX = App::mouseX;
			squareStartY = App::mouseY;
			canSelect = true;
		}else{
			canSelect = false;
		}
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
	}else if (canSelect && clicking && !selectingSquare && App::mouseX >= startX && App::mouseX <= startX+widths && App::mouseY >= screenStart && App::mouseY <= screenStart+screenHeight) {
		selectingSquare = true;
		
	}
	
	return Widget::on_mouse_move_event();
}

void GraphWindow::executeAction(WidgetActionType t) {
	if (t == WidgetActionType::THEME_CALCULATED) {
		setColors();
		rerender = true;
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