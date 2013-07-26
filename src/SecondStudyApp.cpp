#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/params/Params.h"
#include "cinder/Timeline.h"

#include "TuioClient.h"
#include "TuioCursor.h"
#include "OscSender.h"
#include "OscListener.h"

#include "TouchTrace.h"
#include "Tangible.h"

#include "Gesture.h"
#include "TapGesture.h"
#include "StrokeGesture.h"

#define FPS 60

using namespace ci;
using namespace ci::app;
using namespace std;

namespace SecondStudy {

	class TheApp : public AppNative {
		float _zoom;
		float _scale;
		Vec2f _s, _o, _uo;
		params::InterfaceGl _params;
		
		tuio::Client _tuioClient;
		
		string _hostname;
		int _port;
		shared_ptr<osc::Sender> _sender;
		
		map<int, shared_ptr<Tangible>> _objects;
		map<int, shared_ptr<TouchTrace>> _traces;
		mutex _tracesMutex;

		list<shared_ptr<TouchTrace>> _finishedTraces;
		list<shared_ptr<Gesture>> _gestures;
		mutex _gesturesMutex;
		
		thread _gestureProcessor;
		bool _gestureProcessorShouldStop;

		list<list<shared_ptr<Tangible>>> _sequences;
		mutex _sequencesMutex;

		float _noteLength;
		int _currentNote;

		CueRef _cue;

	public:
		void setup();
		void shutdown();
		void update();
		void draw();
		void resize();
		void processGestures();
		void processTrace(shared_ptr<TouchTrace> t);
		
		void keyDown(KeyEvent event);
		void mouseDown(MouseEvent event);
		
		void cursorAdded(tuio::Cursor cursor);
		void cursorUpdated(tuio::Cursor cursor);
		void cursorRemoved(tuio::Cursor cursor);
		
		void objectAdded(tuio::Object object);
		void objectUpdated(tuio::Object object);
		void objectRemoved(tuio::Object object);

		Vec2f tuioToWorld(Vec2f p);
		//Vec2f worldToScreen(Vec2f p);
		//Vec2f tuioToScreen(Vec2f p) { return worldToScreen(tuioToWorld(p)); }

		vector<shared_ptr<Tangible>> getNeighbors(shared_ptr<Tangible> t);

		//void playTangible(int id);
	};

	void TheApp::setup() {
		_params = params::InterfaceGl("Parameters", Vec2i(200,250));
		_zoom = 1.0f;
		_params.addParam("Zoom", &_zoom, "min=0.1 max=1.0 step=0.001 precision=3");
		
		_tuioClient.registerCursorAdded(this, &TheApp::cursorAdded);
		_tuioClient.registerCursorUpdated(this, &TheApp::cursorUpdated);
		_tuioClient.registerCursorRemoved(this, &TheApp::cursorRemoved);
		
		_tuioClient.registerObjectAdded(this, &TheApp::objectAdded);
		_tuioClient.registerObjectUpdated(this, &TheApp::objectUpdated);
		_tuioClient.registerObjectRemoved(this, &TheApp::objectRemoved);
		
		_tuioClient.connect(); // Defaults to UDP:3333
		
		setFrameRate(FPS);
		setWindowSize(640, 480);
		_scale = getWindowHeight() / 480.0f;
		float w = getWindowWidth();
		float h = getWindowHeight();
		float H = h/0.75f;
		_s = Vec2f(H, h);
		_o = Vec2f((w - H)/2.0f, 0.0f);
		_uo = Vec2f(0.0f, 0.0f);

		_gestureProcessorShouldStop = false;
		_gestureProcessor = thread(bind(&TheApp::processGestures, this));

		_noteLength = 0.25f;
		_currentNote = 0;

		_sender = make_shared<osc::Sender>();
		_sender->setup("localhost", 3000);
	}

	void TheApp::shutdown() {
		_gestureProcessorShouldStop = true;
		_gestureProcessor.join();
	}
	
	void TheApp::update() {
		_sequencesMutex.lock();
		_sequences.remove_if( [](list<shared_ptr<Tangible>> l) {
			return l.empty();
		});
		_sequencesMutex.unlock();

		_tracesMutex.lock();
		for(auto i = _traces.begin(); i != _traces.end(); ) {
			if(!i->second->isVisible && i->second->isDead()) {
				_finishedTraces.push_back(i->second);
				i = _traces.erase(i);
			} else {
				++i;
			}
		}
		_tracesMutex.unlock();

		if(_finishedTraces.size() > 0) {
			processTrace(_finishedTraces.front());
			_finishedTraces.pop_front();
		}

		for(auto& o : _objects) {
			shared_ptr<Tangible> t = o.second;
			_sequencesMutex.lock();
			for(auto& s : _sequences) {
				s.remove_if( [this](shared_ptr<Tangible> u) {
					return !u->isVisible && (getElapsedSeconds() - u->timeRemoved) > 1.0f;
				});
			}
			_sequencesMutex.unlock();
		}
	}

