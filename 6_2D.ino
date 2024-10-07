#include "ADC_DEAKIN.h"
#include "DHT.h"
#include "GPIO_DEAKIN.h"
#include "TIMER_DEAKIN.h"
#include <Arduino.h>

// Pin assignments
#define LED_PIN_1 5
#define LED_PIN_2 6
#define DHT_SENSOR_PIN 4

// Class instances
ADC_DEAKIN adcModule;
DHT dhtSensor(DHT_SENSOR_PIN, DHT22);
GPIO_DEAKIN gpioModule;
TIMER_DEAKIN timerModule;

// Constants for thermistor calculations
const float RESISTANCE_KNOWN = 10000.0;
const float INPUT_VOLTAGE = 3.3;
const float THERMISTOR_BETA = 3950;
const float REFERENCE_RESISTANCE = 10000.0;
const float REFERENCE_TEMP_K = 298.15;

// Sampling and averaging settings
const int SAMPLE_COUNT = 10;
float tempSamples[SAMPLE_COUNT];
float humiditySamples[SAMPLE_COUNT];
int sampleIdx = 0;
float avgTemp = 0;
float avgHumidity = 0;

// Timing variables
unsigned long lastSampleTime = 0;
unsigned long sampleInterval = 60000;
unsigned long alarmTime = 0;
int freqSetting = 6;

// Calculate resistance from ADC reading
float getThermistorResistance(int adcVal) {
    float outputVoltage = (adcVal * INPUT_VOLTAGE) / 1023.0;
    return (outputVoltage == 0) ? -1 : RESISTANCE_KNOWN * ((INPUT_VOLTAGE / outputVoltage) - 1);
}

// Convert resistance to temperature (Celsius)
float getTempCelsius(float resistance) {
    if (resistance <= 0) return NAN;
    return (1.0 / ((1.0 / REFERENCE_TEMP_K) + (1.0 / THERMISTOR_BETA) * log(resistance / REFERENCE_RESISTANCE))) - 273.15;
}

// Capture humidity reading
void captureHumidity() {
    float humidity = dhtSensor.readHumidity();
    if (isnan(humidity)) {
        Serial.println("Error reading humidity!");
        return;
    }
    humiditySamples[sampleIdx] = humidity;
}

// Capture temperature reading
void captureTemperature() {
    int adcReading;
    adcModule.read_ADC(&adcReading);
    float resistance = getThermistorResistance(adcReading);
    float temperature = getTempCelsius(resistance);

    if (temperature < 0) temperature += 22;
    else if (temperature > 0) temperature -= 18;

    tempSamples[sampleIdx] = isnan(temperature) ? 0 : temperature;
}

// Compute average values
void computeAverages() {
    float tempSum = 0;
    float humiditySum = 0;
    int validTempCount = 0;

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        if (tempSamples[i] >= 0) {
            tempSum += tempSamples[i];
            validTempCount++;
        }
        humiditySum += humiditySamples[i];
    }

    avgTemp = (validTempCount > 0) ? tempSum / validTempCount : 0;
    avgHumidity = humiditySum / SAMPLE_COUNT;

    Serial.print("Average Temperature: ");
    Serial.println(avgTemp);
    Serial.print("Average Humidity: ");
    Serial.println(avgHumidity);
}

// Control LED blinking
void controlLED(uint8_t pin, int blinkRate) {
    unsigned long blinkInterval = 1000 / blinkRate;
    gpioModule.digitalWrite(pin, HIGH);
    timerModule.start(blinkInterval);
    while (!timerModule.isComplete());
    gpioModule.digitalWrite(pin, LOW);
    timerModule.start(blinkInterval);
    while (!timerModule.isComplete());
}

// Check for alarm conditions
void monitorAlarms() {
    if (avgTemp < 4) {
        Serial.println("Temperature below 4°C - LED1 blinking at 10Hz");
        alarmTime = millis();
        for (int i = 0; i < 15; ++i) controlLED(LED_PIN_1, 10);
    } else if (avgTemp < 10) {
        Serial.println("Temperature below 10°C - LED1 blinking at 1Hz");
        alarmTime = millis();
        for (int i = 0; i < 15; ++i) controlLED(LED_PIN_1, 1);
    }

    if (avgHumidity < 30) {
        Serial.println("Humidity below 30% - LED2 ON");
        alarmTime = millis();
        gpioModule.digitalWrite(LED_PIN_2, HIGH);
        timerModule.start(15000);
        while (!timerModule.isComplete());
    } else if (avgHumidity < 50) {
        Serial.println("Humidity below 50% - LED2 blinking at 0.5Hz");
        alarmTime = millis();
        for (int i = 0; i < 15; ++i) controlLED(LED_PIN_2, 2);
    }
}

// Menu for user interaction
void displayMenu() {
    Serial.println("\nMenu:");
    Serial.println("a - Show current sensor data");
    Serial.println("b - Show alarm status");
    Serial.println("Enter a number between 1 and 30 to set frequency");
    Serial.print("Enter choice: ");
}

// Process user input and block until valid input is received
void processMenuInput() {
    while (!Serial.available()); // Wait until input is available

    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.equalsIgnoreCase("a")) {
        Serial.print("Current Temp: ");
        Serial.println(tempSamples[sampleIdx]);
        Serial.print("Current Humidity: ");
        Serial.println(humiditySamples[sampleIdx]);
    } else if (input.equalsIgnoreCase("b")) {
        Serial.print("Alarm activated at: ");
        Serial.println(alarmTime);
    } else {
        int enteredFreq = input.toInt();
        if (enteredFreq >= 1 && enteredFreq <= 30) {
            freqSetting = enteredFreq;
            sampleInterval = freqSetting * 1000;
            Serial.print("Frequency set to: ");
            Serial.println(freqSetting);
        } else {
            Serial.println("Invalid input. Please enter a valid option.");
            processMenuInput(); // Recur until valid input is received
        }
    }
}

void setup() {
    Serial.begin(9600);
    while (!Serial);
    dhtSensor.begin();
    adcModule.setup_ADC('A', 3, 10, 0);
    adcModule.enable_ADC();
    gpioModule.pinMode(LED_PIN_1, OUTPUT);
    gpioModule.pinMode(LED_PIN_2, OUTPUT);
    timerModule.init();
    Serial.println("System initialized.");
}

void loop() {
    displayMenu();
    processMenuInput(); // Ensure valid input before proceeding

    unsigned long currentTime = millis();
    if (lastSampleTime == 0 || currentTime - lastSampleTime >= sampleInterval) {
        lastSampleTime = currentTime;

        for (int i = 0; i < SAMPLE_COUNT; ++i) {
            captureTemperature();
            captureHumidity();
            sampleIdx = (sampleIdx + 1) % SAMPLE_COUNT;

            for (int j = 0; j < freqSetting; ++j) {
                timerModule.start(1000);
                while (!timerModule.isComplete());
            }
        }

        computeAverages();
        monitorAlarms();
    }
}
