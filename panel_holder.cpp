#include "panel_holder.h"


#include "application.h"
#include "editor.h"
#include "widgetchooser.h"
#include "codeedit.h"
#include "settings.h"
#include "filetree.h"
#include "text_renderer.h"

PanelHolder::PanelHolder(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Panel holder");
}

void PanelHolder::position(int x, int y, int width, int height) {
	t_x = x;
	t_y = y;
	t_w = width;
	t_h = height;
	
	handle_long = TextRenderer::get_text_height()*2;
	handle_short = TextRenderer::get_text_width(1);
	
	hastwo = (children.size() == 2);
	
	if (hastwo) {
		if (is_horizontal) {
			c1_x = x;
			c1_w = width*ratio;
			c2_x = x+c1_w;
			c2_w = width-c1_w;
			
			c1_y = c2_y = y;
			c1_h = c2_h = height;
		}else{
			c1_y = y;
			c1_h = height*ratio;
			c2_y = y+c1_h;
			c2_h = height-c1_h;
			
			c1_x = c2_x = x;
			c1_w = c2_w = width;
		}
		
		children[0]->position(c1_x, c1_y, c1_w, c1_h);
		children[1]->position(c2_x, c2_y, c2_w, c2_h);
	}else{
		Widget::position(t_x, t_y, t_w, t_h);
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	hovered = mx > t_x && mx < t_x+t_w && my > t_y && my < t_y+t_h;
	
	has_one_to_add = false;
	has_one_to_rem = false;
	
	first_hovered = mx > c1_x && mx < c1_x+c1_w && my > c1_y && my < c1_y+c1_h;
	second_hovered = mx > c2_x && mx < c2_x+c2_w && my > c2_y && my < c2_y+c2_h;
	
	if (hovered){
		if (!hastwo && App::curr_adding_panel) {
			has_one_to_add = true;
			adding_first_pos = false;
			
			int horz_dst = mx-(t_x+t_w/2);
			int vert_dst = my-(t_y+t_h/2);
			
			bool horz = abs(horz_dst) > abs(vert_dst);
			is_horizontal = horz;
			
			if (horz && horz_dst < 0) {
				x_nb = t_x;
				w_nb = t_w/2;
				y_nb = t_y;
				h_nb = t_h;
				adding_first_pos = true;
			}else if (horz) {
				x_nb = t_x+t_w/2;
				w_nb = t_w/2;
				y_nb = t_y;
				h_nb = t_h;
			}else if (vert_dst < 0) {
				x_nb = t_x;
				w_nb = t_w;
				y_nb = t_y;
				h_nb = t_h/2;
				adding_first_pos = true;
			}else {
				x_nb = t_x;
				w_nb = t_w;
				y_nb = t_y+t_h/2;
				h_nb = t_h/2;
			}
		}
		
		if (hastwo && App::curr_removing_panel) {
			if (first_hovered) {
				if (children[0]->children.size() == 1) {
					x_nb = c1_x;
					y_nb = c1_y;
					w_nb = c1_w;
					h_nb = c1_h;
					has_one_to_rem = true;
				}
			}else if (second_hovered) {
				if (children[1]->children.size() == 1) {
					x_nb = c2_x;
					y_nb = c2_y;
					w_nb = c2_w;
					h_nb = c2_h;
					has_one_to_rem = true;
				}
			}
		}
	}
	
	if (hastwo) {
		if (is_horizontal) {
			xpos_h = t_x + t_w*ratio;
			ypos_h = t_y + t_h/2;
			
			width_h = handle_short;
			height_h = handle_long;
		}else{
			xpos_h = t_x + t_w/2;
			ypos_h = t_y + t_h*ratio;
			
			width_h = handle_long;
			height_h = handle_short;
		}
	}
}

void PanelHolder::addWidget(bool horz, bool frst, Widget* new_wid) {
	is_horizontal = horz;
	
	auto existing_widget = children[0];
	
	auto p1_c = new PanelHolder(this);
	auto p2_c = new PanelHolder(this);
	
	if (frst){
		App::MoveWidget(existing_widget, p2_c);
		App::MoveWidget(new_wid, p1_c);
	}else{
		App::MoveWidget(existing_widget, p1_c);
		App::MoveWidget(new_wid, p2_c);
	}
	
	ratio = 0.5f;
	
	App::nada_panel();
}

bool PanelHolder::on_mouse_button_event(int button, int action, int mods) {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (has_one_to_add && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		addWidget(is_horizontal, adding_first_pos, new WidgetChooser(nullptr));
		return true;
	}
	
	if (has_one_to_rem && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		if (first_hovered) {
			children[0]->request_close([&](Widget* to_remove){
				Widget::close_callback(to_remove);
				
				if (auto sub = dynamic_cast<PanelHolder*>(children[0])){
					int our_indx = App::GetWidgetIndexInParent(this);
					
					App::MoveWidget(sub, parent);
					App::RemoveWidgetFromParent(this);
					
					if (our_indx == 0) {
						std::swap(sub->parent->children[0], sub->parent->children[1]);
					}
					
					delete this;
				}
			});
		}else {
			children[1]->request_close([&](Widget* to_remove){
				Widget::close_callback(to_remove);
				
				if (auto sub = dynamic_cast<PanelHolder*>(children[0])){
					int our_indx = App::GetWidgetIndexInParent(this);
					
					App::MoveWidget(sub, parent);
					App::RemoveWidgetFromParent(this);
					
					if (our_indx == 0) {
						std::swap(sub->parent->children[0], sub->parent->children[1]);
					}
					
					delete this;
				}
			});
		}
		
		App::nada_panel();
		return true;
	}
	
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && hastwo) {
		if (mx > xpos_h - width_h/2 && mx < xpos_h + width_h/2 && my > ypos_h-height_h/2 && my < ypos_h+height_h/2){
			dragging_handle = true;
		}
	}else if (dragging_handle && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE){
		dragging_handle = false;
	}
	
	for (auto child : children) {
		if (child->on_mouse_button_event(button, action, mods)) {
			return true;
		}
	}
	
	return false;
}

