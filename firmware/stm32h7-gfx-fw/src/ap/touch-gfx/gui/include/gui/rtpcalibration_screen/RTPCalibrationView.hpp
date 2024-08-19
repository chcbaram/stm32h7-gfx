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
#define LOG

class RTPCalibrationView : public RTPCalibrationViewBase
{
public:
    RTPCalibrationView();
    virtual ~RTPCalibrationView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void showTchPoint(uint8_t point);
    void handleClickEvent(const ClickEvent& evt);
    void handleTickEvent(void);

    
protected:
    void (*calibrationEndCallback)(void);
    
    bool pressed;
    uint32_t pressed_time;
    uint8_t rtp_cali_step;
    ak4183_adc_t adc;
    uint32_t x_adc_sum;
    uint32_t y_adc_sum;
    ak4183_adc_t adc_avg;
    ak4183_cali_t calibration_info;
    uint32_t adc_cnt;
};

#endif // RTPCALIBRATIONVIEW_HPP
