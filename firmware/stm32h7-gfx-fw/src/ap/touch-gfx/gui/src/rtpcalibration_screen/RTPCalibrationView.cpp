#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>

RTPCalibrationView::RTPCalibrationView() :
	touchPointBtnCallback(this, &RTPCalibrationView::touchPointBtnCallbackHandler),
	frameTick(0),
	sec(0),
	tchPointStep(TCH_POINT_1),
	pressed(false)
{
	TouchPoint1.setClickAction(touchPointBtnCallback);
	TouchPoint2.setClickAction(touchPointBtnCallback);
	TouchPoint3.setClickAction(touchPointBtnCallback);
	TouchPoint4.setClickAction(touchPointBtnCallback);
	TouchPoint5.setClickAction(touchPointBtnCallback);
}

void RTPCalibrationView::setupScreen()
{
    RTPCalibrationViewBase::setupScreen();
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

void RTPCalibrationView::touchPointBtnCallbackHandler(const RepeatButton& btn, const ClickEvent& event)
{
	#ifdef SIMULATOR
		touchgfx_printf("[%02d] tchPoint Pressed\n", tchPointStep);
	#endif

    switch (tchPointStep)
    {
			case TCH_POINT_1:
			if (&btn == &TouchPoint1)
			{
				if (event.getType() == ClickEvent::PRESSED)
				{
					if (!pressed)
					{
						pressed = true;
						tchPointTime[TCH_POINT_1] = getSecond();
					}
				}
				if (event.getType() == ClickEvent::RELEASED)
				{
					pressed = false;
					if (getSecond() - tchPointTime[TCH_POINT_1] >= PRESS_TIME)
					{
						TouchPoint1.setVisible(false);
						TouchPoint2.setVisible(true);
						presenter->notifyRtpCalibrationStep(tchPointStep);
						tchPointStep = TCH_POINT_2;
						invalidate();
					}
				}
			}
			break;

			case TCH_POINT_2:
			if (&btn == &TouchPoint2)
      {
        if (event.getType() == ClickEvent::PRESSED)
        {
          if (!pressed)
          {
            pressed = true;
            tchPointTime[TCH_POINT_2] = getSecond();
          }
        }
        if (event.getType() == ClickEvent::RELEASED)
        {
          pressed = false;
					if (getSecond() - tchPointTime[TCH_POINT_2] >= PRESS_TIME)
					{
						// hide tch point 1
						TouchPoint2.setVisible(false);
						// show tch point 2
						TouchPoint3.setVisible(true);
						// tchPointStep update
						tchPointStep = TCH_POINT_3;
						invalidate();
					}
        }
      }
			break;

			case TCH_POINT_3:
			if (&btn == &TouchPoint3)
      {
        if (event.getType() == ClickEvent::PRESSED)
        {
          if (!pressed)
          {
            pressed = true;
            tchPointTime[TCH_POINT_3] = getSecond();
          }
        }
        if (event.getType() == ClickEvent::RELEASED)
        {
          pressed = false;
					if (getSecond() - tchPointTime[TCH_POINT_3] >= PRESS_TIME)
					{
						// hide tch point 1
						TouchPoint3.setVisible(false);
						// show tch point 2
						TouchPoint4.setVisible(true);
						// tchPointStep update
						tchPointStep = TCH_POINT_4;
						invalidate();
					}
        }
      }
			break;

			case TCH_POINT_4:
			if (&btn == &TouchPoint4)
      {
        if (event.getType() == ClickEvent::PRESSED)
        {
          if (!pressed)
          {
            pressed = true;
            tchPointTime[TCH_POINT_4] = getSecond();
          }
        }
        if (event.getType() == ClickEvent::RELEASED)
        {
          pressed = false;
					if (getSecond() - tchPointTime[TCH_POINT_4] >= PRESS_TIME)
					{
						// hide tch point 1
						TouchPoint4.setVisible(false);
						// show tch point 2
						TouchPoint5.setVisible(true);
						// tchPointStep update
						tchPointStep = TCH_POINT_5;
						invalidate();
					}
        }
      }
			break;

			case TCH_POINT_5:
			if (&btn == &TouchPoint5)
      {
        if (event.getType() == ClickEvent::PRESSED)
        {
          if (!pressed)
          {
            pressed = true;
            tchPointTime[TCH_POINT_5] = getSecond();
          }
        }
        if (event.getType() == ClickEvent::RELEASED)
        {
          pressed = false;
					if (getSecond() - tchPointTime[TCH_POINT_5] >= PRESS_TIME)
					{
						// hide tch point 1
						TouchPoint5.setVisible(false);
						// show tch point 2
						TouchPoint1.setVisible(true);
						// tchPointStep update
						tchPointStep = TCH_POINT_1;
						invalidate();
						// screen change
						application().gotoHomeScreenNoTransition();
					}
        }
      }
			break;

			default:
			break;
    }
}

