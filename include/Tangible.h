#pragma once

#include "TuioObject.h"

using namespace ci;
using namespace std;

namespace SecondStudy {

class Tangible : public std::enable_shared_from_this<Tangible> {
	pair<int, int> _size;

public:
	tuio::Object object;
	bool isOn;
	bool isVisible;
	Rectf icon;
	Rectf board;
	
	list<list<Vec2f>> strokes;
	mutex strokesMutex;

	vector<vector<bool>> notes;
	mutex notesMutex;

	Tangible(void) {
		isOn = false;
		isVisible = true;

		icon = Rectf(Vec2f(30.0f, -15.0f), Vec2f(60.0f, 15.0f));
		board = Rectf(Vec2f(30.0f, -50.0f), Vec2f(190.0f, 50.0f));

		_size = pair<int, int>(8,5); // 8 notes, 5 pitches
		notes = vector<vector<bool>>(_size.first, vector<bool>(_size.second, false));
	}

	~Tangible(void) {

	}

	void toggle(pair<int, int> note) {
		if(note.first >= 0 && note.first < _size.first && note.second >= 0 && note.second < _size.second) {
			notes[note.first][note.second] = !notes[note.first][note.second];
		}
	}

	pair<int, int>& size() { return _size; }
};

}