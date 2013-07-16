#pragma once

#include "cinder/Vector.h"
#include "TouchTrace.h"
#include "Gesture.h"

namespace SecondStudy {
	
	class StrokeGesture : public Gesture {
	public:
		TouchTrace trace;
		
		StrokeGesture() { }
		StrokeGesture(shared_ptr<TouchTrace> t) : trace(*t) { }
		
		~StrokeGesture() { }
	};
}