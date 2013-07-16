#pragma once

#include <list>
#include "TouchPoint.h"

namespace SecondStudy {

class TouchTrace {
	int _lifespan;

public:
	enum class State {
		TOUCH_DOWN,
		TOUCH_MOVING,
		TOUCH_STILL,
		TOUCH_UP
	} state;

	std::list<TouchPoint> touchPoints;

	bool isVisible;

	TouchTrace(void) {
		state = State::TOUCH_DOWN;
		isVisible = true;

		_lifespan = 10;
	}

	~TouchTrace(void) {
		touchPoints.clear();
	}

	bool isDead() {
		return (--_lifespan) == 0;
	}

	// TODO State info should be added to the cursors
	void addCursorDown(TouchPoint p) {
		touchPoints.push_back(p);
		state = State::TOUCH_DOWN;
	}

	void cursorMove(TouchPoint p) {
		touchPoints.push_back(p);
		if(p.getSpeed().length() == 0) {
			state = State::TOUCH_STILL;
		} else {
			state = State::TOUCH_MOVING;
		}
	}

	void addCursorUp(TouchPoint p) {
		touchPoints.push_back(p);
		state = State::TOUCH_UP;
	}
};

}