#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>

RTPCalibrationView::RTPCalibrationView() : 
  pressed(false),
	pressed_time(0),
  rtp_cali_step(TCH_POINT_1),
	adc{ },
  x_adc_sum(0),
  y_adc_sum(0),
  adc_avg{ },
	adc_cnt(0),
  popup_show_time(0),
  cali_data{ }
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
    // 터치 영역이 캘리브레이션 취소 버튼 영역이면 초기 화면으로 이동
    if (evt.getX() >= CalibrationCancelBtn.getX() && evt.getX() <= (CalibrationCancelBtn.getX() + CalibrationCancelBtn.getWidth()))
    {
      if (evt.getY() >= CalibrationCancelBtn.getY() && evt.getY() <= (CalibrationCancelBtn.getY() + CalibrationCancelBtn.getHeight()))
      {
        application().changeToStartScreen();
      }
    }

    logPrintf("[  ] pressed X : %d, Y : %d\n", evt.getX(), evt.getY());
    pressed = true;
    pressed_time = millis();
  }

  if (evt.getType() == touchgfx::ClickEvent::RELEASED)
  {
    logPrintf("[  ] released\n");
    pressed = false;
  }
}

void RTPCalibrationView::showPopupText(const char* text)
{
  if (text != NULL)
  {
    popup_show_time = millis();
    Unicode::snprintf(PopupTextBuffer, POPUPTEXT_SIZE, text);
    Popup.setVisible(true);
    Popup.invalidate();
  }
}

void RTPCalibrationView::handleTickEvent()
{
	if (pressed)
	{
    if (adc_cnt < AK4183_READ_ADC_CNT)
    {
      if (millis() - pressed_time > PRESSED_LATENCY) // 300ms 동안의 ADC 는 무시
      {
        if (ak4183ReadAdc(&adc) == true)
        {
          x_adc_sum += adc.x_adc;
          y_adc_sum += adc.y_adc;
          adc_cnt++;

          #if LOG
          logPrintf("[%3d] adc x : %4d, adc y : %4d\n", adc_cnt, adc.x_adc, adc.y_adc);
          #endif
        }
        else
        {
          #if LOG
          logPrintf("[E_] RTP Get ADC Failed\n");
          #endif
        }
      }
    }
    else
    {
      // avg 
      adc_avg.x_adc = x_adc_sum / AK4183_READ_ADC_CNT;
      adc_avg.y_adc = y_adc_sum / AK4183_READ_ADC_CNT;

      #if LOG
      logPrintf("[  ] adc_avg x : %d, adc_avg y : %d\n", adc_avg.x_adc, adc_avg.y_adc);
      #endif

      cali_data.x_adc[rtp_cali_step] = adc_avg.x_adc;
      cali_data.y_adc[rtp_cali_step] = adc_avg.y_adc;
      
      if (rtp_cali_step < TCH_POINT_5)
      {  
        rtp_cali_step++;
        showTchPoint(rtp_cali_step);
      }
      else
      {
        if (ak4183IsCaliResultErr(&cali_data))
        {
          showPopupText("Calibration Success");

          #if LOG
          logPrintf("[  ] success\n");
          #endif
          if (ak4183SaveCaliData(&cali_data) == false)
          {
            // eeprom save error
            #if LOG
            logPrintf("[E_] saving data to eeprom failed\n");
            #endif
          }
        }
        else
        {
          // 기본 세팅값으로 설정
          // 
          showPopupText("Calibration Failed");
          ak4183SetDefault();
        }
        rtp_cali_step = TCH_POINT_1;
        memset(&cali_data, 0, sizeof(cali_data));
      }

      // Initialize Parameter
      x_adc_sum = 0;
      y_adc_sum = 0;
      adc_cnt = 0;
      
      memset(&adc_avg, 0, sizeof(adc_avg));
      pressed = false;
    }
  }

  if (millis() - popup_show_time >= 3000 && Popup.isVisible())
  {
    Popup.setVisible(false);
    Popup.invalidate();
    showTchPoint(TCH_POINT_1);
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

  for(uint8_t i=0;i<5;i++)
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
      default:
      break;
  }

  invalidate();
}