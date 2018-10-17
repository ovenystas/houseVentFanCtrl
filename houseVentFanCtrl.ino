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


#define NUMBER_OF_RELAYS 6
#define EEPROM_ADDRESS 0
#define CHECKSUM_ADDRESS EEPROM_ADDRESS + 1

#define TEMP_HYSTERESIS_DEFAULT 10      // In 10th of degC
#define HUM_HYSTERESIS_DEFAULT 10       // In 10th of %
#define READ_INTERVAL_DEFAULT_MS 30000  // Read every 30 seconds

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
  READ_INTERVAL_DEFAULT_MS,
};


// Variables
DHT dht22(16, DHT22);
int16_t temperature = 0;      // In 10th of Celsius.
int16_t sentTemperature = 0;  // In 10th of Celsius.
int16_t humidity = 0;         // In 10th of percent.
int16_t sentHumidity = 0;     // In 10th of percent.

// Variables for parameters.
uint16_t parameters[PARAM_MAX] = {0};


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
    updateChecksum();
  }
}


void loop(void)
{
  // Empty. Functions getter() and setter() are called from Z-Uno.
  static uint32_t lastMillis = 0;
  
  uint32_t currentMillis = millis();
  if (currentMillis - lastMillis >= parameters[PARAM_READ_INTERVAL])
  {
    lastMillis = currentMillis;
     
    temperature = dht22.readTemperatureC10();
    humidity = dht22.readHumidityH10();

    if (abs(temperature - sentTemperature) >= parameters[PARAM_TEMP_HYSTERESIS])
    {
      zunoSendReport(CH_TEMP);
      sentTemperature = temperature;
    }

    if (abs(humidity - sentHumidity) >= parameters[PARAM_HUM_HYSTERESIS])
    {
      zunoSendReport(CH_HUM);
      sentHumidity = humidity;
    }
  }
}


void setDefaultParameters()
{
  memcpy(parameters, parametersDefault, sizeof(parameters));
}

  
void loadParameters()
{
  for (int8_t i = 0; i < PARAM_MAX; i++)
  {
    zunoLoadCFGParam(PARAMETER_FIRST_NUMBER + i, &parameters[i]);
  }
}


void saveParameters()
{
  for (int8_t i = 0; i < PARAM_MAX; i++)
  {
    zunoSaveCFGParam(PARAMETER_FIRST_NUMBER + i, &parameters[i]);
  }
}


void updateChecksum(void)
{
  uint8_t checksum = 0;
  for (int8_t i = 0; i < PARAM_MAX; i++)
  {
    checksum += highByte(parameters[i]);
    checksum += lowByte(parameters[i]);
  }

  EEPROM.update(CHECKSUM_ADDRESS, checksum);
}


// Returns true if checksum is OK
bool verifyChecksum()
{
  uint8_t checksum = 0;
  for (int8_t i = 0; i < (PARAM_MAX * 2); i++)
  {
    checksum += EEPROM.read(PARAMETER_START_EEPROM_ADDRESS + i);
  }

  return (checksum == EEPROM.read(CHECKSUM_ADDRESS));
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
uint8_t getterFan(void)
{
  return EEPROM.read(EEPROM_ADDRESS);
}


// Sets new relay state depending on new speed percentage value of 0-99%.
void setterFan(uint8_t newValue)
{
  // Set the relays according to new speed value.
  setSpeedRelays(convertSpeed(newValue));
    
  // Save the new value in EEPROM to survive power outage.
  EEPROM.update(EEPROM_ADDRESS, newValue);
}


uint16_t getterTemperature()
{
  return temperature;
}


uint16_t getterHumidity()
{
  return humidity;
}


void configParameterChanged(uint8_t param, uint16_t* value)
{
  uint8_t paramNumber = param - PARAMETER_FIRST_NUMBER;
  if (paramNumber >= PARAM_MAX)
  {
    return;
  }

  parameters[paramNumber] = *value;

  updateChecksum();
}