	void TheApp::draw() {
		gl::clear(Color(0, 0, 0));
		
		_scale = getWindowHeight() / 480.0f;

		Vec2f _do = _o + _uo;

		_sequencesMutex.lock();
		for(auto& s : _sequences) {
			if(s.size() > 1) {
				for(auto it = s.begin(); it != prev(s.end()); ++it) {
					shared_ptr<Tangible> a = *it;
					shared_ptr<Tangible> b = *(next(it));
					Vec2f ap = a->object.getPos() * _s + _do;
					Vec2f bp = b->object.getPos() * _s + _do;
					float w = 1.0f / log(1 + ap.distance(bp) / 100.0f);
					gl::color(w, w, w, 1.0f); // doubtfully useful on a proper video card...
					Vec2f d(bp - ap);
					d.normalize();
					gl::drawVector(Vec3f(ap), Vec3f(ap + d), ap.distance(bp), _scale * w * 5.0f);
				}
			}
		}
		_sequencesMutex.unlock();
		gl::color(1,1,1,1);
		
		// Draw the objects
		for(auto object : _objects) {
			shared_ptr<Tangible> t = object.second;
			if(!t->isVisible) {
				continue;
			}

			// PUSH MODEL VIEW
			gl::pushModelView();

			Matrix44f transform;
			transform.translate(Vec3f((t->object.getPos()*_s)+_do));
			transform.rotate(Vec3f(0.0f, 0.0f, t->object.getAngle()));
			gl::multModelView(transform);

			/*
			_sequencesMutex.lock();
			for(auto& seq : _sequences) {
				if(t == seq.front()) {
					gl::color(0,1,0,1);
					break;
				} else if(t == seq.back()) {
					gl::color(0,0,1,1);
					break;
				} else {
					gl::color(1,1,1,1);
					//break;
				}
			}
			_sequencesMutex.unlock();
			*/
			gl::drawSolidRect(Rectf(-20.0f*_scale, -20.0f*_scale, 20.0f*_scale, 20.0f*_scale));

			gl::color(0.2f, 0.2f, 0.2f, 1.0f);
			gl::drawSolidCircle(Vec2f(0,0), 50.0f*_scale);
			gl::color(1,1,1,1);

			Rectf board = t->isOn ? t->board : t->icon;

			t->notesMutex.lock();
			pair<int, int> size = t->size();
			ColorAf off(0.25f, 0.25f, 0.25f, 1.0f);
			ColorAf on(0.5f, 0.5f, 0.5f, 1.0f);
			for(int row = 0; row < size.first; row++) {
				for(int col = 0; col < size.second; col++) {
					if(t->notes[row][col]) {
						gl::color(on);
					} else {
						gl::color(off);
					}

					Vec2f noteRectSize(board.getSize() / Vec2f(size.first, size.second));
					Rectf noteRect(Vec2f(0.0f, 0.0f), noteRectSize);
					gl::drawSolidRect((noteRect + noteRectSize*Vec2f(row, col) + board.getUpperLeft()) * _scale);
					if(t->isOn) {
						gl::color(on * 1.25f);
						gl::drawStrokedRect((noteRect + noteRectSize*Vec2f(row, col) + board.getUpperLeft()) * _scale);
					}

					// Draw icons
					if(t->isOn) {
						// Draw close icon
						Rectf closeIcon = t->closeIcon * _scale;
						gl::drawStrokedRect(closeIcon);
						gl::lineWidth(2.0f * _scale);
						gl::drawLine(closeIcon.getUpperLeft() + Vec2f(5.0f, 5.0f)*_scale, closeIcon.getLowerRight() + Vec2f(-5.0f, -5.0f)*_scale);
						gl::drawLine(closeIcon.getUpperRight() + Vec2f(-5.0f, 5.0f)*_scale, closeIcon.getLowerLeft() + Vec2f(5.0f, -5.0f)*_scale);
						gl::lineWidth(1.0f * _scale);

						Rectf playIcon = t->playIcon * _scale;
						gl::drawStrokedRect(playIcon);
						gl::drawSolidTriangle(playIcon.getUpperLeft() + Vec2f(5.0f, 5.0f)*_scale, playIcon.getLowerLeft() + Vec2f(5.0f, -5.0f)*_scale, playIcon.getCenter() + Vec2f(5.0f, 0.0f) * _scale);

						Rectf cursor = (t->cursor + t->cursorOffset.value()) * _scale;
						gl::drawSolidRect(cursor);
					}
				}
			}
			t->notesMutex.unlock();

			gl::color(1,1,1,1);

			gl::popModelView();
			// POP MODEL VIEW
		}
		
		// Draws traces as they go
		_tracesMutex.lock();
		for(auto trace : _traces) {
			auto touchPoints = trace.second->touchPoints;
			
			std::vector<Vec2f> v;
			for(TouchPoint p : touchPoints) {
				v.push_back(p.getPos());
			}
			if(v.size() > 2) {
				BSpline2f l(v, min((int)v.size()-1, 3), false, true);
				PolyLine2f pl;
				for(int i = 0; i < v.size(); i++) {
					float t = (float)i/(float)v.size();
					pl.push_back((l.getPosition(t)*_s)+_do);
				}
				glLineWidth(2.0f * _scale);
				gl::draw(pl);
				glLineWidth(1.0f * _scale);
			}
			
			TouchPoint p = touchPoints.back();
			gl::drawSolidCircle((p.getPos()*_s)+_do , _zoom * _scale * 2.0f);
		}
		_tracesMutex.unlock();
		
		//_params.draw();

		glLineWidth(1.0f * _scale);

		gl::color(1,1,1,1);
	}
	
