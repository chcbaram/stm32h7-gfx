#ifndef RTPCALIBRATIONVIEW_HPP
#define RTPCALIBRATIONVIEW_HPP

#include <gui_generated/rtpcalibration_screen/RTPCalibrationViewBase.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationPresenter.hpp>

#define PRESS_TIME          3 // Second
class RTPCalibrationView : public RTPCalibrationViewBase
{
public:
    typedef enum
    {
        TCH_POINT_1,
        TCH_POINT_2,
        TCH_POINT_3,
        TCH_POINT_4,
        TCH_POINT_5,
        TCH_POINT_BACK,
        TCH_POINT_MAX
    } RtpCalibrationStep_t ;

    RTPCalibrationView();
    virtual ~RTPCalibrationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    
    uint32_t getSecond()
    {
        return sec;
    }
protected:
    void touchPointBtnCallbackHandler(const RepeatButton& btn, const ClickEvent& event);
    Callback <RTPCalibrationView, const RepeatButton&, const ClickEvent&> touchPointBtnCallback;
    void handleTickEvent();
    uint32_t frameTick;
    uint32_t sec;
    uint8_t tchPointStep;
    bool pressed;
    uint32_t tchPointTime[TCH_POINT_MAX];
};

#endif // RTPCALIBRATIONVIEW_HPP
