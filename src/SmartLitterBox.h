#pragma once
#include "LitterScale.h"
#include <Callback.h>

struct CatLitterUse
{
    int Duration;
    float CatWeight;
    float PoopWeight;
};

class SmartLitterBox 
{
    public:
        SmartLitterBox();
        void Tick();
        Signal<CatLitterUse> LitterUsage;

    private:
        LitterScale m_litterScale;

        float m_lastStableReading = -1.f;
        float m_startReading = -1.f;
        int m_startTime = -1;
        int m_endTime = -1;
        CatLitterUse m_pendingLitterUse;
        bool m_catOnScale = false;
        bool m_initalized = false;

        void OnWeightUpdated(float weight);
};