	void TheApp::resize() {
		float w = getWindowWidth();
		float h = getWindowHeight();
		_s.y = h;
		_s.x = _s.y / 0.75f;
		_o.x = (w-_s.x)/2.0f;
		_o.y = 0.0f;
	}

	void TheApp::processGestures() {
		// TODO busy waiting is for whimps. Counting semaphore or signal queue, maybe?
		Vec2f _do;
		while(true) {
			_do = _o + _uo;
			if(_gestureProcessorShouldStop) {
				return;
			}
			if(!_gestures.empty()) {
				_gesturesMutex.lock();
				shared_ptr<Gesture> g = _gestures.front();
				_gestures.pop_front();
				_gesturesMutex.unlock();

				if(dynamic_pointer_cast<TapGesture>(g) != nullptr) {
					shared_ptr<TapGesture> tap = dynamic_pointer_cast<TapGesture>(g);
					Vec3f p = Vec3f(tap->position.x, tap->position.y, 0.0f);
					// See if the tap has happened inside one of the objects boxes.
					for(auto object : _objects) {
						shared_ptr<Tangible> t = object.second;
						Matrix44f transform;
						transform.translate(Vec3f(t->object.getPos()*_s+_do));
						transform.rotate(Vec3f(0.0f, 0.0f, t->object.getAngle()));
						Vec3f tp3 = transform.inverted().transformPoint(p);
						// Let's see if the tap hit a box
						Vec2f tp = Vec2f(tp3.x, tp3.y);
						if(t->isOn) {
							Rectf closeIcon = t->closeIcon * _scale;
							if(closeIcon.contains(tp)) {
								t->isOn = false;
							}
							Rectf playIcon = t->playIcon * _scale;
							if(playIcon.contains(tp)) {
								console() << "Let's play tangible " << t->object.getFiducialId() << endl;
								t->play(_noteLength);
							}
							Rectf board = t->board * _scale;
							if(board.contains(tp)) {
								Vec2f off = board.getCenter() - board.getSize()/2.0f;
								tp -= off;
								tp /= board.getSize();
								tp *= Vec2i(t->size().first, t->size().second);
								pair<int, int> n((int)tp.x, (int)tp.y);
								t->toggle(n);
								
							}
						}
						if(tp.length() < 50.0f * _scale && !t->isOn) {
							t->isOn = true;
						}
					}
				} else if(dynamic_pointer_cast<StrokeGesture>(g) != nullptr) {
					shared_ptr<StrokeGesture> stroke = dynamic_pointer_cast<StrokeGesture>(g);

					Vec3f front = Vec3f(stroke->trace.touchPoints.front().getPos() * Vec2f(getWindowSize()));
					Vec3f back = Vec3f(stroke->trace.touchPoints.back().getPos() * Vec2f(getWindowSize()));

					// safe-guard for preventing multiple stroke gestures to be recognized with the same trace
					bool gestureRecognized = false;
					for(auto object : _objects) {
						shared_ptr<Tangible> tangible = object.second;

						// MUSICAL STROKE
						if(tangible->isOn) {
							// Let's see if it's a musical stroke.
							// Check if both front() and back() are on the same active object's box.
							Rectf box = tangible->board*_scale;
							Matrix44f transform;
							transform.translate(Vec3f(tangible->object.getPos()*_s)+Vec3f(_do.x, _do.y, 0.0f));
							transform.rotate(Vec3f(0.0f, 0.0f, tangible->object.getAngle()));
							Vec3f tfront = transform.inverted().transformPoint(front);
							Vec3f tback = transform.inverted().transformPoint(back);
							if(box.contains(Vec2f(tfront.x, tfront.y)) && box.contains(Vec2f(tback.x, tback.y))) {
								// Rejoice in happiness, it's a musical stroke!
								gestureRecognized = true;

								list<Vec2f> transformedStroke;

								// Let's compute the transform that will normalise the stroke
								// to the unit box centered in (0.5, 0.5). Heh.
								Vec2f offset(0,0);
								//console() << tangible->board.getSize() << tangible->board.getCenter() << endl;
								offset += tangible->board.getCenter()/480.0f; // DA FLYIN' FUQ?
								offset.rotate(tangible->object.getAngle());
								offset *= Vec2f(0.75f, 1.0f);
								offset += tangible->object.getPos();
								
								vector<Vec2f> qs;
								for(auto p : stroke->trace.touchPoints) {
									Vec2f q(p.getPos());
									q -= offset;
									q /= Vec2f(0.75, 1.0f);
									q.rotate(-tangible->object.getAngle());
									q *= Vec2f(0.75, 1.0); // DA FUQ?
									qs.push_back(q);
								}

								vector<Vec2f> tqs;
								for(auto p : qs) {
									tqs.push_back(p * Vec2f(640.0f, 480.0f));
								}

								if(tqs.size() > 2) {
									BSpline2f l(tqs, min((int)tqs.size(), 3), false, true);
									float totalLength = l.getLength(0,1);
									float step = sqrt(totalLength);
									transformedStroke.push_back((l.getPosition(0.0f) / Vec2f(640.0f, 480.0f)) / ((tangible->board.getSize()/Vec2f(640.0f, 480.0f))));
									for(float p = 0.0f; p <= totalLength; p += step) {
										Vec2f lp(l.getPosition(l.getTime(p)));
										lp /= Vec2f(640.0f, 480.0f);
										transformedStroke.push_back(lp / (tangible->board.getSize()/Vec2f(640.0f, 480.0f)));
									}
									transformedStroke.push_back((l.getPosition(1.0f) / Vec2f(640.0f, 480.0f)) / ((tangible->board.getSize()/Vec2f(640.0f, 480.0f))));
								}

								tangible->strokesMutex.lock();
								tangible->strokes.push_back(transformedStroke);
								tangible->strokesMutex.unlock();

								pair<int, int> size = tangible->size();
								vector<int> notes(size.first, 1000);
								for(auto& p : transformedStroke) {
									Vec2i q(Vec2i(Vec2f(size.first, size.second) * (p + Vec2f(0.5f, 0.5f))));
									if(q.x > -1 && q.x < notes.size()) {
										notes[q.x] = min(notes[q.x], q.y);
									}
								}
								for(int i = 0; i < notes.size(); i++) {
									console() << i << ":" << notes[i] << endl;
									if(notes[i] < 1000) {
										tangible->toggle(pair<int, int>(i, notes[i]));
									}
								}
							}
						}
						if(gestureRecognized) {
							goto theMoon;
						}

						// CONNECTION STROKE
						for(auto other : _objects) {
							if(tangible != other.second && tangible->isVisible && other.second->isVisible) {
								shared_ptr<Tangible> otherTangible = other.second;
								Vec2f thisPos = tangible->object.getPos() * Vec2f(getWindowSize());
								Vec2f otherPos = otherTangible->object.getPos() * Vec2f(getWindowSize());
								if(thisPos.distance(Vec2f(front.x, front.y)) <= _scale*50.0f && otherPos.distance(Vec2f(back.x, back.y)) <= _scale*50.0f) {
									// We do have a legit connection stroke
									gestureRecognized = true;
									_sequencesMutex.lock();
									[&]() {
										for(auto sit = _sequences.begin(); sit != _sequences.end(); ++sit) {
											for(auto lit = sit->begin(); lit != sit->end(); ++lit) {
												if(*lit == tangible) {
													// Check if otherTangible is in the same sequence, just before lit
													for(auto i(lit); i != sit->begin(); --i) {
														if(*i == otherTangible) {
															return;
														}
													}
													// cover the head case because of reverse iterator madness
													if(*(sit->begin()) == otherTangible) {
														return;
													}
													if(*(sit->begin()) != otherTangible) { // prevents tail-head loops
														for(auto nsit = _sequences.begin(); nsit != _sequences.end(); ++nsit) {
															for(auto nlit = nsit->begin(); nlit != nsit->end(); ++nlit) {
																if(*nlit == otherTangible) {
																	nsit->splice(nlit, *sit, sit->begin(), next(lit));
																	return;
																}
															}
														}
													}
												}
											}
										}
									}();
									_sequencesMutex.unlock();
								}
							}
						}
						if(gestureRecognized) {
							goto theMoon;
						}

						// CUTTING STROKE
						_sequencesMutex.lock();
						[&, this]() {
							for(auto& s : _sequences) {
								if(s.size() > 1) {
									for(auto it = s.begin(); it != prev(s.end()); ++it) {
										shared_ptr<Tangible> at = *it;
										shared_ptr<Tangible> bt = *(next(it));
										Vec2f a = at->object.getPos() * _s + _do;
										Vec2f b = bt->object.getPos() * _s + _do;
										Vec2f c = Vec2f(front.x, front.y) + _uo;
										Vec2f d = Vec2f(back.x, back.y) + _uo;

										// If A1 o A2 are INF, then they are both vetical...
										float A1 = (a.y - b.y) / (a.x - b.x);
										float A2 = (c.y - d.y) / (c.x - d.x);
										float b1 = a.y - A1 * a.x;
										float b2 = c.y - A2 * c.x;

										if(abs(A1 - A2) > FLT_EPSILON) {
											float px = (b2 - b1) / (A1 - A2);
											Vec2f p(px, A1 * px + b1);

											// Now, to see if p is contained within both bounding boxes...
											Rectf ab(min(a.x, b.x), min(a.y, b.y), max(a.x, b.x), max(a.y, b.y));
											Rectf cd(min(c.x, d.x), min(c.y, d.y), max(c.x, d.x), max(c.y, d.y));
											if(ab.contains(p) && cd.contains(p)) {
												gestureRecognized = true;
												// Now, the connection goes from a to b, so b becomes a new sequence
												console() << at->object.getFiducialId() << " -> " << bt->object.getFiducialId() << endl;
												list<shared_ptr<Tangible>> ns;
												ns.splice(ns.end(), s, next(it), s.end());
												_sequences.push_back(ns);
												return;
											}
										}
									}
								}
							}
						}();
						_sequencesMutex.unlock();

					}
				} else {
					console() << "Unknown gesture..." << endl;
				}
			}
			theMoon: ;
		}
	}

