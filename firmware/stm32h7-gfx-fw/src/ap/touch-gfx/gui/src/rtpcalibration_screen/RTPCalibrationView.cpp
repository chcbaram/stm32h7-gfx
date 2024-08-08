#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>

RTPCalibrationView::RTPCalibrationView() : 
  pressed(false),
	pressed_time(0),
  rtp_cali_step(TCH_POINT_1),
	adc{ },
  x_adc_sum(0),
  y_adc_sum(0),
  adc_avg{ },
	adc_cnt(0)
{

}

void RTPCalibrationView::setupScreen()
{
  RTPCalibrationViewBase::setupScreen();
	rtp_cali_step = TCH_POINT_1;
	showTchPoint(TCH_POINT_1);
}

void RTPCalibrationView::tearDownScreen()
{
  RTPCalibrationViewBase::tearDownScreen();
	rtp_cali_step = TCH_POINT_1;
}


void RTPCalibrationView::handleClickEvent(const ClickEvent& evt)
{
  if (evt.getType() == touchgfx::ClickEvent::PRESSED)
  {
    adc_cnt = 0;
    pressed = true;
    pressed_time = millis();
  }

  if (evt.getType() == touchgfx::ClickEvent::RELEASED)
  {
    pressed = false;
  }
}


void RTPCalibrationView::handleTickEvent()
{
	if (pressed)
	{
    if (adc_cnt <= MAX_ADC_CNT)
		{
      if (millis() - pressed_time > PRESSED_LATENCY) // 300ms 동안의 ADC 는 무시
      {
        if (ak4183ReadAdc(&adc) == true)
        {
          x_adc_sum += adc.x_adc;
          y_adc_sum += adc.y_adc;
          adc_cnt++;
          logPrintf("[  ] adc count : %3d, adc x : %4d, adc y : %4d\n", adc_cnt, adc.x_adc, adc.y_adc);
        }
        else
        {
          logPrintf("[E_] RTP Get ADC Failed\n");
        }
      }
		}
    else
    {
      //avg 
      adc_avg.x_adc = x_adc_sum / MAX_ADC_CNT;
      adc_avg.y_adc = y_adc_sum / MAX_ADC_CNT;
      pressed = false;

      if (rtp_cali_step < TCH_POINT_5)
      {
        showTchPoint(rtp_cali_step+1);
      }
      logPrintf("[  ] adc count : %d, adc_avg x : %d, adc_avg y : %d\n", adc_cnt, adc_avg.x_adc, adc_avg.y_adc);
    }
	}
}