#include "toast.h"
#include "application.h"
#include "text_renderer.h"

Toast::Toast(Widget *parent) : Widget(parent) {
	id = MST::toMonoString("Toast");
}

void Toast::position(int x, int y, int width, int height) {
	const int pad = 10;
	t_w = width/4;
	t_h = fmax(height/12, TextRenderer::get_text_height()*3);
	t_x = x+width-t_w-pad;
	t_y = y+pad;
	
	if (time != -1) {
		if (glfwGetTime()-time > 4) {
			time = -1;
			App::time_till_regular = 2;
			App::reclear = 3;
		}
	}
}

void Toast::render() {
	if (!is_visible) {
		return;
	}
	
	const int padding = App::text_padding;
	auto drawToast = [&](int x, int width) {
		const float radius = App::themeCornerRadius(width, t_h);
		App::runWithRoundedClip(x, t_y, width, t_h, radius, [&]() {
			if (radius > 0.0f) {
				App::DrawRoundedRect(x, t_y, width, t_h, radius, App::theme.darker_background_color);
				App::DrawRoundBorder(x, t_y, width, t_h, App::theme.border, 5, radius);
			} else {
				App::DrawRect(x, t_y, width, t_h, App::theme.darker_background_color);
				App::DrawBorder(x, t_y, width, t_h, App::theme.border);
			}
			TextRenderer::draw_text(x+padding, t_y+t_h/2-TextRenderer::get_text_height()/2, text, App::theme.main_text_color);
		});
	};
	
	if (time != -1) {
		if (displayOffset != 0) {
			App::time_till_regular = 2;
			App::reclear = 3;
		}
		
		displayOffset *= (1.0 - 0.1 * App::settings->getValue("anim_speed", 1.0f));
		
		if (displayOffset <= 1) {
			displayOffset = 0;
		}
		
		int x_disp = t_x+displayOffset;
		int w_disp = t_w-displayOffset;
		
		drawToast(x_disp, w_disp);
	}else if (displayOffset >= 0) {
		App::time_till_regular = 2;
		App::reclear = 3;
		
		displayOffset += ((float)t_w-displayOffset)*.1*App::settings->getValue("anim_speed", 1.0f);
		
		int x_disp = t_x+displayOffset;
		int w_disp = t_w-displayOffset;
		
		drawToast(x_disp, w_disp);
		
		if (t_w-displayOffset <= 1.0f) {
			displayOffset = -1;
		}
	}
	
	Widget::render();
}

void Toast::displayMessage(MST::MonoString displ_text) {
	text = displ_text;
	time = glfwGetTime();
	displayOffset = t_w;
	closingOpacity = 1.0;
	App::time_till_regular = 2;
	App::reclear = 3;
}