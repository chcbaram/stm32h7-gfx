/*********************************************************************************/
/********** THIS FILE IS GENERATED BY TOUCHGFX DESIGNER, DO NOT MODIFY ***********/
/*********************************************************************************/
#include <gui_generated/screen_screen/screenViewBase.hpp>
#include <touchgfx/Color.hpp>
#include <images/BitmapDatabase.hpp>
#include <texts/TextKeysAndLanguages.hpp>

screenViewBase::screenViewBase() :
    buttonCallback(this, &screenViewBase::buttonCallbackHandler)
{
    __background.setPosition(0, 0, 480, 480);
    __background.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    add(__background);

    box1.setPosition(0, 0, 480, 480);
    box1.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    add(box1);

    buttonWithLabel1.setXY(120, 372);
    buttonWithLabel1.setBitmaps(touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_MEDIUM_ROUNDED_NORMAL_ID), touchgfx::Bitmap(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_BUTTON_REGULAR_HEIGHT_50_MEDIUM_ROUNDED_PRESSED_ID));
    buttonWithLabel1.setLabelText(touchgfx::TypedText(T___SINGLEUSE_SR1N));
    buttonWithLabel1.setLabelColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    buttonWithLabel1.setLabelColorPressed(touchgfx::Color::getColorFromRGB(255, 255, 255));
    buttonWithLabel1.setAction(buttonCallback);
    add(buttonWithLabel1);

    analogClock1.setXY(120, 110);
    analogClock1.setBackground(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_ANALOGCLOCK_BACKGROUNDS_SMALL_PLAIN_DARK_ID, 120, 120);
    analogClock1.setupSecondHand(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_ANALOGCLOCK_HANDS_SMALL_SECOND_PLAIN_DARK_ID, 2, 100);
    analogClock1.setupMinuteHand(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_ANALOGCLOCK_HANDS_SMALL_MINUTE_PLAIN_DARK_ID, 10, 87);
    analogClock1.setMinuteHandSecondCorrection(false);
    analogClock1.setupHourHand(BITMAP_ALTERNATE_THEME_IMAGES_WIDGETS_ANALOGCLOCK_HANDS_SMALL_HOUR_PLAIN_DARK_ID, 9, 69);
    analogClock1.setHourHandMinuteCorrection(false);
    analogClock1.initializeTime24Hour(10, 10, 0);
    add(analogClock1);

    textArea1.setXY(142, 33);
    textArea1.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    textArea1.setLinespacing(0);
    textArea1.setTypedText(touchgfx::TypedText(T___SINGLEUSE_3B28));
    add(textArea1);
}

screenViewBase::~screenViewBase()
{

}

void screenViewBase::setupScreen()
{

}

void screenViewBase::buttonCallbackHandler(const touchgfx::AbstractButton& src)
{
    if (&src == &buttonWithLabel1)
    {
        //Interaction1
        //When buttonWithLabel1 clicked change screen to Screen1
        //Go to Screen1 with screen transition towards East
        application().gotoScreen1ScreenSlideTransitionEast();
    }
}
