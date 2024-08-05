#ifndef RTPCALIBRATIONVIEW_HPP
#define RTPCALIBRATIONVIEW_HPP

#include <gui_generated/rtpcalibration_screen/RTPCalibrationViewBase.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationPresenter.hpp>

extern "C" {
    #include "ap.h"
    #include "touch/ak4183.h"
}

class RTPCalibrationView : public RTPCalibrationViewBase
{
public:
    

    RTPCalibrationView();
    virtual ~RTPCalibrationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    void showTchPoint(uint8_t point)
    {
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
		int16_t x;
		int16_t y;
    bool pressed;
    uint32_t click_debounce_time;
    RtpCalibrationStep_t rtp_cali_step;
};

#endif // RTPCALIBRATIONVIEW_HPP
