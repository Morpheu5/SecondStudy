#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class SecondStudyApp : public AppNative {
  public:
	void setup();
	void mouseDown( MouseEvent event );	
	void update();
	void draw();
};

void SecondStudyApp::setup()
{
}

void SecondStudyApp::mouseDown( MouseEvent event )
{
}

void SecondStudyApp::update()
{
}

void SecondStudyApp::draw()
{
	// clear out the window with black
	gl::clear( Color( 0, 0, 0 ) ); 
}

CINDER_APP_NATIVE( SecondStudyApp, RendererGl )
