#include <gui/home_screen/HomeView.hpp>

HomeView::HomeView()
{

}

void HomeView::setupScreen()
{
    HomeViewBase::setupScreen();
    
    #ifdef _USE_HW_AK4183
    RTPCalibrationBtn.setVisible(true);
    RTPCalibrationBtn.invalidate();
    #endif
}

void HomeView::tearDownScreen()
{
    HomeViewBase::tearDownScreen();
}