bool PanelHolder::on_mouse_move_event() {
	if (dragging_handle) {
		if (is_horizontal) {
			ratio = (App::mouseX-t_x)/(float)t_w;
		}else{
			ratio = (App::mouseY-t_y)/(float)t_h;
		}
		
		if (ratio < 0.025) {
			ratio = 0.025;
		}else if (ratio > 0.975) {
			ratio = 0.975;
		}
	}
	
	Widget::on_mouse_move_event();
	return false;
}

void PanelHolder::render() {
	Widget::render();

	if (has_one_to_add) {
		App::DrawRect(x_nb, y_nb, w_nb, h_nb, MakeColor(0.2f, 1.0f, 0.2f, 0.25f));
	}if (has_one_to_rem) {
		App::DrawRect(x_nb, y_nb, w_nb, h_nb, MakeColor(1.0f, 0.2f, 0.2f, 0.25f));
	}
	
	// draw the handles
	if (hastwo) {
		if (is_horizontal) {
			App::DrawRect(xpos_h-2, t_y, 4, t_h, App::theme.main_background_color);
		}else{
			App::DrawRect(t_x, ypos_h-2, t_w, 4, App::theme.main_background_color);
		}
		
		App::DrawRoundedRect(xpos_h-width_h/2, ypos_h-height_h/2, width_h, height_h, 5, App::theme.lesser_text_color);
	}
}

void PanelHolder::setState(nlohmann::json state) {
	for (auto c : state["children"]) {
		if (c.contains("children")) {
			auto ph = new PanelHolder(this);
			ph->setState(c);
		}else if (c == "FileTree"){
			new FileTree(this);
		}else if (c == "Settings"){
			new Settings(this);
		}else if (c == "WidgetChooser"){
			new WidgetChooser(this);
		}else { // any unknown, or editor
			new Editor(this);
		}
	}
	
	ratio = state["ratio"];
	is_horizontal = state["horizontal"];
}

nlohmann::json PanelHolder::saveConfiguration() {
	nlohmann::json thisitm = nlohmann::json::object();
	
	for (int i = 0; i < children.size(); i++) {
		auto c = children[i];
		
		if (auto pe = dynamic_cast<PanelHolder*>(c)){
			thisitm["children"][i] = pe->saveConfiguration();
		}else if (auto pe = dynamic_cast<Editor*>(c)){
			thisitm["children"][i] = "Editor";
		}else if (auto pe = dynamic_cast<FileTree*>(c)){
			thisitm["children"][i] = "FileTree";
		}else if (auto pe = dynamic_cast<Settings*>(c)){
			thisitm["children"][i] = "Settings";
		}else if (auto pe = dynamic_cast<WidgetChooser*>(c)){
			thisitm["children"][i] = "WidgetChooser";
		}else {
			thisitm["children"][i] = "Ehhhhh";
		}
		
		thisitm["ratio"] = ratio;
		thisitm["horizontal"] = is_horizontal;
	}
	
	return thisitm;
}
