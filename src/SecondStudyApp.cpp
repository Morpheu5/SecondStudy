#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/params/Params.h"

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
		Vec2f _s, _o;
		params::InterfaceGl _params;
		
		tuio::Client _tuioClient;
		
		string _hostname;
		int _port;
		//osc::Listener _oscListener;
		
		map<int, shared_ptr<Tangible>> _objects;
		map<int, shared_ptr<TouchTrace>> _traces;

		list<shared_ptr<TouchTrace>> _finishedTraces;
		list<shared_ptr<Gesture>> _gestures;
		mutex _gesturesMutex;
		
		thread _gestureProcessor;

		list<list<shared_ptr<Tangible>>> _sequences;
		mutex _sequencesMutex;

	public:
		void setup();
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

		//console() << "_s : " << _s << endl << "_o : " << _o << endl;

		_gestureProcessor = thread(bind(&TheApp::processGestures, this));
	}

	void TheApp::update() {
		for(auto i = _traces.begin(); i != _traces.end(); ) {
			if(!i->second->isVisible && i->second->isDead()) {
				_finishedTraces.push_back(i->second);
				i = _traces.erase(i);
			} else {
				++i;
			}
		}

		if(_finishedTraces.size() > 0) {
			processTrace(_finishedTraces.front());
			_finishedTraces.pop_front();
		}

		/* This should automatically create sequences based on proximity but it doesn't work all that well...
		for(auto& o : _objects) {
			shared_ptr<Tangible> t = o.second;
			vector<shared_ptr<Tangible>> N = getNeighbors(t);
			if(N.size() == 0) {
				_sequencesMutex.lock();
				_sequences[t->object.getFiducialId()] = t;
				_sequencesMutex.unlock();
			} else if(N.size() == 1) {
				shared_ptr<Tangible> n = N[0];
				if(t->next != n) {
					_sequencesMutex.lock();
					if(_sequences[n->object.getFiducialId()] != nullptr) { // n.isHead
						t->setNext(n);
						_sequences.erase(n->object.getFiducialId());
						_sequences[t->object.getFiducialId()] = t;
					} else { // must be tail
						n->setNext(t);
					}
					_sequencesMutex.unlock();
				}
			} else { // N.size() > 1
				shared_ptr<Tangible> n = N[0]; // n is the closest
				shared_ptr<Tangible> m = N[1];
				if(!(t->next == n || t->next == m || n->next == t || m->next == t)) {
					// t is not in a sequence longer than 1
					if(n->next !=m && m->next != n) { // t is between two distinct sequences
						_sequencesMutex.lock();
						if(_sequences[n->object.getFiducialId()] != nullptr) { // n.isHead
							t->setNext(n);
							_sequences.erase(n->object.getFiducialId());
							_sequences[t->object.getFiducialId()] = t;
						} else if(n->prev != nullptr && n->next == nullptr) { // n.isTail
							n->setNext(t);
						} else {
							_sequences[t->object.getFiducialId()] = t;
						}
						_sequencesMutex.unlock();
					}
				} else { // n and m belong to the same sequence
					if(m->next == n) {
						t->setNext(n);
						m->setNext(t);
					} else { // n->next == m
						t->setNext(m);
						n->setNext(t);
					}
				}
			}
		}
		*/
	}

	void TheApp::draw() {
		gl::clear(Color(0, 0, 0));
		
		_scale = getWindowHeight() / 480.0f;

		gl::color(0.2f, 0.2f, 0.2f);
		glLineWidth(2.0f*_scale);
		//gl::drawStrokedCircle(getWindowCenter(), 0.5f * getWindowHeight());
		gl::color(1.0f, 1.0f, 1.0f);
		glLineWidth(1.0f*_scale);

		_sequencesMutex.lock();
		for(auto& s : _sequences) {
			if(s.size() > 1) {
				for(auto it = s.begin(); it != prev(s.end()); ++it) {
					shared_ptr<Tangible> a = *it;
					shared_ptr<Tangible> b = *(next(it));
					Vec2f ap = a->object.getPos() * _s + _o;
					Vec2f bp = b->object.getPos() * _s + _o;
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
		
		for(auto object : _objects) {
			shared_ptr<Tangible> t = object.second;
			if(!t->isVisible) {
				continue;
			}

			// PUSH MODEL VIEW
			gl::pushModelView();

			Matrix44f transform;
			transform.translate(Vec3f((t->object.getPos()*_s)+_o));
			transform.rotate(Vec3f(0.0f, 0.0f, t->object.getAngle()));
			gl::multModelView(transform);

			gl::drawSolidRect(Rectf(-20.0f*_scale, -20.0f*_scale, 20.0f*_scale, 20.0f*_scale));

			gl::drawStrokedCircle(Vec2f(0,0), 50.0f*_scale);

			glLineWidth(2.0f*_scale);
			if(t->isOn) {
				gl::drawStrokedRect(t->board*_scale);

				t->strokesMutex.lock();
				list<list<Vec2f>> traces = t->strokes;
				t->strokesMutex.unlock();
				
				for(auto trace : traces) {
					vector<Vec2f> v;
					for(auto p : trace) {
						Vec2f q(p*t->board.getSize()*_scale + t->board.getCenter()*_scale);
						v.push_back(q);
					}
					if(v.size() > 2) { 
						BSpline2f l(v, min((int)v.size()-1, 5), false, true);
						PolyLine2f pl;
						for(int i = 0; i < (int)floor(v.size()); i++) {
							float t = (float)i/(float)(v.size());
							//pl.push_back(l.getPosition(t));
							//gl::drawSolidCircle(l.getPosition(t), 2.0f*_scale);
						}
						glLineWidth(1.0f * _scale);
						gl::draw(PolyLine2f(v));
						glLineWidth(1.0f * _scale);
					}
				}
			} else {
				gl::drawStrokedRect(t->icon * _scale);
			}
			glLineWidth(1.0f * _scale);

			gl::popModelView();
			// POP MODEL VIEW
		}
		
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
					pl.push_back((l.getPosition(t)*_s)+_o);
				}
				glLineWidth(2.0f * _scale);
				gl::draw(pl);
				glLineWidth(1.0f * _scale);
			}
			
			TouchPoint p = touchPoints.back();
			gl::drawSolidCircle((p.getPos()*_s)+_o , _zoom * _scale * 2.0f);
		}
		
		//_params.draw();

		glLineWidth(1.0f * _scale);
	}
	
	void TheApp::resize() {
		float w = getWindowWidth();
		float h = getWindowHeight();
		_s = Vec2f(h / 0.75f, h);
		_o = Vec2f((w - h/0.75f)/2.0f, 0.0f);

		//console() << getWindowSize() << endl << _s << endl << _o << endl;
	}

	void TheApp::processGestures() {
		// TODO busy waiting is for whimps. Counting semaphore or signal queue, maybe?
		while(true) {
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
						transform.translate(Vec3f(t->object.getPos()*_s+_o));
						transform.rotate(Vec3f(0.0f, 0.0f, t->object.getAngle()));
						Vec3f tp = transform.inverted().transformPoint(p);
						// Let's see if the tap hit a box
						Rectf box = t->isOn ? t->board * _scale : t->icon * _scale;
						if(box.contains(Vec2f(tp.x, tp.y))) {
							_objects[object.first]->isOn = !t->isOn;
						}
					}
				} else if(dynamic_pointer_cast<StrokeGesture>(g) != nullptr) {
					shared_ptr<StrokeGesture> stroke = dynamic_pointer_cast<StrokeGesture>(g);
					// Let's see if it's a musical stroke.
					// Check if both front() and back() are on the same active object's box.
					Vec3f front = Vec3f(stroke->trace.touchPoints.front().getPos() * Vec2f(getWindowSize()));
					Vec3f back = Vec3f(stroke->trace.touchPoints.back().getPos() * Vec2f(getWindowSize()));
					for(auto object : _objects) {
						shared_ptr<Tangible> tangible = object.second;

						// CONNECTION STROKE
						for(auto other : _objects) {
							if(tangible != other.second && tangible->isVisible && other.second->isVisible) {
								shared_ptr<Tangible> otherTangible = other.second;
								Vec2f thisPos = tangible->object.getPos() * Vec2f(getWindowSize());
								Vec2f otherPos = otherTangible->object.getPos() * Vec2f(getWindowSize());
								if(thisPos.distance(Vec2f(front.x, front.y)) <= _scale*50.0f && otherPos.distance(Vec2f(back.x, back.y)) <= _scale*50.0f) {
									_sequencesMutex.lock();

									for(auto sit = _sequences.begin(); sit != _sequences.end(); ++sit) {
										for(auto lit = sit->begin(); lit != sit->end(); ++lit) {
											if(*lit == tangible) {
												// Check if otherTangible is in the same sequence, just before lit
												for(auto i(lit); i != sit->begin(); --i) {
													if(*i == otherTangible) {
														goto omg;
													}
												}
												// cover the head case because of reverse iterator madness
												if(*(sit->begin()) == otherTangible) {
													goto omg;
												}
												if(*(sit->begin()) != otherTangible) { // prevents tail-head loops
													for(auto nsit = _sequences.begin(); nsit != _sequences.end(); ++nsit) {
														for(auto nlit = nsit->begin(); nlit != nsit->end(); ++nlit) {
															if(*nlit == otherTangible) {
																nsit->splice(nlit, *sit, sit->begin(), next(lit));
																goto omg;
															}
														}
													}
												}
											}
										}
									}
omg:

									int i = 0;
									console() << endl << "========= " << tangible->object.getFiducialId() << " " << otherTangible->object.getFiducialId();
									for(auto& seq : _sequences) {
										console() << endl << i++ << " ---" << endl;
										for(auto& t : seq) {
											console() << "   " << t->object.getFiducialId() << " ";
										}
									}
									console() << endl;

									_sequencesMutex.unlock();
								}
							}
						}


						// MUSICAL STROKE
						if(tangible->isOn) {
							Rectf box = tangible->board*_scale;
							Matrix44f transform;
							transform.translate(Vec3f(tangible->object.getPos()*_s)+Vec3f(_o.x, _o.y, 0.0f));
							transform.rotate(Vec3f(0.0f, 0.0f, tangible->object.getAngle()));
							Vec3f tfront = transform.inverted().transformPoint(front);
							Vec3f tback = transform.inverted().transformPoint(back);
							if(box.contains(Vec2f(tfront.x, tfront.y)) && box.contains(Vec2f(tback.x, tback.y))) {
								// Rejoice in happiness, it's a musical stroke!
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

								if(qs.size() > 2) { // should always be the case
									BSpline2f l(qs, min((int)qs.size()/1, 3), false, true);
									float totLength = l.getLength(0, 1);
									// This is a real P.I.T.A., it takes ages if the stroke is too long.
									float step = 0.05/log10(1+totLength);//0.05/sqrtf(totLength);
									if(step > 0.5) {
										step = 0.2;
									}
									console() << "Length: " << totLength << " f: " << sqrtf(totLength) << endl << "Step: " << step << endl;
									for(float p = 0.0f; p <= 1.0f; p += step) {
										Vec2f lp(l.getPosition(l.getTime(p * totLength)));
										transformedStroke.push_back(lp / (tangible->board.getSize()/Vec2f(640.0f, 480.0f)));
									}
								}
								tangible->strokesMutex.lock();
								tangible->strokes.push_back(transformedStroke);
								tangible->strokesMutex.unlock();
							}
						}
					}
				} else {
					console() << "Unknown gesture..." << endl;
				}
			}
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
		_traces[cursor.getSessionId()] = make_shared<TouchTrace>();
		_traces[cursor.getSessionId()]->addCursorDown(cursor);
	}

	void TheApp::cursorUpdated(tuio::Cursor cursor) {
		_traces[cursor.getSessionId()]->addCursorDown(cursor);
	}

	void TheApp::cursorRemoved(tuio::Cursor cursor) {
		_traces[cursor.getSessionId()]->addCursorUp(cursor);
		_traces[cursor.getSessionId()]->isVisible = false;
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
		}
		
		_sequencesMutex.lock();
		list<shared_ptr<Tangible>> l;
		l.push_back(_objects[object.getFiducialId()]);
		_sequences.push_back(l);
		_sequencesMutex.unlock();

		int i = 0;
		console() << endl << "=========";
		for(auto& seq : _sequences) {
			console() << endl << i++ << " ---" << endl;
			for(auto& t : seq) {
				console() << "   " << t->object.getFiducialId() << " ";
			}
		}
		console() << endl;
	}

	void TheApp::objectUpdated(tuio::Object object) {
		_objects[object.getFiducialId()]->object = object;
	}

	void TheApp::objectRemoved(tuio::Object object) {
		_objects[object.getFiducialId()]->object = object;
		_objects[object.getFiducialId()]->isVisible = false;

		_sequencesMutex.lock();
		for(auto sit = _sequences.begin(); sit != _sequences.end(); ++sit) {
			sit->remove_if([&](shared_ptr<Tangible> t) {
				return t == _objects[object.getFiducialId()];
			});
		}
		_sequencesMutex.unlock();
	}

	Vec2f TheApp::tuioToWorld(Vec2f p) {
		return p * _s;
	}

	/*Vec2f worldToScreen(Vec2f p) {

	}*/

	vector<shared_ptr<Tangible>> TheApp::getNeighbors(shared_ptr<Tangible> t) {
		//console() << "-- " << t->object.getFiducialId() << endl;
		vector<shared_ptr<Tangible>> v;
		for(auto& _o : _objects) {
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

	/*
	shared_ptr<Tangible> TheApp::getClosestNeighbor(shared_ptr<Tangible> t) {
		vector<shared_ptr<Tangible>> v = getNeighbors(t);
		if(v.empty()) {
			return nullptr;
		} else if(v.size() == 1) {
			return v[0];
		} else {
			shared_ptr<Tangible> c = v[0];
			v.erase(v.begin());
			for(auto o : v) {
				float d = tuioToWorld(t->object.getPos()).distance(tuioToWorld(o->object.getPos()));
				if(tuioToWorld(t->object.getPos()).distance(tuioToWorld(c->object.getPos())) > d) {
					c = o;
				}
			}
			return c;
		}
	}
	*/
}

CINDER_APP_NATIVE( SecondStudy::TheApp, RendererGl )
