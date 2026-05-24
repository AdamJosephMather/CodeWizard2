#include "asteroids.h"
#include "text_renderer.h"
#include "application.h"
#include <random>
#include <chrono>
#include <cmath>

#ifndef M_PI
const double M_PI = 3.141592653589793238;
#endif

long double getTime() {
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration<long double>(duration).count();
}

Asteroids::Asteroids(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Asteroids");
	
	std::random_device rd;  // Hardware seed
	gen = std::mt19937(rd()); // Standard mersenne_twister_engine
	distrib = std::uniform_int_distribution<>(1, 100);
	
	lastTime = getTime();
	lastBullet = getTime();
	
	startGameButton = new Button(this, icu::UnicodeString::fromUTF8("Play"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2-w/2;
		btn->t_y = t_y+t_h/2-h/2;
	},  [&](Button* btn){
		// onclick
		App::setActiveLeafNode(this);
		resetGame();
	});
	startGameButton->border = true;
	startGameButton->rounded = true;
}

void Asteroids::position(int winx, int winy, int winw, int winh) {
	Widget::position(winx, winy, winw, winh);
	
	auto curTime = getTime();
	auto deltaTime = curTime - lastTime;
	lastTime = curTime;
	
	for (int i = asteroids.size()-1; i >= 0; i--) {
		Asteroid *a = &asteroids[i];
		
		int truewidth = a->size * TextRenderer::get_text_height();
		
		a->x += ((double)(a->v_x)/(double)(t_w)) * deltaTime;
		a->y += ((double)(a->v_y)/(double)(t_h)) * deltaTime;
		
		if (a->x < 0) {
			a->x += 1;
		}else if (a->x > 1) {
			a->x -= 1;
		}
		
		if (a->y < 0) {
			a->y += 1;
		}else if (a->y > 1) {
			a->y -= 1;
		}
		
		a->rel_screen_x = a->x * t_w;
		a->rel_screen_y = a->y * t_h;
		
		a->r = std::fmod(a->r + a->v_r * deltaTime, 2 * M_PI);
		
		if (a->r < 0) {
			a->r += 2 * M_PI;
		}
		
		a->rel_screen_s = truewidth;
	}
	
	App::time_till_regular = 2;
	
	if (!playing) {
		return;
	}
	
	if (glfwGetKey(App::window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_LEFT) == GLFW_PRESS) {
		shipr -= 4 * deltaTime;
	}
	
	if (glfwGetKey(App::window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
		shipr += 4 * deltaTime;
	}
	
	accelerating = false;
	
	if (glfwGetKey(App::window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_UP) == GLFW_PRESS) {
		auto magnitude = TextRenderer::get_text_height() * 23 * deltaTime;
		shipv_x += magnitude * std::cos(shipr);
		shipv_y += magnitude * std::sin(shipr);
		accelerating = true;
	}
	
	auto magnitude = std::sqrt(shipv_x*shipv_x + shipv_y*shipv_y);
	if (magnitude != 0) { // this slows it down no matter what.
		shipv_x -= (shipv_x/magnitude)*0.4;
		shipv_y -= (shipv_y/magnitude)*0.4;
	}
	
	auto maxspeed = TextRenderer::get_text_height() * 80;
	if (magnitude > maxspeed) { // max value
		shipv_x *= maxspeed/magnitude;
		shipv_y *= maxspeed/magnitude;
	}
	
	shipx += (shipv_x * deltaTime) / t_w;
	shipy += (shipv_y * deltaTime) / t_h;
	
	if (shipx > 1) {
		shipx -= 1;
	}else if (shipx < 0) {
		shipx += 1;
	}
	
	if (shipy > 1) {
		shipy -= 1;
	}else if (shipy < 0) {
		shipy += 1;
	}
	
	const int ship_rad = TextRenderer::get_text_height() * 0.8;
	
	double shipw_over2 = TextRenderer::get_text_height();
	
	int shipx_t_w = shipx*t_w + shipw_over2;
	int shipy_t_h = shipy*t_h + shipw_over2;
	
	if (bullets.size() < 10 && glfwGetKey(App::window, GLFW_KEY_SPACE) == GLFW_PRESS && curTime-lastBullet > 0.5) {
		auto speed = (double)TextRenderer::get_text_height()*110;
		double shipw_over2 = TextRenderer::get_text_height();
		
		bullets.push_back({
			shipx + shipw_over2/t_w,
			shipy + shipw_over2/t_h,
			std::cos(shipr)*speed,
			std::sin(shipr)*speed,
		});
		
		App::displayText(icu::UnicodeString::fromUTF8("FIRE!"));
		
		lastBullet = curTime;
	}
	
	for (int j = bullets.size()-1; j >= 0; j--) {
		Bullet *b = &bullets[j];
		b->timeleft -= deltaTime;
		
		if (b->timeleft < 0) {
			bullets.erase(bullets.begin() + j);
			break;
		}else{
			b->x += (b->vx*deltaTime)/t_w;
			b->y += (b->vy*deltaTime)/t_h;
			
			if (b->x > 1) {
				b->x -= 1;
			}else if (b->x < 0) {
				b->x += 1;
			}
			
			if (b->y > 1) {
				b->y -= 1;
			}else if (b->y < 0) {
				b->y += 1;
			}
		}
	}
	
	for (int i = asteroids.size()-1; i >= 0; i--) {
		Asteroid *a = &asteroids[i];
		
		auto dist = std::pow(a->rel_screen_x-shipx_t_w, 2) + std::pow(a->rel_screen_y-shipy_t_h, 2);
		auto astDst = std::pow(a->rel_screen_s/2 + ship_rad, 2);
		
		if (dist < astDst) {
			playing = false;
			App::MoveWidget(startGameButton, this);
			break;
		}
		
		
		for (int j = bullets.size()-1; j >= 0; j--) {
			Bullet *b = &bullets[j];
			
			auto dist = std::pow(a->rel_screen_x-b->x*t_w, 2) + std::pow(a->rel_screen_y-b->y*t_h, 2);
			auto astDst = std::pow(a->rel_screen_s/2 + TextRenderer::get_text_height()/2, 2); // add some leeway
			
			if (dist < astDst) {
				if (a->size > 2) {
					double ax = a->x, ay = a->y;
					int asize = a->size;
					for (int k = 0; k < 3; k++) {
						Asteroid ast = {
							(distrib(gen)-50)/3000.0 + ax, (distrib(gen)-50)/3000.0 + ay, distrib(gen)/(100.0 / (2 * M_PI)),
							((distrib(gen)+100)/300.0) * asize,
							(int)(distrib(gen)/30),
							((distrib(gen)-50)/8.0) * (double)TextRenderer::get_text_height(), // this is pixels/second
							((distrib(gen)-50)/8.0) * (double)TextRenderer::get_text_height(),
							(distrib(gen)-50)/(50.0 / (1.4 * M_PI)) // rads/second
						};
						asteroids.push_back(ast);
					}
				}
				
				asteroids.erase(asteroids.begin() + i);
				bullets.erase(bullets.begin() + j);
				
				if (asteroids.empty()) {
					playing = false;
					App::MoveWidget(startGameButton, this);
				}
				
				break;
			}
		}
	}
}

void Asteroids::render() {
	Color* asteroid_color = App::theme.main_text_color;
	if (!playing) {
		asteroid_color = App::theme.hover_background_color;
	}
	
	for (Asteroid a : asteroids) {
		int x = t_x + a.rel_screen_x-a.rel_screen_s/2;
		int y = t_y + a.rel_screen_y-a.rel_screen_s/2;
		
		App::DrawAsteroid(x, y, a.rel_screen_s, a.r, asteroid_color, a.type);
		
		bool drawitx = false;
		int newx = x;
		if (x+a.rel_screen_s > t_x + t_w) {
			newx -= t_w;
			drawitx = true;
		}else if (x < t_x) {
			newx += t_w;
			drawitx = true;
		}
		if (drawitx) {
			App::DrawAsteroid(newx, y, a.rel_screen_s, a.r, asteroid_color, a.type);
		}
		
		bool drawity = false;
		int newy = y;
		if (y+a.rel_screen_s > t_y + t_h) {
			newy -= t_h;
			drawity = true;
		}else if (y < t_y) {
			newy += t_h;
			drawity = true;
		}
		
		if (drawity) {
			App::DrawAsteroid(x, newy, a.rel_screen_s, a.r, asteroid_color, a.type);
		}
		
		if (drawity && drawitx) {
			App::DrawAsteroid(newx, newy, a.rel_screen_s, a.r, asteroid_color, a.type);
		}
	}
	
	if (playing) {
		int shipw = TextRenderer::get_text_height()*2;
		int x = shipx*t_w + t_x;
		int y = shipy*t_h + t_y;
		
		App::DrawShip(x, y, shipw, shipr, App::theme.main_text_color, accelerating);
		
		
		bool drawitx = false;
		int newx = x;
		if (x+shipw > t_x + t_w) {
			newx -= t_w;
			drawitx = true;
		}else if (x < t_x) {
			newx += t_w;
			drawitx = true;
		}
		if (drawitx) {
			App::DrawShip(newx, y, shipw, shipr, App::theme.main_text_color, accelerating);
		}
		
		bool drawity = false;
		int newy = y;
		if (y+shipw > t_y + t_h) {
			newy -= t_h;
			drawity = true;
		}else if (y < t_y) {
			newy += t_h;
			drawity = true;
		}
		
		if (drawity) {
			App::DrawShip(x, newy, shipw, shipr, App::theme.main_text_color, accelerating);
		}
		
		if (drawity && drawitx) {
			App::DrawShip(newx, newy, shipw, shipr, App::theme.main_text_color, accelerating);
		}
		
		
		int bulletr = TextRenderer::get_text_width(1) * 0.2;
		
		for (Bullet b : bullets) {
			App::DrawCircle(b.x * t_w + t_x, b.y * t_h + t_y, bulletr, 8, App::theme.main_text_color);
		}
	}
	
	Widget::render();
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
}

void Asteroids::resetGame() {
	if (startGameButton->parent == this) {
		App::RemoveWidgetFromParent(startGameButton);
	}
	
	playing = true;
	
	lastTime = getTime();
	
	asteroids.clear();
	
	const int ASTEROID_CONSTANT = 2000; // textwidth^2 / asteroid roughly
	int asteroid_count = (t_w * t_h) / (TextRenderer::get_text_width(1)*TextRenderer::get_text_width(1)) / ASTEROID_CONSTANT; // this is an area that has been normalized based on scaling (text size)
	
	for (int i = 0; i < asteroid_count; i++) {
		Asteroid ast = {
			distrib(gen)/100.0, distrib(gen)/100.0, distrib(gen)/(100.0 / (2 * M_PI)),
			(distrib(gen)+100)/60.0,
			(int)(distrib(gen)/30),
			((distrib(gen)-50)/6.0) * (double)TextRenderer::get_text_height(), // this is pixels/second
			((distrib(gen)-50)/6.0) * (double)TextRenderer::get_text_height(),
			(distrib(gen)-50)/(50.0 / (1.4 * M_PI)) // rads/second
		};
		
		while (std::abs(ast.x - 0.5) < 0.1 && std::abs(ast.y - 0.5) < 0.1) {
			ast.x = distrib(gen)/100.0;
			ast.y = distrib(gen)/100.0;
		}
		
		asteroids.push_back(ast);
	}
	
	shipx = 0.5;
	shipy = 0.5;
	shipv_x = 0;
	shipv_y = 0;
	shipr = 0;
	
	bullets.clear();
}