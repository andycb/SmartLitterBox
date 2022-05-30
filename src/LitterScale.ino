#include "LitterScale.h"

LitterScale::LitterScale()
{
    this->m_scale_A.begin(LOADCELL_DOUT_PIN_A, LOADCELL_SCK_PIN_A);
    this->m_scale_A.set_scale(CALIBRATION_FACTOR_A);
    this->m_scale_A.set_offset(ZERO_FACTOR_A);
    this->m_scale_A.tare();

    this->m_scale_B.begin(LOADCELL_DOUT_PIN_B, LOADCELL_SCK_PIN_B);
    this->m_scale_B.set_scale(CALIBRATION_FACTOR_B);
    this->m_scale_B.set_offset(ZERO_FACTOR_B);
    this->m_scale_B.tare();
}

void LitterScale::Tick()
{
  float reading = this->GetReading();
  
  if (abs(m_lastReading - reading) < 0.05)
  {
      if (!this->m_isStable)
      {
        this->m_isStable = true;

        Serial.println("Weight Stable");
        this->WeightChanged.fire(reading);
      }
  }
  else if (this->m_isStable)
  {
    Serial.println("Weight not Stable");
    this->m_isStable = false;
  }

  this->m_lastReading = reading;
}

void LitterScale::Tare()
{  
    this->m_scale_A.tare();
    this->m_scale_B.tare(); 

    this->m_lastReading = 0.f;
}

float LitterScale::GetReading()
{
    float readings_a = 0.f;;
    float readings_b = 0.f;

    for (auto i = 0; i < this->SCALE_SAMPLE_COUNT; ++i)
    {
        readings_a += m_scale_A.get_units();
        readings_b += m_scale_B.get_units() * -1;
    }

    return (readings_a + readings_b) / this->SCALE_SAMPLE_COUNT;
}
