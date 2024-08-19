#ifndef HOMEPRESENTER_HPP
#define HOMEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class HomeView;

class HomePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    HomePresenter(HomeView& v);

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

    virtual ~HomePresenter() {}

private:
    HomePresenter();

    HomeView& view;
};

#endif // HOMEPRESENTER_HPP
