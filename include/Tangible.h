#pragma once

#include "TuioObject.h"

using namespace ci;
using namespace std;

namespace SecondStudy {

class Tangible : public std::enable_shared_from_this<Tangible> {
public:
	tuio::Object object;
	bool isOn;
	bool isVisible;
	Rectf icon;
	Rectf board;
	
	list<list<Vec2f>> strokes;
	std::mutex strokesMutex;

	shared_ptr<Tangible> next;
	shared_ptr<Tangible> prev;

	Tangible(void) {
		isOn = false;
		isVisible = true;

		icon = Rectf(Vec2f(30.0f, -15.0f), Vec2f(60.0f, 15.0f));
		board = Rectf(Vec2f(30.0f, -60.0f), Vec2f(180.0f, 60.0f));

		next = nullptr;
		prev = nullptr;

		/*auto _this = shared_ptr<Tangible>(this).get();
		app::console() << _this << " : " << this << endl*/;
	}

	~Tangible(void) {

	}

	void setNext(shared_ptr<Tangible> o) {
		if(o == nullptr) {
			if(next != nullptr) {
				next->prev = nullptr;
			}
			next = nullptr;
		} else {
			o->next = next;
			next = o;
			o->prev = shared_from_this();
			if(o->next != nullptr) {
				o->next->prev = o;
			}
		}
	}

	void setPrev(shared_ptr<Tangible> o) {

	}
};

}