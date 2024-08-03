#ifndef RTPCALIBRATIONVIEW_HPP
#define RTPCALIBRATIONVIEW_HPP

#include <gui_generated/rtpcalibration_screen/RTPCalibrationViewBase.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationPresenter.hpp>


class RTPCalibrationView : public RTPCalibrationViewBase
{
public:
    

    RTPCalibrationView();
    virtual ~RTPCalibrationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    
    uint32_t getSecond()
    {
        return sec;
    }
protected:
   
    void handleTickEvent();
    uint32_t frameTick;
    uint32_t sec;
    uint8_t tchPointStep;
    bool pressed;
    uint32_t tchPointTime[TCH_POINT_MAX];
};

#endif // RTPCALIBRATIONVIEW_HPP
