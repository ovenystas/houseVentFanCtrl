/*
 * Title: Z-Wave whole house ventilation fan speed controller
 * Author: Ove Nystas
 * Date: 2018-10-03
 * 
 * Control the speed of a whole house ventilation fan with 6 hard speed values.
 * Off - very low - low - mid - high - full.
 * Speed is controlled via relays by connecting one of five outputs from a transformer to the fan motor.
 * Five out of six relays on a six relay module board are used.
 * Speed is set remotely via Z-Wave protocol using a Z-Uno Arduino board.
 * Last set speed is remembered and used to reset relays to same state after a power outage.
 */

#include "EEPROM.h"


#define NUMBER_OF_RELAYS 6
#define EEPROM_ADDRESS 0x0000


// Pin numbers, where the six relays are connected (avoiding standard LED pin)
const uint8_t RELAY_PIN[NUMBER_OF_RELAYS] = {9, 10, 11, 12, 14, 15 };

// Relays are off when HIGH, ROW = Fan speed, COLUMN = Relay number
const uint8_t relayTable[6][5] =
{
  // 1     2     3     4     5
  { HIGH, HIGH, HIGH, HIGH, HIGH },  // Off
  { LOW,  HIGH, HIGH, HIGH, HIGH },  // Very low speed
  { HIGH, HIGH, HIGH, HIGH, LOW  },  // Low speed
  { HIGH, LOW,  HIGH, HIGH, LOW  },  // Mid speed
  { HIGH, HIGH, HIGH, LOW,  LOW  },  // High speed
  { HIGH, HIGH, LOW,  LOW,  LOW  },  // Full speed
};


// Setup the Z-Wave channel for Z-Uno.
ZUNO_SETUP_CHANNELS(ZUNO_SWITCH_MULTILEVEL(getter, setter));


void setup(void)
{
  // Setup relay pins to output and relay off.
  for (uint8_t i = 0; i < NUMBER_OF_RELAYS; i++)
  {
    pinMode(RELAY_PIN[i], OUTPUT);
    digitalWrite(RELAY_PIN[i], HIGH);
  }

  // Retreive last set value from EEPROM and reset relays according to it.
  setter(EEPROM.read(EEPROM_ADDRESS));
}


void loop(void)
{
  // Empty. Functions getter() and setter() are called from Z-Uno.
}


// Set relays according to relayTable depending on speedValue.
// Valid range of speedValue is 0-5.
void setSpeedRelays(uint8_t speedValue)
{
  if (speedValue <= 5)
  {
    for (uint8_t i = 0; i < (NUMBER_OF_RELAYS - 1); i++)
    {
      digitalWrite(RELAY_PIN[i], relayTable[speedValue][i]);
    }
  }
}

// Convert speed from percantage 0-99% to a value in range 0-5.
// 0% = 0, Off
// 1-20% = 1, Very low speed
// 21-40% = 2, Low speed
// 41-60% = 3, Mid speed
// 61-80% = 4, High speed
// 80-99% = 5, Full speed
uint8_t convertSpeed(uint8_t percentage)
{
  if (percentage > 80) return 5;
  if (percentage > 60) return 4;
  if (percentage > 40) return 3;
  if (percentage > 20) return 2;
  if (percentage > 0) return 1;
  return 0;
}


// Gets the current speed percentage value.
uint8_t getter(void)
{
  return EEPROM.read(EEPROM_ADDRESS);
}


// Sets new relay state depending on new speed percentage value of 0-99%.
void setter(uint8_t newValue)
{
  // Set the relays according to new speed value.
  setSpeedRelays(convertSpeed(newValue));
    
  // Save the new value in EEPROM to survive power outage.
  EEPROM.update(EEPROM_ADDRESS, newValue);
}
