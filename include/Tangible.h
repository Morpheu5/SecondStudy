#pragma once

#include "TuioObject.h"
#include "cinder/app/AppNative.h"
#include "cinder/Timeline.h"
#include "OscSender.h"
#include "OscMessage.h"

using namespace ci;
using namespace std;

namespace SecondStudy {

class Tangible : public std::enable_shared_from_this<Tangible> {
	pair<int, int> _size;
	//int _currentNote;
	vector<int> _midiNotes;
	CueRef _cue;
	shared_ptr<osc::Sender> _sender;
	TimelineRef _timeline;

	void _play(int currentNote) {
		app::console() << "Playing note " << currentNote << " of tangible " << object.getFiducialId() << endl;
		app::console() << "cursor at (" << cursorOffset.value().x << ", " << cursorOffset.value().y << ")" << endl;

		for(int i = 0; i < _size.second; i++) {
			if(notes[currentNote][i]) {
				osc::Message m;
				m.setAddress("/playnote");
				m.addIntArg(_midiNotes[i]);
				_sender->sendMessage(m);
			}
		}
	}	

public:
	tuio::Object object;
	bool isOn;
	bool isVisible;
	double timeRemoved;
	Rectf icon;
	Rectf board;
	Rectf closeIcon;
	Rectf playIcon;
	Rectf cursor;
	Anim<Vec2f> cursorOffset;
	
	list<list<Vec2f>> strokes;
	mutex strokesMutex;

	vector<vector<bool>> notes;
	mutex notesMutex;

	Tangible(void) {
		_timeline = Timeline::create();
		_size = pair<int, int>(8,5); // 8 notes, 5 pitches
		notes = vector<vector<bool>>(_size.first, vector<bool>(_size.second, false));

		isOn = false;
		isVisible = true;
		timeRemoved = app::getElapsedSeconds();
		app::console() << "Creating object at " << timeRemoved << " seconds" << endl;

		icon = Rectf(Vec2f(30.0f, -15.0f), Vec2f(60.0f, 15.0f));
		board = Rectf(Vec2f(30.0f, -50.0f), Vec2f(190.0f, 50.0f));
		closeIcon = Rectf(Vec2f(200.0f, -50.0f), Vec2f(220.0f, -30.0f));
		playIcon = Rectf(Vec2f(200.0f, -20.0f), Vec2f(220.0f, 0.0f));
		cursor = Rectf(Vec2f(30.0f, 50.0f), Vec2f(30.0f + (board.getWidth() / _size.first), 55.0f));

		_sender = nullptr;

		// C major pentatonic
		_midiNotes.push_back(69);
		_midiNotes.push_back(67);
		_midiNotes.push_back(64);
		_midiNotes.push_back(62);
		_midiNotes.push_back(60);
	}

	~Tangible(void) {

	}

	void toggle(pair<int, int> note) {
		if(note.first >= 0 && note.first < _size.first && note.second >= 0 && note.second < _size.second) {
			if(notes[note.first][note.second]) {
				notes[note.first][note.second] = false;
			} else {
				for(auto &n : notes[note.first]) {
					n = false;
				}
				notes[note.first][note.second] = !notes[note.first][note.second];
			}
		}
	}

	pair<int, int>& size() { return _size; }

	void sender(shared_ptr<osc::Sender> sender) { _sender = sender; }

	void play(float noteLength) {
		app::timeline().apply(&cursorOffset, Vec2f(0.0f, 0.0f), 0);
		app::timeline().appendTo(&cursorOffset, Vec2f(board.getWidth() * (1.0f - 1.0f/_size.first), 0.0f), noteLength*(_size.first));
		app::timeline().appendTo(&cursorOffset, Vec2f(0.0f, 0.0f), noteLength, EaseInOutSine());

		for(int i = 0; i < _size.first; i++) {
			_cue = app::timeline().add( bind(&Tangible::_play, this, i), app::timeline().getCurrentTime() + noteLength*i); 
		}
		_cue->setAutoRemove(true);
		_cue->setLoop(false);
	}
};

}