	void TheApp::processTrace(shared_ptr<TouchTrace> trace) {
		Vec3f front = Vec3f(trace->touchPoints.front().getPos()*_s+_o);
		Vec3f back = Vec3f(trace->touchPoints.back().getPos()*_s+_o);
		double d = front.distance(back);
		if(d <= 2.0f) {
			// This could be a tap, let's check how long it was
			if(trace->touchPoints.back().timestamp - trace->touchPoints.front().timestamp < 1.0) {
				// If it's less than a second, there has been a tap
				shared_ptr<TapGesture> tap(new TapGesture(Vec2f(front.x, front.y)));
				_gesturesMutex.lock();
				_gestures.push_back(tap);
				_gesturesMutex.unlock();
				return;
			}
		}
		// If it wasn't a tap, let's treat it as a stroke and be done with it.
		shared_ptr<StrokeGesture> stroke(new StrokeGesture(trace));
		_gesturesMutex.lock();
		_gestures.push_back(stroke);
		_gesturesMutex.unlock();
	}

	void TheApp::keyDown(cinder::app::KeyEvent event) {
		switch(event.getChar()) {
			case KeyEvent::KEY_f: {
				setFullScreen(!isFullScreen());
				break;
			}
			case KeyEvent::KEY_p: {
				console() << _uo.x << ", " << _uo.y << endl;
				if(_params.isVisible()) {
					_params.hide();
				} else {
					_params.show();
				}
				break;
			}
			case KeyEvent::KEY_z: {
				if(event.isControlDown()) {
					_zoom -= 0.01f;
				}
				break;
			}
			case KeyEvent::KEY_x: {
				if(event.isControlDown()) {
					_zoom += 0.01f;
				}
				break;
			}
			case KeyEvent::KEY_w: {
				_uo.y -= event.isControlDown() ? 1.0f : 0.0f;
				break;
			}
			case KeyEvent::KEY_s: {
				_uo.y += event.isControlDown() ? 1.0f : 0.0f;
				break;
			}
			case KeyEvent::KEY_a: {
				_uo.x -= event.isControlDown() ? 1.0f : 0.0f;
				break;
			}
			case KeyEvent::KEY_d: {
				_uo.x += event.isControlDown() ? 1.0f : 0.0f;
				break;
			}
			case KeyEvent::KEY_c: {
				for(auto object : _objects) {
					_objects[object.first]->strokesMutex.lock();
					_objects[object.first]->strokes.clear();
					_objects[object.first]->strokesMutex.unlock();
				}
				break;
			}
		}
	}

