#include "filetree.h"
#include "text_renderer.h"
#include "application.h"

#include <stb_image.h>

FileTree::FileTree(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("FileTree");
	
	openpaths = { App::settings->getValue("current_folder", getExecutableDir()) };
	
	folderIcon = prepareTexture(getExecutableDir()+"\\folderIcon.png");
	fileIcon = prepareTexture(getExecutableDir()+"\\fileIcon.png");
}

GLuint FileTree::prepareTexture(std::string imagepath) {
	int channels;
	int imgW;
	int imgH;
	
	unsigned char* data = stbi_load(
		imagepath.c_str(),
		&imgW, &imgH, &channels,
		STBI_rgb_alpha
	);
	
	if (!data) {
		App::displayToast(icu::UnicodeString::fromUTF8("Failed to load image: "+imagepath));
		return -1;
	}
	
	GLuint texID;
	// Generate GL texture
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(
		GL_TEXTURE_2D, 0, GL_RGBA,
		imgW, imgH, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, data
	);
	// Simple linear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(data);
	
	return texID;
}

void FileTree::renderTexture(GLuint texID, int x, int y, int w, int h) {
	if (texID == (GLuint)-1) { return; }

	glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texID);

	// 1) Set the main text color as the PRIMARY_COLOR
	glColor4f(App::theme.main_text_color->r,
			  App::theme.main_text_color->g,
			  App::theme.main_text_color->b,
			  App::theme.main_text_color->a);

	// 2) Background color (behind the icon)
	float blendColor[] = { back_color->r, back_color->g, back_color->b, 1.0f };
	glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blendColor);

	// 3) Configure combiner:
	// result.rgb = PRIMARY_COLOR * tex.a + CONSTANT(back) * (1 - tex.a)
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);

	// Arg0: foreground = main text color (PRIMARY_COLOR)
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

	// Arg1: background = constant back_color
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

	// Arg2: mixer = texture alpha
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

	// We’re doing all the "alpha compositing" in RGB, so blending can stay off
	glDisable(GL_BLEND);

	// IMPORTANT: don’t overwrite the main_text_color with white anymore
	// glColor4f(1.0f, 1.0f, 1.0f, 1.0f);  // <-- remove this

	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f); glVertex2f(x,   y+h);
		glTexCoord2f(1.0f, 1.0f); glVertex2f(x+w, y+h);
		glTexCoord2f(1.0f, 0.0f); glVertex2f(x+w, y);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(x,   y);
	glEnd();

	glPopAttrib();
}

void FileTree::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	std::lock_guard<std::mutex> lock(tree_mutex);
	
	for (auto itm : toRender) {
		if (App::mouseX >= itm.x && App::mouseX <= itm.x+itm.w && App::mouseY > itm.y && App::mouseY <= itm.y+itm.h) {
			App::DrawRoundedRect(itm.x, itm.y, itm.w, itm.h, App::text_padding, App::theme.hover_background_color);
			App::DrawRoundBorder(itm.x, itm.y, itm.w, itm.h, App::theme.border, 5, App::text_padding);
			back_color = App::theme.hover_background_color;
		}else{
			back_color = App::theme.extras_background_color;
		}
		
		if (itm.is_folder) {
			renderTexture(folderIcon, itm.x+App::text_padding, itm.y+App::text_padding, TextRenderer::get_text_height(), TextRenderer::get_text_height());
		}else {
			renderTexture(fileIcon, itm.x+App::text_padding, itm.y+App::text_padding, TextRenderer::get_text_height(), TextRenderer::get_text_height());
		}
		
		TextRenderer::draw_text(itm.x+TextRenderer::get_text_height()+App::text_padding*2, itm.y+App::text_padding, itm.name, App::theme.main_text_color);
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
	
	int w = TextRenderer::get_text_width(str.length())+App::text_padding*3+TextRenderer::get_text_height();
	
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
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	std::lock_guard<std::mutex> lock(tree_mutex);
	
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
	
	if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) {
		return false;
	}
	
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
				root->name = icu::UnicodeString::fromUTF8(std::filesystem::path(root->path).filename().string());
				fillOutTree(root);
			}else{
				// let's open the file now
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
	newRoot->name = icu::UnicodeString::fromUTF8(std::filesystem::path(newRoot->path).filename().string());
	
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