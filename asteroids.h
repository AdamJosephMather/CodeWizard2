#pragma once

#include <random>
#include "button.h"
#include "widget.h"

struct Asteroid {
	double x     = 0;
	double y     = 0;
	double r     = 0;
	double size  = 0;
	int    type  = 0;
	double v_x   = 0;
	double v_y   = 0;
	double v_r   = 0;
	
	int rel_screen_x = 0;
	int rel_screen_y = 0;
	int rel_screen_s = 0;
};

struct Bullet {
	double x;
	double y;
	double vx;
	double vy;
	double timeleft = 1;
};

class Asteroids : public Widget {
public:
	Asteroids(Widget* parent);
	
	bool rounded = true;
	
//	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_char_event(unsigned int keycode);
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
//	void executeAction(WidgetActionType typ);
	
	void position(int x, int y, int w, int h);
	void render();
	
	void resetGame();
private:
	std::vector<Asteroid> asteroids = {};
	
	Button* startGameButton;
	long double lastTime;
	long double lastBullet = -1;
	
	bool playing = false;
	
	double shipx = 0.5;
	double shipy = 0.5;
	double shipv_x = 0;
	double shipv_y = 0;
	double shipr = 0;
	bool accelerating = false;
	
	std::vector<Bullet> bullets = {};
	
	std::mt19937 gen;
	std::uniform_int_distribution<> distrib;
};