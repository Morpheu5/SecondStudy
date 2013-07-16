#pragma once

#include "cinder/Vector.h"
#include "Gesture.h"

namespace SecondStudy {
	
	class TapGesture : public Gesture {
	public:
		ci::Vec2f position;
		
		TapGesture() { }
		TapGesture(ci::Vec2f p) : position(p) {
			
		}
		
		~TapGesture() { }
	};
}