#pragma once

#include "TuioCursor.h"
#include "cinder/app/AppNative.h"

class TouchPoint : public ci::tuio::Cursor {
public:
	double timestamp;
	
	TouchPoint(void) {
		timestamp = ci::app::getElapsedSeconds();
	}
	
	TouchPoint(const ci::tuio::Cursor& c) : ci::tuio::Cursor(c) {
		timestamp = ci::app::getElapsedSeconds();
	}

	~TouchPoint(void) {
	}
};

