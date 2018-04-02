#include "Adafruit_BluefruitLE_SPI.h"
#include <SPI.h>

#define Vcc 5
#define UPDATE_COMMAND_CHARSIZE 20
#define FACTORYRESET_ENABLE      0

#define BUFSIZE                        160   // Size of the read buffer for incoming data
#define VERBOSE_MODE                   true  // If set to 'true' enables debug output

#define BLUEFRUIT_SPI_CS               10
#define BLUEFRUIT_SPI_IRQ              7
#define BLUEFRUIT_SPI_RST              8    // Optional but recommended, set to -1 if unused



enum state_name {
  idle_state = 0,
  connect_state,
  adc_state,
  update_state
};

volatile int Aread;
volatile float Aread_V;
volatile uint32_t distance;
volatile unsigned char state;

uint32_t s_ID;
uint32_t c_ID;
// string to store update command
char updateCommand[UPDATE_COMMAND_CHARSIZE];


// Adafruit provided library to interact with lower level SPI and messging between Bluefruit.
// 3 parameters: CS, IRQ, RST(optional)
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

// A small helper
void error(const __FlashStringHelper*err) {
  Serial.println(err);
  while (1);
}

// Interrupt for ADC conversion completion
ISR(ADC_vect){
  // Read low register first and then 2 bits from high register
  Aread = ADCL | (ADCH << 8);
  Aread_V = float(Aread * Vcc) / 1024; // Calculate real voltage level
  distance = 26.927 * pow(Aread_V, -1.197); // Calculate distance from voltage, formula extracted from data points using excel treadline
  // Remove unstable values
  if (distance <= 10){
    distance = 0;
  }
  // Move to update state
  state = update_state;
}

void setup() {
  cli(); // Disable global interrupt

  Serial.begin(115200);
  Serial.println(F("Beginnging distance sensor project"));

  // Setup BLE
  ble_setup();

  // configure ADC register
  adc_setup();

    // Initialize distance val
  distance = 0;
  state = connect_state;
  
  sei(); // Enable global interrupt;
}

void loop() {
  switch (state){
    case idle_state:
      // Do nothing
      break;
      
    case connect_state:
      Serial.println(F("Waiting for bluetooth device to connect..."));
      
      // Change state only when ble is connected
      if (ble.isConnected()){
        Serial.println(F("Bluetooth device connected."));
        state = adc_state;
      }
      delay(2000);
      break;
      
    case adc_state:
      Serial.println(F("Start sampling ADC value... and go back to idle"));
      // After enablig adc, swithc to idle state to wait for ADC interrupt
      adc_start();
      state = idle_state;
      break;
      
    case update_state:
      // Send update to BLE device and go back to sampling ADC
      setCharacteristicToValue(c_ID, distance);
      state = adc_state;
      break;

    default:
      break;
  }
}

// Setup bluetooth device and
// Add service and characteristics to transmit distance sensor over BLE
// Service: 
void ble_setup(){
    // Begin setting up bluefruit
  if (!ble.begin(VERBOSE_MODE)){
    error(F("Can't initialized bluefruit..."));
  }

    /* Disable command echo from Bluefruit */
  ble.echo(false);

  Serial.println("Requesting Bluefruit info:");
  /* Print Bluefruit information */
  ble.info();

  // Set device name to BLE-DistanceSensor
  if (!ble.atcommand(F("AT+GAPDEVNAME=BLE-DistanceSensor"))) {
    error(F("Failed to set device name"));
  }
  Serial.println(F("Set BLE device name to 'BLE-DistanceSensor'"));

  // Clear all existing services and characteristics
  if (!ble.atcommand(F("AT+GATTCLEAR"))) {
    error(F("Failed to clear GATT services"));
  }
  Serial.println(F("Cleared GATT services"));
  
  // Register a service with a unique 128-bit UUID generated by online generator
  if (!ble.atcommandIntReply(F("AT+GATTADDSERVICE=UUID128=c7-81-14-b0-51-cd-4f-2d-8e-1b-5b-64-28-18-1c-66"), &s_ID)){
    error(F("Failed to register service 1"));
  }
  Serial.println(F("GATT service added"));

  // Register a characteristic with a 16-bit UUID (String: 0x2A3D)
  // Property is notify
  if (!ble.atcommandIntReply(F("AT+GATTADDCHAR=UUID=0x2A3D,PROPERTIES=0x10,MIN_LEN=1,VALUE=0,DESCRIPTION=DISTANCE MEASUREMENT"), &c_ID)){
    error(F("Failed to register characteristic 1"));
  }
  Serial.println(F("GATT characteristic added"));

  // Reset system after changing GATT profile
  ble.atcommand(F("ATZ"));
  delay(3000);
  
  // Start advertising if it isn't already
  ble.atcommand(F("AT+GAPSTARTADV"));
  Serial.println(F("BLE advertising..."));
}

// Send a new distance value to BLE and return 1 for OK or 0 for ERROR
void setCharacteristicToValue(uint32_t ID, uint32_t val){
      // Clear update command string
  memset(updateCommand, 0, UPDATE_COMMAND_CHARSIZE);

    // Fill update command with characteristic ID and distance value
  sprintf(updateCommand, "AT+GATTCHAR=%u,%u", ID, val);

  if (!ble.atcommand(updateCommand)){
    error(F("Failed to send new distance data"));
  }
  
  Serial.print("Executed command:\t");
  Serial.println(updateCommand);
}

// Configure ADC to target ADC0 and enable ADC interrupt. Auto-trigger is disabled
void adc_setup(){

  PRR &= ~(1 << PRADC); // Disable power reduction for adc
  
  ADMUX = (0 << REFS1) | (1 << REFS0); // Set ADC voltage reference to AVcc with external capacitor at AREF pin
  ADMUX &= B11110000; // ADMUX: select ADC0 as input by setting MUX[0:3] = 0
  ADMUX &= ~(1 << ADLAR); // Right-adjusted bit
  
  ADCSRA |= (1 << ADEN); //ADC enable bit
  delay(10);

  ADCSRA |= (1 << ADIF); // Clear interrupt flag by writing a 1
  ADCSRA &= ~(1 << ADATE); // Disable ADC auto trigger bit
  ADCSRA |= (1 << ADIE); // Enable ADC interrupt
  ADCSRA |= B00000111; // Set pre-scaler to 128 for most accurate setting

}

// Start ADC conversion. When conversion is completed, ADC_vect interrupt will be triggered
void adc_start(){
  ADCSRA |= 1 << ADSC; // start conversion, ADCS will stay high while processing and clear by hardware
}