	void TheApp::mouseDown(cinder::app::MouseEvent event) {

	}

	void TheApp::cursorAdded(tuio::Cursor cursor) {
		_tracesMutex.lock();
		_traces[cursor.getSessionId()] = make_shared<TouchTrace>();
		_traces[cursor.getSessionId()]->addCursorDown(cursor);
		_tracesMutex.unlock();
	}

	void TheApp::cursorUpdated(tuio::Cursor cursor) {
		_tracesMutex.lock();
		_traces[cursor.getSessionId()]->addCursorDown(cursor);
		_tracesMutex.unlock();
	}

	void TheApp::cursorRemoved(tuio::Cursor cursor) {
		_tracesMutex.lock();
		_traces[cursor.getSessionId()]->addCursorUp(cursor);
		_traces[cursor.getSessionId()]->isVisible = false;
		_tracesMutex.unlock();
		//_traces.erase(cursor.getSessionId());
		// Well, that was abrupt.
	}

	void TheApp::objectAdded(tuio::Object object) {
		if(_objects.find(object.getFiducialId()) != _objects.end()) {
			_objects[object.getFiducialId()]->object = object;
			_objects[object.getFiducialId()]->isVisible = true;
		} else {
			_objects[object.getFiducialId()] = make_shared<Tangible>();
			_objects[object.getFiducialId()]->object = object;
			_objects[object.getFiducialId()]->sender(_sender);
		}
		
		_sequencesMutex.lock();
		list<shared_ptr<Tangible>> l;
		l.push_back(_objects[object.getFiducialId()]);
		_sequences.push_back(l);
		_sequencesMutex.unlock();
	}

