/*********************************************************************************/
/********** THIS FILE IS GENERATED BY TOUCHGFX DESIGNER, DO NOT MODIFY ***********/
/*********************************************************************************/
#include <new>
#include <gui_generated/common/FrontendApplicationBase.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <touchgfx/transitions/NoTransition.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Texts.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <platform/driver/lcd/LCD16bpp.hpp>
#include <gui/home_screen/HomeView.hpp>
#include <gui/home_screen/HomePresenter.hpp>
#include <gui/menu_screen/MenuView.hpp>
#include <gui/menu_screen/MenuPresenter.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationView.hpp>
#include <gui/rtpcalibration_screen/RTPCalibrationPresenter.hpp>

using namespace touchgfx;

FrontendApplicationBase::FrontendApplicationBase(Model& m, FrontendHeap& heap)
    : touchgfx::MVPApplication(),
      transitionCallback(),
      frontendHeap(heap),
      model(m)
{
    touchgfx::HAL::getInstance()->setDisplayOrientation(touchgfx::ORIENTATION_LANDSCAPE);
    touchgfx::Texts::setLanguage(GB);
    reinterpret_cast<touchgfx::LCD16bpp&>(touchgfx::HAL::lcd()).enableTextureMapperAll();
    reinterpret_cast<touchgfx::LCD16bpp&>(touchgfx::HAL::lcd()).enableDecompressorL8_All();
    reinterpret_cast<touchgfx::LCD16bpp&>(touchgfx::HAL::lcd()).enableDecompressorRGB();
}

/*
 * Screen Transition Declarations
 */

// Home

void FrontendApplicationBase::gotoHomeScreenNoTransition()
{
    transitionCallback = touchgfx::Callback<FrontendApplicationBase>(this, &FrontendApplicationBase::gotoHomeScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &transitionCallback;
}

void FrontendApplicationBase::gotoHomeScreenNoTransitionImpl()
{
    touchgfx::makeTransition<HomeView, HomePresenter, touchgfx::NoTransition, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplicationBase::gotoHomeScreenSlideTransitionWest()
{
    transitionCallback = touchgfx::Callback<FrontendApplicationBase>(this, &FrontendApplicationBase::gotoHomeScreenSlideTransitionWestImpl);
    pendingScreenTransitionCallback = &transitionCallback;
}

void FrontendApplicationBase::gotoHomeScreenSlideTransitionWestImpl()
{
    touchgfx::makeTransition<HomeView, HomePresenter, touchgfx::SlideTransition<WEST>, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

// Menu

void FrontendApplicationBase::gotoMenuScreenSlideTransitionEast()
{
    transitionCallback = touchgfx::Callback<FrontendApplicationBase>(this, &FrontendApplicationBase::gotoMenuScreenSlideTransitionEastImpl);
    pendingScreenTransitionCallback = &transitionCallback;
}

void FrontendApplicationBase::gotoMenuScreenSlideTransitionEastImpl()
{
    touchgfx::makeTransition<MenuView, MenuPresenter, touchgfx::SlideTransition<EAST>, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

// RTPCalibration

void FrontendApplicationBase::gotoRTPCalibrationScreenNoTransition()
{
    transitionCallback = touchgfx::Callback<FrontendApplicationBase>(this, &FrontendApplicationBase::gotoRTPCalibrationScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &transitionCallback;
}

void FrontendApplicationBase::gotoRTPCalibrationScreenNoTransitionImpl()
{
    touchgfx::makeTransition<RTPCalibrationView, RTPCalibrationPresenter, touchgfx::NoTransition, Model >(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}
