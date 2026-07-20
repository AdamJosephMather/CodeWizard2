#include "filetree.h"
#include "text_renderer.h"
#include "application.h"

FileTree::FileTree(Widget* parent) : Widget(parent) {
	id = MST::toMonoString("FileTree");
	
	before_self_close = [&](){
		deleteTree(root);
	};
	
	openpaths = { App::settings->getValue("current_folder", getExecutableDir()) };
	
	folderIcon = App::prepareTexture(getExecutableDir()+"/folderIcon.png").tex;
	fileIcon = App::prepareTexture(getExecutableDir()+"/fileIcon.png").tex;
	
	if (folderIcon == (GLuint)-1 || fileIcon == (GLuint)-1) {
		App::displayToast(MST::toMonoString("Could not load texture(s) for filetree"));
	}
}

void FileTree::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	std::lock_guard<std::mutex> lock(tree_mutex);
	
	Color* textCol;
	for (auto itm : toRender) {
		if (cursor_in_this && App::mouseX >= itm.x && App::mouseX <= itm.x+itm.w && App::mouseY > itm.y && App::mouseY <= itm.y+itm.h) {

			App::DrawRoundedRect(itm.x, itm.y, itm.w, itm.h, App::text_padding, App::theme.hover_background_color);
			App::DrawRoundBorder(itm.x, itm.y, itm.w, itm.h, App::theme.active_color, 5, App::text_padding);
			back_color = App::theme.hover_background_color;
			textCol = App::theme.main_text_color;
		}else{
			textCol = App::theme.main_text_color;
			back_color = App::theme.extras_background_color;
		}
		
		if (itm.is_folder) {
			App::renderColorlessTexture(folderIcon, itm.x+App::text_padding/2, itm.y+App::text_padding/2, TextRenderer::get_text_height(), TextRenderer::get_text_height(), textCol, back_color);
		}else {
			App::renderColorlessTexture(fileIcon, itm.x+App::text_padding/2, itm.y+App::text_padding/2, TextRenderer::get_text_height(), TextRenderer::get_text_height(), textCol, back_color);
		}
		
		TextRenderer::draw_text(itm.x+TextRenderer::get_text_height()+App::text_padding, itm.y+(float)App::text_padding/2, itm.name, textCol);
	}
	
	Widget::render();
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
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
					ts->name = MST::toMonoString(entry.path().filename().string());
					el->childrenFolders.push_back(ts);
					fillOutTree(ts);
				}
				
				for (const auto& entry : std::filesystem::directory_iterator(itm)){
					if (!entry.is_regular_file()) {
						continue;
					}
					
					auto ts = new TreeStructure();
					ts->path = entry.path().string();
					ts->name = MST::toMonoString(entry.path().filename().string());
					el->childrenFiles.push_back(ts);
					fillOutTree(ts);
				}
			}
		}else {
			el->is_folder = false;
			el->name = MST::toMonoString(itm.filename().string());
		}
	} catch(const std::filesystem::filesystem_error& e){
		
	}
	
	App::time_till_regular = 2;
}

double FileTree::createVisuals(double pos, double depth, TreeStructure* el) {
	int x = depth+t_x;
	int y = pos*elHeighto+t_y+2;
	
	MST::MonoString str = el->name;
	
	int w = TextRenderer::get_text_width(str.length)+App::text_padding*3+TextRenderer::get_text_height();
	
	toRender.push_back( { x, y, w, elHeighto, str, el, el->is_folder } );
	
	pos ++;
	
	double newdepth = depth+TextRenderer::get_text_width(3);
	
	for (auto itm : el->childrenFolders) {
		pos = createVisuals(pos, newdepth, itm);
	}
	
	for (auto itm : el->childrenFiles) {
		pos = createVisuals(pos, newdepth, itm);
	}
	
	max_scroll_vert = pos-1+scrolled_to_vert;
	max_scroll_horz = std::max(max_scroll_horz, x+w+scrolled_to_horz);
	
	return pos;
}

void FileTree::position(int x, int y, int w, int h) {
	Widget::position(x, y, w, h);
	
	std::lock_guard<std::mutex> lock(tree_mutex);
	
	if (!root) {
		root = new TreeStructure();
		root->path = App::settings->getValue("current_folder", getExecutableDir());
		root->name = MST::toMonoString(std::filesystem::path(root->path).filename().string());
		fillOutTree(root);
	}
	
	elHeighto = TextRenderer::get_text_height()+App::text_padding;
	toRender.clear();
	
	max_scroll_horz = 0.0;
	createVisuals(-scrolled_to_vert, -scrolled_to_horz+2, root);
	
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
}

bool FileTree::on_mouse_button_event(int button, int action, int mods) {
	if (!cursor_in_this || button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	std::lock_guard<std::mutex> lock(tree_mutex);
	
	for (auto vs : toRender) {
		if (vs.x <= mx && vs.y <= my && vs.x+vs.w >= mx && vs.y+vs.h >= my) {
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
				
				deleteTree(root);
				root = new TreeStructure();
				root->path = App::settings->getValue("current_folder", getExecutableDir());
				root->name = MST::toMonoString(std::filesystem::path(root->path).filename().string());
				fillOutTree(root);
			}else{
				std::filesystem::path p(vs.ts->path);
				App::openFromCMD(vs.ts->path, p.filename().string());
			}
			break;
		}
	}
	
	return true;
}

void FileTree::save() {
	TreeStructure* newRoot = new TreeStructure();
	newRoot->path = App::settings->getValue("current_folder", getExecutableDir());
	newRoot->name = MST::toMonoString(std::filesystem::path(newRoot->path).filename().string());
	
	fillOutTree(newRoot);

	{
		std::lock_guard<std::mutex> lock(tree_mutex);
		
		TreeStructure* oldRoot = root;
		root = newRoot;
		
		toRender.clear(); 
		
		deleteTree(oldRoot); 
	}
}

bool FileTree::on_scroll_event(double xchange, double ychange){
	if (!cursor_in_this) {
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