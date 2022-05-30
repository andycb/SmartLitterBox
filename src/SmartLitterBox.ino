#include "SmartLitterBox.h"

SmartLitterBox::SmartLitterBox() 
{
    this->m_litterScale = LitterScale();
    
    MethodSlot<SmartLitterBox, float> memFunSlot(this, &SmartLitterBox::OnWeightUpdated);
    this->m_litterScale.WeightChanged.attach(memFunSlot);
}

void SmartLitterBox::Tick() 
{
    if (this->m_catOnScale) 
    {
        if (this->m_endTime > 0 && millis() - this->m_startTime > 7 * 60 * 1000)
        {
            // The cat has been on the scale for a very long time, its most likely something is on the litter box, 
            // or it has been refilled with fresh litter. Whatever, its not a pooping cat, just reset and zero.
            this->m_catOnScale = false;
            this->m_startReading = -1.f;
            this->m_lastStableReading = 0;
            this->m_litterScale.Tare();

            Serial.println("Timeout. Resetting.");
        }
        else if (this->m_endTime > 0 && millis() - this->m_endTime > 60 * 1000) 
        {
            Serial.println("Confirmed cat off");
            this->LitterUsage.fire(this->m_pendingLitterUse);

            // Reset
            this->m_catOnScale = false;
            this->m_startReading = -1.f;
            this->m_lastStableReading = 0;
            this->m_litterScale.Tare();
            this->m_endTime = -1;
        }
    }
    
    this->m_litterScale.Tick();
}

void SmartLitterBox::OnWeightUpdated(float weight)
{
    if (this->m_initalized && !this->m_catOnScale && weight - this->m_lastStableReading > 0.5) 
    {
        // Sudden jump means a cat may be on the scale
        this->m_catOnScale = true;
        
        this->m_startReading = weight;
        this->m_startTime = millis();
        Serial.println("Cat on");
    }
    else if (this->m_initalized && this->m_catOnScale && this->m_startReading - weight > 0.5) 
    {
        auto duration = (millis() - this->m_startTime) / 1000;
        auto poopWeight = weight;
        auto catWeight = this->m_startReading - poopWeight;

        if (poopWeight < 0.005) {
            poopWeight = 0.f;
        }

        CatLitterUse result;
        result.Duration = duration;
        result.CatWeight = catWeight;
        result.PoopWeight = poopWeight;

        this->m_endTime = millis();
        this->m_pendingLitterUse = result;
        Serial.println("Cat off. Waiting....");
    }
    else if (this->m_catOnScale) 
    {
        // Update the start weight to be the heaviest stable weight we see while the cat is on the scale.
        // This is because the cat may put their feet on the edge of the litter before jumping up, so the start reading may be too low. 
        if (weight > this->m_startReading) 
        {
            this->m_startReading = weight;
            Serial.println("Updated cat weight");
        }

        this->m_endTime = millis();
    }
    else 
    {
        delay(500);
        Serial.println("Auto tare");
        
        // Zero the scale to handle the slow drift of the sensors, or small changes like the litter being raked.
        this->m_litterScale.Tare();
        weight = 0.f;
    }

    this->m_initalized = true;
    this->m_lastStableReading = weight;
}
