/*********************************************************************************/
/********** THIS FILE IS GENERATED BY TOUCHGFX DESIGNER, DO NOT MODIFY ***********/
/*********************************************************************************/
#include <gui_generated/menu_screen/MenuViewBase.hpp>
#include <touchgfx/Color.hpp>
#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>

MenuViewBase::MenuViewBase() :
    buttonCallback(this, &MenuViewBase::buttonCallbackHandler)
{
    __background.setPosition(0, 0, 800, 480);
    __background.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    add(__background);

    box1.setPosition(0, 0, 800, 480);
    box1.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    add(box1);

    buttonWithLabel1.setXY(280, 38);
    buttonWithLabel1.setBitmaps(touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_MEDIUM_ROUNDED_NORMAL_ID), touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_MEDIUM_ROUNDED_PRESSED_ID));
    buttonWithLabel1.setLabelText(touchgfx::TypedText(T___SINGLEUSE_OY0J));
    buttonWithLabel1.setLabelColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    buttonWithLabel1.setLabelColorPressed(touchgfx::Color::getColorFromRGB(255, 255, 255));
    buttonWithLabel1.setAction(buttonCallback);
    add(buttonWithLabel1);

    RTPCalibrationBtn.setXY(313, 387);
    RTPCalibrationBtn.setBitmaps(touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_SMALL_ROUNDED_DISABLED_ID), touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_SMALL_ROUNDED_DISABLED_ID));
    RTPCalibrationBtn.setLabelText(touchgfx::TypedText(T___SINGLEUSE_8KPD));
    RTPCalibrationBtn.setLabelColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    RTPCalibrationBtn.setLabelColorPressed(touchgfx::Color::getColorFromRGB(255, 255, 255));
    RTPCalibrationBtn.setAction(buttonCallback);
    add(RTPCalibrationBtn);
}

MenuViewBase::~MenuViewBase()
{

}

void MenuViewBase::setupScreen()
{

}

void MenuViewBase::buttonCallbackHandler(const touchgfx::AbstractButton& src)
{
    if (&src == &buttonWithLabel1)
    {
        //Interaction1
        //When buttonWithLabel1 clicked change screen to Home
        //Go to Home with screen transition towards West
        application().gotoHomeScreenSlideTransitionWest();
    }
    if (&src == &RTPCalibrationBtn)
    {
        //Interaction2
        //When RTPCalibrationBtn clicked change screen to RTPCalibration
        //Go to RTPCalibration with no screen transition
        application().gotoRTPCalibrationScreenNoTransition();
    }
}
