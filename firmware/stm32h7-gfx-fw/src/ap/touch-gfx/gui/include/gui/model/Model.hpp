#ifndef MODEL_HPP
#define MODEL_HPP

#ifndef SIMULATOR
#include "ap.h"
#include "touchgfx/Utils.hpp"
#endif

extern "C" {
    #include "ap.h"
    #include "touch/ak4183.h"
}

#define LOG                   1
#define MAX_ADC_CNT         120
#define PRESSED_LATENCY     300

class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();
    void getCalibrationStep(uint8_t step);

protected:
    ModelListener* modelListener;
};

#endif // MODEL_HPP
