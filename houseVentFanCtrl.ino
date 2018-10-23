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
 * 
 * There is also a DHT22 sensor on board which provides a measurement of the relative humidity and
 * the temperature.
 */

#include "EEPROM.h"
#include "ZUNO_DHT.h"

#define DEBUG
#include <Debug.h>

#define NUMBER_OF_RELAYS 6
#define EEPROM_ADDRESS 0
#define CHECKSUM_ADDRESS EEPROM_ADDRESS + 1

#define TEMP_HYSTERESIS_DEFAULT 10      // In 10th of degC
#define HUM_HYSTERESIS_DEFAULT 10       // In 10th of %
#define READ_INTERVAL_DEFAULT_S 30  // Read every 30 seconds
#define READ_INTERVAL_MIN 30

#define PARAMETER_START_EEPROM_ADDRESS 0x2000
#define PARAMETER_MAX_NUMBER_OF 32
#define PARAMETER_FIRST_NUMBER 64


typedef enum
{
  CH_FAN = 1,
  CH_TEMP,
  CH_HUM,
} channelType_t;

typedef enum
{
  PARAM_TEMP_HYSTERESIS = 0,
  PARAM_HUM_HYSTERESIS,
  PARAM_READ_INTERVAL,
  PARAM_MAX,
} param_t;


// Constants
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

const uint16_t parametersDefault[PARAM_MAX] =
{
  TEMP_HYSTERESIS_DEFAULT,
  HUM_HYSTERESIS_DEFAULT,
  READ_INTERVAL_DEFAULT_S,
};


// Variables
DHT dht22(16, DHT22);
int16_t temperature = 0;      // In 10th of Celsius.
int16_t sentTemperature = 0;  // In 10th of Celsius.
int16_t humidity = 0;         // In 10th of percent.
int16_t sentHumidity = 0;     // In 10th of percent.
uint8_t fanSpeed = 0;         // In percent
uint16_t parameters[PARAM_MAX] = {0};
bool pendingCfgParamSave = false;

// Z-Uno setup macros.
ZUNO_SETUP_DEBUG_MODE(DEBUG_ON);

ZUNO_SETUP_CFGPARAMETER_HANDLER(configParameterChanged);

// Setup the Z-Wave channel for Z-Uno.
ZUNO_SETUP_CHANNELS(
  ZUNO_SWITCH_MULTILEVEL(
    getterFan,
    setterFan
  ),
  ZUNO_SENSOR_MULTILEVEL(
    ZUNO_SENSOR_MULTILEVEL_TYPE_TEMPERATURE,
    SENSOR_MULTILEVEL_SCALE_CELSIUS,
    SENSOR_MULTILEVEL_SIZE_TWO_BYTES,
    SENSOR_MULTILEVEL_PRECISION_ONE_DECIMAL,
    getterTemperature
  ),
  ZUNO_SENSOR_MULTILEVEL(
    ZUNO_SENSOR_MULTILEVEL_TYPE_RELATIVE_HUMIDITY,
    SENSOR_MULTILEVEL_SCALE_PERCENTAGE_VALUE,
    SENSOR_MULTILEVEL_SIZE_TWO_BYTES,
    SENSOR_MULTILEVEL_PRECISION_ONE_DECIMAL,
    getterHumidity
  )
);


void setup(void)
{
  // Setup relay pins to output and relay off.
  for (uint8_t i = 0; i < NUMBER_OF_RELAYS; i++)
  {
    pinMode(RELAY_PIN[i], OUTPUT);
    digitalWrite(RELAY_PIN[i], HIGH);
  }

  DBG_BEGIN();
  
  // Retreive last set value from EEPROM and reset relays according to it.
  setterFan(EEPROM.read(EEPROM_ADDRESS));

  dht22.begin();

  if (verifyChecksum())
  {
    loadParameters();
  }
  else
  {
    setDefaultParameters();
    saveParameters();
  }
}


