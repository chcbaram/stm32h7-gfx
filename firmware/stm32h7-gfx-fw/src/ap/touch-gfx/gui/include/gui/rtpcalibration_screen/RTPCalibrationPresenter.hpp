#ifndef RTPCALIBRATIONPRESENTER_HPP
#define RTPCALIBRATIONPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class RTPCalibrationView;

class RTPCalibrationPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    RTPCalibrationPresenter(RTPCalibrationView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~RTPCalibrationPresenter() {}
private:
    RTPCalibrationPresenter();

    RTPCalibrationView& view;
};

#endif // RTPCALIBRATIONPRESENTER_HPP
