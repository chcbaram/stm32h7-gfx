#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>

RTPCalibrationView::RTPCalibrationView() : 
  pressed(false),
	pressed_time(0),
  rtp_cali_step(TCH_POINT_1),
	adc{ },
  x_adc_sum(0),
  y_adc_sum(0),
  adc_avg{ },
  calibration_info{ },
	adc_cnt(0)
{

}

void RTPCalibrationView::setupScreen()
{
  RTPCalibrationViewBase::setupScreen();
	rtp_cali_step = TCH_POINT_1;
	showTchPoint(rtp_cali_step);
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
    logPrintf("[  ] pressed\n");
    pressed = true;
    pressed_time = millis();
  }

  if (evt.getType() == touchgfx::ClickEvent::RELEASED)
  {
    logPrintf("[  ] released\n");
    pressed = false;
  }
}


void RTPCalibrationView::handleTickEvent()
{
	if (pressed)
	{
    if (rtp_cali_step < TCH_POINT_MAX)
    {
      if (adc_cnt < MAX_ADC_CNT)
      {
        if (millis() - pressed_time > PRESSED_LATENCY) // 300ms 동안의 ADC 는 무시
        {
          if (ak4183ReadAdc(&adc) == true)
          {
            x_adc_sum += adc.x_adc;
            y_adc_sum += adc.y_adc;
            adc_cnt++;

            #ifdef LOG
            logPrintf("[  ] adc count : %3d, adc x : %4d, adc y : %4d\n", adc_cnt, adc.x_adc, adc.y_adc);
            #endif
          }
          else
          {
            #ifdef LOG
            logPrintf("[E_] RTP Get ADC Failed\n");
            #endif
          }
        }
      }
      else
      {
        // avg 
        adc_avg.x_adc = x_adc_sum / MAX_ADC_CNT;
        adc_avg.y_adc = y_adc_sum / MAX_ADC_CNT;
        logPrintf("[  ] adc count : %d, adc_avg x : %d, adc_avg y : %d\n", adc_cnt, adc_avg.x_adc, adc_avg.y_adc);

        pressed = false;

        calibration_info.x_adc[rtp_cali_step] = adc_avg.x_adc;
        calibration_info.y_adc[rtp_cali_step] = adc_avg.y_adc;

        adc_avg.x_adc = 0;
        adc_avg.y_adc = 0;
        x_adc_sum = 0;
        y_adc_sum = 0;
        adc_cnt = 0;

        if (rtp_cali_step <= TCH_POINT_5)
        {
          rtp_cali_step++;
          showTchPoint(rtp_cali_step);
        }
      }
    }
	}
  else
  {
    if (rtp_cali_step > TCH_POINT_5)
    {
      #ifdef LOG
      logPrintf("\n");
      for (uint8_t i=0; i<5; i++)
      {
        logPrintf("calibration_info x_adc[%d] : %4d,  y_adc[%d] : %4d\n", i, calibration_info.x_adc[i], i, calibration_info.y_adc[i]);
      }
      logPrintf("\n");
      #endif

      if (ak4183IsCaliResultErr(&calibration_info))
      {
        logPrintf("[  ] success\n");
        if (ak4183SaveCaliData(&calibration_info))
        {
          // Popup - Success
        }
        else
        {
          // eeprom save error
        }
      } 

      // Initialize Parameter
      x_adc_sum = 0;
      y_adc_sum = 0;
      adc_cnt = 0;
      memset(&adc_avg, 0, sizeof(adc_avg));
      memset(&calibration_info, 0, sizeof(calibration_info));
      rtp_cali_step = TCH_POINT_1;
      showTchPoint(rtp_cali_step);
    }
  }
}

void RTPCalibrationView::showTchPoint(uint8_t point)
{
  touchgfx::Image *btns[] = {
    &TouchPoint1,
    &TouchPoint2,
    &TouchPoint3,
    &TouchPoint4,
    &TouchPoint5
  };

  for(uint8_t i=0;i<TCH_POINT_MAX;i++)
  {
    btns[i]->setVisible(i == point);
    btns[i]->invalidate();
  }

  TouchPoint1.setVisible(false);
  TouchPoint2.setVisible(false);
  TouchPoint3.setVisible(false);
  TouchPoint4.setVisible(false);
  TouchPoint5.setVisible(false);

  switch (point)
  {
      case TCH_POINT_1:
      TouchPoint1.setVisible(true);
      break;
      case TCH_POINT_2:
      TouchPoint2.setVisible(true);
      break;
      case TCH_POINT_3:
      TouchPoint3.setVisible(true);
      break;
      case TCH_POINT_4:
      TouchPoint4.setVisible(true);
      break;
      case TCH_POINT_5:
      TouchPoint5.setVisible(true);
      break;
      case TCH_POINT_MAX:
      // 보정이 완료되었습니다. 팝업
      break;
      default:
      break;
  }

  invalidate();
}