#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>

RTPCalibrationView::RTPCalibrationView() : 
  x(0),
  y(0),
	pressed(false),
  click_debounce_time(0),
	rtp_cali_step(CALIBRATION_START)
{

}

void RTPCalibrationView::setupScreen()
{
  RTPCalibrationViewBase::setupScreen();
	rtp_cali_step = CALIBRATION_START;
	showTchPoint(TCH_POINT_1);
}

void RTPCalibrationView::tearDownScreen()
{
  RTPCalibrationViewBase::tearDownScreen();
	rtp_cali_step = CALIBRATION_END;
}


void RTPCalibrationView::handleClickEvent(const ClickEvent& evt)
{
	switch (rtp_cali_step)
	{
		case TCH_POINT_1:
    if (evt.getType() == touchgfx::ClickEvent::PRESSED)
    {
      if (!pressed)
      {
        pressed = true;
        click_debounce_time = millis();
				x = evt.getX();
				y = evt.getY();
				logPrintf("x, y : %d, %d\n", x, y);
      }
    }
    else // RELEASED or CANCELED
    {
      pressed = false;
    }
		break;
		case TCH_POINT_2:
		break;
		case TCH_POINT_3:
		break;
		case TCH_POINT_4:
		break;
		case TCH_POINT_5:
		break;
		default:
		break;
	}
}

void RTPCalibrationView::handleTickEvent()
{
  switch (rtp_cali_step)
	{
		case CALIBRATION_START:
      showTchPoint(TCH_POINT_1);
      rtp_cali_step = TCH_POINT_1;
		break;
		case TCH_POINT_1:
    if (pressed)
    {
      if (millis() - click_debounce_time >= 500)
      {
        if (ak4183CalibrationProc(x, y))
        {
					// 평균값을 AK4183.c 에 있는 구조체 변수에 담는다. (5개의 데이터 중 1번째)
					// rtp_cali_step = TCH_POINT_2;
					showTchPoint(TCH_POINT_2);
					pressed = false;
        }
      }
    }
		/*
		if (ak4183CalibrationEnd(TCH_POINT_1))
		{
			hideTchPoint(TCH_POINT_1);
			showTchPoint(TCH_POINT_2);
		}
		*/
		break;
		case TCH_POINT_2:
		break;
		case TCH_POINT_3:
		break;
		case TCH_POINT_4:
		break;
		case TCH_POINT_5:
		break;
		case CALIBRATION_END:
		break;
	}
}