void loop(void)
{
  // Empty. Functions getter() and setter() are called from Z-Uno.
  static uint32_t lastMillis = 0;
  
  uint32_t currentMillis = millis();
  if (currentMillis - lastMillis >= (parameters[PARAM_READ_INTERVAL] * 1000))
  {
    lastMillis = currentMillis;
     
    temperature = dht22.readTemperatureC10();
    humidity = dht22.readHumidityH10();

    DBG_PRINTV(temperature);
    DBG_PRINTV(humidity);
    
    if (abs(temperature - sentTemperature) >= parameters[PARAM_TEMP_HYSTERESIS])
    {
      DBG_PRINT("Sending temperature");
      zunoSendReport(CH_TEMP);
      sentTemperature = temperature;
    }

    if (abs(humidity - sentHumidity) >= parameters[PARAM_HUM_HYSTERESIS])
    {
      DBG_PRINT("Sending humidity");
      zunoSendReport(CH_HUM);
      sentHumidity = humidity;
    }

    // Debug
    verifyChecksum();
  }

  if (pendingCfgParamSave)
  {
    saveParameters();
    pendingCfgParamSave = false;
  }
}


void setDefaultParameters()
{
  memcpy(parameters, parametersDefault, sizeof(parameters));
}

  
void loadParameters()
{
  DBG_PRINT("Loading parameters");
  for (int8_t i = 0; i < PARAM_MAX; i++)
  {
    zunoLoadCFGParam(PARAMETER_FIRST_NUMBER + i, &parameters[i]);
  }
}


void saveParameters(void)
{
  DBG_PRINT("Saving parameters");
  uint8_t checksum = 0;
  for (int8_t i = 0; i < PARAM_MAX; i++)
  {
    checksum += highByte(parameters[i]);
    checksum += lowByte(parameters[i]);
    zunoSaveCFGParam(PARAMETER_FIRST_NUMBER + i, &parameters[i]);
  }
  DBG_PRINTX(checksum);
  EEPROM.update(CHECKSUM_ADDRESS, checksum);
}


// Returns true if checksum is OK
bool verifyChecksum(void)
{
  uint8_t checksum = 0;
  for (int8_t i = 0; i < (PARAM_MAX * 2); i++)
  {
    checksum += EEPROM.read(PARAMETER_START_EEPROM_ADDRESS + i);
  }

  uint8_t storedChecksum = EEPROM.read(CHECKSUM_ADDRESS);
  DBG_PRINTX(checksum);
  DBG_PRINTX(storedChecksum);
  return (checksum == storedChecksum);
}


// Set relays according to relayTable depending on speedValue.
// Valid range of speedValue is 0-5.
void setSpeedRelays(uint8_t speedValue)
{
  DBG_PRINTV(speedValue);
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
uint8_t getterFan(void)
{
  return fanSpeed;
}


// Sets new relay state depending on new speed percentage value of 0-99%.
void setterFan(uint8_t newValue)
{
  DBG_PRINTMSGV("Setting fan to speed%", newValue);
  fanSpeed = newValue;

  // Set the relays according to new speed value.
  setSpeedRelays(convertSpeed(newValue));
    
  // Save the new value in EEPROM to survive power outage.
  EEPROM.update(EEPROM_ADDRESS, newValue);
}


uint16_t getterTemperature(void)
{
  return temperature;
}


uint16_t getterHumidity(void)
{
  return humidity;
}


// Handler which is called whenever a configuration parameter change call comes in through Z-Wave.
// Configuration parameters are automatically saved to the EEPROM.
void configParameterChanged(uint8_t param, uint16_t* value_p)
{
  uint16_t value = *value_p;

  DBG_PRINTV(param);
  DBG_PRINTV(value);
  
  uint8_t paramNumber = param - PARAMETER_FIRST_NUMBER;
  if (paramNumber >= PARAM_MAX)
  {
    DBG_PRINTMSGV("Param out of range", param);
    return;
  }

  if (paramNumber == PARAM_READ_INTERVAL && value < READ_INTERVAL_MIN)
  {
    value = READ_INTERVAL_MIN;
  }

  parameters[paramNumber] = value;

  // Save new parameters to EEPROM and update checksum when returning to loop() function.
  // Can't be done here since input value is stored to EEPROM by caller after this function returns.
  pendingCfgParamSave = true;
}
