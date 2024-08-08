#ifndef RTPCALIBRATIONVIEW_HPP
#define RTPCALIBRATIONVIEW_HPP

#include <gui_generated/rtpcalibration_screen/RTPCalibrationViewBase.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationPresenter.hpp>

extern "C" {
    #include "ap.h"
    #include "touch/ak4183.h"
}

#define MAX_ADC_CNT         120
#define PRESSED_LATENCY     300
#define OBTAIN_TIME         2000

class RTPCalibrationView : public RTPCalibrationViewBase
{
public:
    RTPCalibrationView();
    virtual ~RTPCalibrationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void showTchPoint(uint8_t point)
    {
      touchgfx::Image *btns[] = {
        &TouchPoint1,
        &TouchPoint2,
        &TouchPoint3,
        &TouchPoint4,
        &TouchPoint5
      };
      for(uint8_t i=0;i<TCH_POINT_5;i++)
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

    void handleClickEvent(const ClickEvent& evt);
    void handleTickEvent(void);
protected:
    bool pressed;
    uint32_t pressed_time;
    RtpCalibrationStep_t rtp_cali_step;
    ak4183_adc_t adc;
    uint32_t x_adc_sum;
    uint32_t y_adc_sum;
    ak4183_adc_t adc_avg;
    uint32_t adc_cnt;
};

#endif // RTPCALIBRATIONVIEW_HPP
