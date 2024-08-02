#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationPresenter.hpp>

RTPCalibrationPresenter::RTPCalibrationPresenter(RTPCalibrationView& v)
    : view(v)
{

}

void RTPCalibrationPresenter::activate()
{

}

void RTPCalibrationPresenter::deactivate()
{

}

void RTPCalibrationPresenter::notifyRtpCalibrationStep(uint8_t step)
{
    model->getCalibrationStep(step);
}