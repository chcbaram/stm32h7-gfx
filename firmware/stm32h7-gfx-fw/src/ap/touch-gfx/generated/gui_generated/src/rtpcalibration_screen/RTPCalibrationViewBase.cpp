/*********************************************************************************/
/********** THIS FILE IS GENERATED BY TOUCHGFX DESIGNER, DO NOT MODIFY ***********/
/*********************************************************************************/
#include <gui_generated/rtpcalibration_screen/RTPCalibrationViewBase.hpp>
#include <touchgfx/Color.hpp>
#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>

RTPCalibrationViewBase::RTPCalibrationViewBase() :
    buttonCallback(this, &RTPCalibrationViewBase::buttonCallbackHandler)
{
    __background.setPosition(0, 0, 800, 480);
    __background.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    add(__background);

    Background.setPosition(0, 0, 800, 480);
    Background.setColor(touchgfx::Color::getColorFromRGB(130, 130, 130));
    add(Background);

    TouchPoint1.setXY(73, 73);
    TouchPoint1.setBitmap(touchgfx::Bitmap(BITMAP__CROSS_ID));
    add(TouchPoint1);

    TouchPoint2.setXY(73, 383);
    TouchPoint2.setBitmap(touchgfx::Bitmap(BITMAP__CROSS_ID));
    add(TouchPoint2);

    TouchPoint3.setXY(683, 383);
    TouchPoint3.setBitmap(touchgfx::Bitmap(BITMAP__CROSS_ID));
    add(TouchPoint3);

    TouchPoint4.setXY(683, 73);
    TouchPoint4.setBitmap(touchgfx::Bitmap(BITMAP__CROSS_ID));
    add(TouchPoint4);

    TouchPoint5.setXY(383, 223);
    TouchPoint5.setBitmap(touchgfx::Bitmap(BITMAP__CROSS_ID));
    add(TouchPoint5);

    CalibrationCancelBtn.setXY(345, 400);
    CalibrationCancelBtn.setBitmaps(touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_TINY_ROUND_DISABLED_ID), touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_TINY_ROUND_DISABLED_ID));
    CalibrationCancelBtn.setLabelText(touchgfx::TypedText(T___SINGLEUSE_TXX3));
    CalibrationCancelBtn.setLabelColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    CalibrationCancelBtn.setLabelColorPressed(touchgfx::Color::getColorFromRGB(255, 255, 255));
    CalibrationCancelBtn.setAction(buttonCallback);
    add(CalibrationCancelBtn);

    boxWithBorder1.setPosition(256, 11, 289, 50);
    boxWithBorder1.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    boxWithBorder1.setBorderColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    boxWithBorder1.setBorderSize(0);
    add(boxWithBorder1);

    textArea1.setPosition(256, 14, 289, 44);
    textArea1.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    textArea1.setLinespacing(0);
    Unicode::snprintf(textArea1Buffer, TEXTAREA1_SIZE, "%s", touchgfx::TypedText(T___SINGLEUSE_L5J5).getText());
    textArea1.setWildcard(textArea1Buffer);
    textArea1.setTypedText(touchgfx::TypedText(T___SINGLEUSE_VVAJ));
    add(textArea1);
}

RTPCalibrationViewBase::~RTPCalibrationViewBase()
{

}

void RTPCalibrationViewBase::setupScreen()
{

}

void RTPCalibrationViewBase::buttonCallbackHandler(const touchgfx::AbstractButton& src)
{
    if (&src == &CalibrationCancelBtn)
    {
        //BackPrevScreen
        //When CalibrationCancelBtn clicked call calibrationPointPressed on RTPCalibration
        //Call calibrationPointPressed
        calibrationPointPressed(5);
    }
}
