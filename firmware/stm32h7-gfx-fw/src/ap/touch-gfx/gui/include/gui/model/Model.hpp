#ifndef MODEL_HPP
#define MODEL_HPP

#ifndef SIMULATOR
#include "ap.h"
#include "touchgfx/Utils.hpp"
#endif



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
