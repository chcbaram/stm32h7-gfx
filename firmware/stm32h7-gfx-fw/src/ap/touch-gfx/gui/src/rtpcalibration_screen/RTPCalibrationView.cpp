#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>

RTPCalibrationView::RTPCalibrationView() :
	frameTick(0),
	sec(0),
	tchPointStep(TCH_POINT_1),
	pressed(false)
{

}

void RTPCalibrationView::setupScreen()
{
    RTPCalibrationViewBase::setupScreen();
	rtpCalibrationOn();
}

void RTPCalibrationView::tearDownScreen()
{
    RTPCalibrationViewBase::tearDownScreen();
}

void RTPCalibrationView::handleTickEvent()
{
	frameTick++;
	if (frameTick % 60 == 0)
	{
		sec++;
	}
}