	void TheApp::objectUpdated(tuio::Object object) {
		_objects[object.getFiducialId()]->object = object;
	}

	void TheApp::objectRemoved(tuio::Object object) {
		_objects[object.getFiducialId()]->object = object;
		_objects[object.getFiducialId()]->isVisible = false;
		_objects[object.getFiducialId()]->timeRemoved = getElapsedSeconds();
	}

	Vec2f TheApp::tuioToWorld(Vec2f p) {
		return p * _s;
	}

	vector<shared_ptr<Tangible>> TheApp::getNeighbors(shared_ptr<Tangible> t) {
		//console() << "-- " << t->object.getFiducialId() << endl;
		vector<shared_ptr<Tangible>> v;
		for(auto _o : _objects) {
			if(_o.first != t->object.getFiducialId()) {
				shared_ptr<Tangible> o = _o.second;
				float d = tuioToWorld(t->object.getPos()).distance(tuioToWorld(o->object.getPos()));
				//console() << tuioToWorld(t->object.getPos()) << endl << tuioToWorld(o->object.getPos()) << endl << d << endl;
				if(d < 150.0f) {
					v.push_back(o);
				}
			}
		}
		sort(v.begin(), v.end(),
			[&, this](shared_ptr<Tangible> a, shared_ptr<Tangible> b) -> bool {
				float da = tuioToWorld(t->object.getPos()).distance(tuioToWorld(a->object.getPos()));
				float db = tuioToWorld(t->object.getPos()).distance(tuioToWorld(b->object.getPos()));
				
				return da < db;
		}); // woohoo! Lambdas are awesome!

		for(auto p : v) {
			console() << p->object.getFiducialId() << " :: " << tuioToWorld(t->object.getPos()).distance(tuioToWorld(p->object.getPos())) << endl;
		}
		return v;
	}
}

CINDER_APP_NATIVE( SecondStudy::TheApp, RendererGl )
