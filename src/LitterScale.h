#pragma once
#include "HX711.h"
#include <Callback.h>
    
class LitterScale 
{
    public: 
        void Tick();
        void Tare();
        LitterScale();

        Signal<float> WeightChanged;
    
    private:
        const float ZERO_FACTOR_A = -97991.f;
        const float CALIBRATION_FACTOR_A = -74500.00f;
        const float ZERO_FACTOR_B = 140274.f;
        const float CALIBRATION_FACTOR_B = -99500.00f;

        const int SCALE_SAMPLE_COUNT = 20;
        const int LOADCELL_DOUT_PIN_A = 2;
        const int LOADCELL_SCK_PIN_A = 16;
        const int LOADCELL_DOUT_PIN_B = 13;
        const int LOADCELL_SCK_PIN_B = 15;

        HX711 m_scale_A;
        HX711 m_scale_B;

        bool m_isStable;
        float m_lastReading = -1.f;

        float GetReading();
};
