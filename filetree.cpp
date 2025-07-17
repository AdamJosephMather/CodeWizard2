#include "filetree.h"
#include "text_renderer.h"
#include "application.h"

FileTree::FileTree(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("FileTree");
	
	openpaths = { App::settings->getValue("current_folder", getExecutableDir()) };
}

void FileTree::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	for (auto itm : toRender) {
		App::DrawRect(itm.x+2, itm.y+2, itm.w-4, itm.h-4, App::theme.hover_background_color);
		TextRenderer::draw_text(itm.x+App::text_padding, itm.y+App::text_padding, itm.name, AllOneColor(App::theme.main_text_color, itm.name.length()));
	}
	
	Widget::render();
}

void FileTree::deleteTree(TreeStructure* node) {
	for (auto c : node->childrenFolders) {
		deleteTree(c);
	}
	for (auto c : node->childrenFiles) {
		deleteTree(c);
	}
	delete node;
}

void FileTree::request_close(Widget::close_callback_type cllbck) {
	if (closing) {
		return;
	}
	
	deleteTree(root);
	
	Widget::request_close(cllbck);
}

void FileTree::fillOutTree(TreeStructure* el) {
	try {
		if (!std::filesystem::exists(el->path)) {
			return;
		}
		
		std::filesystem::path itm(el->path);
		
		if (std::filesystem::is_directory(itm)){
			el->is_folder = true;
			
			if (std::find(openpaths.begin(), openpaths.end(), el->path) != openpaths.end()) {
				for (const auto& entry : std::filesystem::directory_iterator(itm)){
					if (!entry.is_directory()) {
						continue;
					}
					
					auto ts = new TreeStructure();
					ts->path = entry.path().string();
					ts->name = icu::UnicodeString::fromUTF8(entry.path().filename().string());
					el->childrenFolders.push_back(ts);
					fillOutTree(ts);
				}
				
				for (const auto& entry : std::filesystem::directory_iterator(itm)){
					if (!entry.is_regular_file()) {
						continue;
					}
					
					auto ts = new TreeStructure();
					ts->path = entry.path().string();
					ts->name = icu::UnicodeString::fromUTF8(entry.path().filename().string());
					el->childrenFiles.push_back(ts);
					fillOutTree(ts);
				}
			}
		}else {
			el->is_folder = false;
			el->name = icu::UnicodeString::fromUTF8(itm.filename().string());
		}
	} catch(const std::filesystem::filesystem_error& e){
		
	}
	
	App::time_till_regular = 2;
}

double FileTree::createVisuals(double pos, double depth, TreeStructure* el) {
	int x = depth+t_x;
	int y = pos*elHeighto+t_y;
	
	icu::UnicodeString str = el->name;
	if (el->is_folder) {
		UChar32 bs = U'\\';
		str.append(bs);
	}
	int w = TextRenderer::get_text_width(str.length())+App::text_padding*2;
	
	toRender.push_back( { x, y, w, elHeighto, str, el } );
	
	pos ++;
	
	double newdepth = depth+TextRenderer::get_text_width(3);
	
	for (auto itm : el->childrenFolders) {
		pos = createVisuals(pos, newdepth, itm);
	}
	
	for (auto itm : el->childrenFiles) {
		pos = createVisuals(pos, newdepth, itm);
	}
	
	max_scroll_vert = pos-1+scrolled_to_vert;
	max_scroll_horz = max(max_scroll_horz, x+w+scrolled_to_horz);
	
	return pos;
}

void FileTree::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	if (!root) {
		root = new TreeStructure();
		root->path = App::settings->getValue("current_folder", getExecutableDir());
		root->name = icu::UnicodeString::fromUTF8(std::filesystem::path(root->path).filename().string());
		fillOutTree(root);
	}
	
	elHeighto = TextRenderer::get_text_height()+App::text_padding*2;
	toRender.clear();
	
	max_scroll_horz = 0.0;
	createVisuals(-scrolled_to_vert, -scrolled_to_horz, root);
	
	if (scrolled_to_vert > max_scroll_vert) {
		scrolled_to_vert = max_scroll_vert;
	}else if (scrolled_to_vert < 0.0) {
		scrolled_to_vert = 0.0;
	}
	
	if (scrolled_to_horz > max_scroll_horz) {
		scrolled_to_horz = max_scroll_horz;
	}else if (scrolled_to_horz < 0.0) {
		scrolled_to_horz = 0.0;
	}
	
	Widget::position(x, y, w, h);
}

bool FileTree::on_mouse_button_event(int button, int action, int mods){
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (t_x > mx || t_y > my || t_x+t_w < mx || t_y+t_h < my) {
		return false;
	}
	
	for (auto vs : toRender) {
		if (vs.x <= mx && vs.y <= my && vs.x+vs.w >= mx && vs.y+vs.h >= my) {
			if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
				if (!vs.ts) {
					break;
				}
				if (vs.ts->is_folder) {
					auto it = std::find(openpaths.begin(), openpaths.end(), vs.ts->path);
					if (it != openpaths.end()) {
						openpaths.erase(it);
					}else{
						openpaths.push_back(vs.ts->path);
					}
				}else{
					// let's open the file now
					std::filesystem::path p(vs.ts->path);
					App::openFromCMD(vs.ts->path, p.filename().string());
				}
				
				deleteTree(root);
				root = new TreeStructure();
				root->path = App::settings->getValue("current_folder", getExecutableDir());
				root->name = icu::UnicodeString::fromUTF8(std::filesystem::path(root->path).filename().string());
				fillOutTree(root);
			}
			break;
		}
	}
	
	return true;
}

void FileTree::save() { // we'll update the actuall tree structure every x seconds, (onsave) or when the user clicks something
	deleteTree(root);
	root = new TreeStructure();
	root->path = App::settings->getValue("current_folder", getExecutableDir());
	root->name = icu::UnicodeString::fromUTF8(std::filesystem::path(root->path).filename().string());
	
	fillOutTree(root);
}

bool FileTree::on_scroll_event(double xchange, double ychange){
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (t_x > mx || t_y > my || t_x+t_w < mx || t_y+t_h < my) {
		return false;
	}
	
	if (glfwGetKey(App::window, GLFW_KEY_LEFT_SHIFT)){
		std::swap(xchange, ychange);
	}
	
	scrolled_to_vert += ychange*6;
	scrolled_to_horz += xchange*6*6;
	
	if (scrolled_to_vert > max_scroll_vert) {
		scrolled_to_vert = max_scroll_vert;
	}else if (scrolled_to_vert < 0.0) {
		scrolled_to_vert = 0.0;
	}
	
	if (scrolled_to_horz > max_scroll_horz) {
		scrolled_to_horz = max_scroll_horz;
	}else if (scrolled_to_horz < 0.0) {
		scrolled_to_horz = 0.0;
	}
	
	return true;	
}