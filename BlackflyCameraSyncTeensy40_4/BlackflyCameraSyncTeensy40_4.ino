// *********************************************************************************************//
// Blacklfy Camera Sync with Teensy 4.x
// *********************************************************************************************//
//
// Description
// -----------
// This code uses a Teensy 4.x microcontroller to control an external light source for 
// Team 21050's Rapid Optical Imaging of Physiological Process (ROIPP)
// engineering senior design project for the University of Arizona (ENGR 498) 2021.
//
// Requirements
// ------------
// This sofware requires a Teensy 4.x Microcontroller.
//
// Setup
// -----
// The Blackfly Camera Output Line (Line 2, red) will need to be programmed to be the Frame Sync. 
// Best Configuration is with inverted logic, meaning signal high is no exposure and low
// is senors is colleccting light.
//
// Operation
// ---------
// This code cycles through 13 LEDs (+ background). 
// The camera provides a trigger on the GPIO line, letting the teensy know that the sensor is 
// exposed to light. Through this GPIO signal, the camera and microcontroller are able to 
// synchronize the camera images to the 13 LED light sources.
// 
// The intensity of the lightsource is adjusted  using PWM  modulation;
// The driver electronics for the LEDs consists of a power FET and a FET driver.
// The PWM signal is attached to the gate of the FET driver and the LED on/off signal to enable pin.
//
// By pressing an external button, the system is able to have all LEDs to be turned off. 
//
// Shanon McCoy,Urs Utzinger, February, Summer, Fall 2021, Tucson Arizona, USA
//
// *********************************************************************************************//
// Copyright (c) 2021
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
// and associated documentation files (the "Software"), to deal in the Software without restriction, 
// including without limitation the rights to use, copy, modify, merge, publish, distribute, 
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
//
// The copyright notice and this permission notice shall be included in all copies or substantial 
// portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE 
// AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// *********************************************************************************************//

#define VERSION "1.4.0"   // 
#define EEPROM_VALID 0xF3 // if you change the EEPROM configuration, change this value so that EEPROM is reset
#include <iterator>       //
#include <algorithm>      //

// *********************************************************************************************//
// Teensy 4.0 Specifics
// PWM pins: 0,1,2,3,4,5,6,7,8,9,10,11,12,,13,14,15,18,19,22,23,24,25,28,29,33,34,35,36,37,38,39
// ADC pins: 14,15,16,17,18,19,20,21,22,23, 24,26,27
// SERIAL_TX and RX can be: 1,0  7,8  14,15  16,17  20,21  24,25  28,29  38,39 
// https://www.pjrc.com/teensy/pinout.html
// *********************************************************************************************//

// FlexPWM 4.482kHz
// QuadTimer 3.611kHz

// *********************************************************************************************//
// ------------------------------------------------------------------------
// Autoadvance:     Autoadvance to next channel when external Frame Trigger occurs
// No Autoadvance:  User is adjusting a single channel
// ------------------------------------------------------------------------
volatile bool AutoAdvance = false;              // Auto (true) or Manual
// 
#define DEBOUNCEBLOCK 1800  
// Oscillations on the frame trigger primarily occur when output is changed on teensy pins.
// It lasts a few micro seconds and is in the 10MHz range.
// The bouncing block values is in microseconds;
// With 500 frames per second we have frame trigger every 2000 micro seconds
// therore the bounce block should be less than 2 ms long

// ------------------------------------------------------------------------
// Controlling Reporting and Input Output
// ------------------------------------------------------------------------
bool SERIAL_REPORTING = false;            // Boot messages and interrupt reporting on/off, for debugging

// ------------------------------------------------------------------------
// Pin Names:
// ------------------------------------------------------------------------
#define LEDPIN           13 // buitin LED, https://www.pjrc.com/teensy/pinout.html
// Hardware LED Channels, there are many more pins available, see above
#define LED1             2  // Connect to LED driver channel 1 (output)
#define LED2             3  // Connect to LED driver channel 2 (output)
#define LED3             4  // Connect to LED driver channel 3 (output)
#define LED4             5  // Connect to LED driver channel 4 (output)
#define LED5             6  // Connect to LED driver channel 5 (output)
#define LED6             7  // Connect to LED driver channel 6 (output)
#define LED7             8  // Connect to LED driver channel 7 (output)
#define LED8             9  // Connect to LED driver channel 8 (output)
#define LED9             10 // Connect to LED driver channel 9 (output)
#define LED10            11 // Connect to LED driver channel 10 (output)
#define LED11            12 // Connect to LED driver channel 11 (output)
#define LED12            14 // Connect to LED driver channel 12 (output)
#define LED13            15 // Connect to LED driver channel 13 (output)
#define BACKGND          99 // Background, no LED, dummy value
// PWM
#define PWM              22 // Connect to LED driver PWM CLOCK (output)
#define CAMTRG           21 // Camera frame trigger, needs to go to sync line of camera
#define POWERSWITCH      -1 // External Button (input) -1 =  not used

// ------------------------------------------------------------------------
// This depends on the hardware logic of the FET driver and FET of your circuit
#define TURN_ON  HIGH       // Adjust if you have inverted logic for ON/OFF
#define TURN_OFF LOW        // 
#define PWM_INV  true       // If true, high on PWM pin represents OFF
#define INTERRUPTTRIGGER RISING // Can be RISING or FALLING
// You can change the LED right after exposure is completed or within first few miroseconds
// after camera starts measuring. Ideal is after current exposure: 
// With inverted FRAME_EXPOSURE signal that would be the rising edge of the rtigger signal

// ------------------------------------------------------------------------
// LED channel configuration
// for fewer or more channels, adjust this section
// ------------------------------------------------------------------------
#define NUM_CHANNELS 14
unsigned int                     chWorking = 0;   // current working channel for manual changes
volatile int                     chCurrent = 0;   // Start LED cycle at LED[0]
const int               LEDs[NUM_CHANNELS] = {LED1,  LED2,  LED3,  LED4,  LED5,  LED6,  LED7,  LED8,  LED9,  LED10,  LED11,  LED12,  LED13, BACKGND }; // LEDs
float              LEDsInten[NUM_CHANNELS] = {5.,    5.,    5.,    5.,    5.,    5.,    5.,    5.,    5.,    5.,     5.,     5.,     5.,    BACKGND }; // PWM in %
volatile uint16_t LEDsIntenI[NUM_CHANNELS] = {12,    12,    12,    12,    12,    12,    12,    12,    12,    12,     12,     12,     12,    BACKGND }; // PWM in raw numbers, max depends on PWM resolution

//////////////////////////////////////////////////////////////////////////////////////////////////
// You should not need to change values below                                                   //
//////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------
// Button and Frame Trigger ISR
// ------------------------------------------------------------------------
volatile long lastInterrupt;                    // keep time when interrup occured 
volatile bool myPreviousAdvance = false;        // If on/off button is pressed, what was previous status
volatile unsigned int frameTriggerOccurred = 0; // If larger than 0, a trigger occured, if larger than 1, the signal bounced (not desired)
volatile unsigned long lastTriggerTime = 0;     // For debouncing, next tirgger can not occur for DEBOUNCEBLOCK miro seconds

// ------------------------------------------------------------------------
// EEPROM configuration
// ------------------------------------------------------------------------
#include <EEPROM.h>
#define EEPROM_SIZE 2048 // Max size
int eepromAddress = 0;   // Where storage starts
struct EEPROMsettings {                  // Setting structure
  byte         valid;                    // First time use of EEPROM?
  float        LEDsInten[NUM_CHANNELS];  // Is this LED brightness in %
  float        DutyCycle;                // Default duty cycle (ie 10 = 10% duty cycle)
  float        PWM_Frequency;            // Lower frequency, higher resolution
  unsigned int PWM_Resolution;           // Check teensy specs, smaller resolution, higher PWM clock
  bool         AutoAdvance;              // Frame trigger turns on next LED
};

EEPROMsettings mySettings; // the settings

// ------------------------------------------------------------------------
// Intervals and Timing
// ------------------------------------------------------------------------
#define POLL_INTERVAL           1000     // desired main loop time in microseconds (1000 results in main loop executing 1000 times per second)
#define CHECKINPUT_INTERVAL    50000     // interval in microseconds between polling serial input (20 times per second)
#define LEDON_INTERVAL        100000     // internal LED interval on in microseconds  (1/10 a second on, 9/10 off)
#define LEDOFF_INTERVAL       900000     // internal LED interval off in microseconds 

unsigned long lastInAvail;               // serial input 
unsigned long currentTime;               // current time
unsigned long nextLEDCheck;              // next time we switch status LED
unsigned long lastFrameTrigger;          // keep track when frametrigger occured
bool Stopped = true;                     // System is not autoadvancing, stalled
bool PWM_Enabled = false;                // chWorking, is LED enabled?

// ------------------------------------------------------------------------
// Indicator
// ------------------------------------------------------------------------
bool ledStatus = false;                  // Led should be off at start up

// ------------------------------------------------------------------------
// Main System and PWM working variables
// ------------------------------------------------------------------------
long          CPU_Frequency   = F_CPU / 1E6;   //
float         PWM_Frequency   = 4577.64;       // Ideal for 15 bit resolution and 600MHz Teensy 4.0
unsigned int  PWM_Resolution  = 8;             // 
unsigned int  PWM_MaxValue    = pow(2, PWM_Resolution)-1; // 255
float         DutyCycle       = 5.0;           // 5%

// ------------------------------------------------------------------------
// Serial
#define SERIAL_PORT_SPEED    2000000     // Serial Baud Rate
char   inBuff[] = "----------------";    // needs to match maximum length of a command
int    bytesread;                        // keeping track of string length

// *********************************************************************************************//
// *********************************************************************************************//
// SETUP                                                                                        //
// *********************************************************************************************//
// *********************************************************************************************//
void setup(){

  // Configure output pins, set them all to off/low
  // LEDs
  for (int i=0; i<NUM_CHANNELS-1; i++) {
    pinMode(LEDs[i], OUTPUT); 
    digitalWrite(LEDs[i],  TURN_OFF);
  }
  // PWM
  pinMode(PWM,       OUTPUT); // PWM pin
  if (PWM_INV) { digitalWrite(PWM, HIGH); } else { digitalWrite(PWM, LOW); }
  pinMode(LEDPIN,    OUTPUT);  // indicator
  // status LED
  digitalWrite(LEDPIN,LOW ); ledStatus = false;

  //EEPROM
  EEPROM.get(eepromAddress, mySettings);
  if (mySettings.valid == EEPROM_VALID) { // apply settings because EEPROM has valid settings
    PWM_Frequency  = mySettings.PWM_Frequency;
    PWM_Resolution = mySettings.PWM_Resolution;
    PWM_MaxValue   = pow(2,PWM_Resolution)-1;  
    DutyCycle      = mySettings.DutyCycle;
    for (int i=0; i<NUM_CHANNELS; i++) { LEDsInten[i]  = mySettings.LEDsInten[i]; }
    AutoAdvance     = mySettings.AutoAdvance;
  } else {
    // create default values for settings
    PWM_Resolution = 8;
    PWM_Frequency  = GetMaxPWMFreqValue(CPU_Frequency, PWM_Resolution); 
    PWM_MaxValue   = pow(2,PWM_Resolution)-1;
    DutyCycle      = 5.0;        
    for (int i=0; i<NUM_CHANNELS; i++) { LEDsInten[i]  = 5.; }
    AutoAdvance     = false;
  }
  
  // convert the PWM % to actual PWM number
  for (int i=0; i<NUM_CHANNELS; i++) {
    if (PWM_INV) { LEDsIntenI[i] = uint16_t(( 100.0 - LEDsInten[i]) / 100. * float(PWM_MaxValue) ); }
    else         { LEDsIntenI[i] = uint16_t(          LEDsInten[i]  / 100. * float(PWM_MaxValue) ); }
  }

  // Input Pins
  pinMode(CAMTRG, INPUT_PULLUP);  // Frame trigger

  if ( isIO(POWERSWITCH) ) { pinMode(POWERSWITCH, INPUT_PULLUP); }  // initialize pin for power switch

  // Set PWM source
  setupPWM(PWM_Frequency, DutyCycle, PWM_Resolution);

  // Serial startup
  Serial.begin(SERIAL_PORT_SPEED);
  if (SERIAL_REPORTING) {
    Serial.println("Starting System");
    printHelp();
  }

  // Interrupts
  // Frame Trigger, required
  attachInterrupt(digitalPinToInterrupt(CAMTRG),   frameISR, INTERRUPTTRIGGER);
  // Power Button, if button is not existing pin, dont enable this, e.g. -1
  if ( isIO(POWERSWITCH) ) { attachInterrupt(digitalPinToInterrupt(POWERSWITCH), onOff, CHANGE); } // Create interrupt on pin for power switch. Trigger when there is a change detected (button is pressed)

  // House keeping
  lastInAvail = lastInterrupt = nextLEDCheck = micros();

  printSystemInformation();
  printHelp();

} // end setup

// *********************************************************************************************//
// *********************************************************************************************//
// Main LOOP                                                                                    //
// *********************************************************************************************//
// *********************************************************************************************//

void loop(){
  
  // Time Keeper
  //////////////////////////////////////////////////////////////////
  currentTime = micros();                                         // whats the time
 
  // Input Commands
  //////////////////////////////////////////////////////////////////
  if ((currentTime - lastInAvail) >= CHECKINPUT_INTERVAL) {
    if (Serial.available()) {
      bytesread = Serial.readBytesUntil('\n', inBuff, 16);      // Read from serial until CR is read or timeout exceeded
      inBuff[bytesread] = '\0';
      String instruction = String(inBuff);
      processInstruction(instruction);
    }
    lastInAvail = currentTime;
  }
  
  // Blink LED
  //////////////////////////////////////////////////////////////////
  if (frameTriggerOccurred > 0) { // blink
    ledStatus = !ledStatus;
    digitalWriteFast(LEDPIN, ledStatus); // blink
    nextLEDCheck = currentTime + LEDON_INTERVAL;
    if (SERIAL_REPORTING) {Serial.printf("Trg: %u, LED: %i, Inten: %hu\r\n",frameTriggerOccurred, chCurrent, LEDsIntenI[chCurrent]);}
    frameTriggerOccurred = 0; // reset signal
    lastFrameTrigger = currentTime; // keep track, so we can figure out if system stalled
    Stopped = false;  // system did not stall
  } else { // regular blinking
    if (currentTime > nextLEDCheck) {
      if (ledStatus) {
        // LED is ON
        ledStatus = false; 
        digitalWriteFast(LEDPIN, ledStatus);                        // turn off
        nextLEDCheck = currentTime + LEDOFF_INTERVAL;
      } else {
        // LED is OFF
        ledStatus = true;
        digitalWriteFast(LEDPIN, ledStatus);                        // turn on
        nextLEDCheck = currentTime + LEDON_INTERVAL;
      }
    }
  }

  // If autoadvance enabled
  // and if no frame trigger detected for 5 seconds
  // turn off all LEDs
  if (AutoAdvance && (Stopped==false)) {
    if ( (currentTime -lastFrameTrigger) > 5000000) {
      for (int i=0; i<NUM_CHANNELS-1; i++) { digitalWriteFast(LEDs[i],TURN_OFF); }
      Stopped = true;
      if (SERIAL_REPORTING) { Serial.println("Turned off all LEDs"); }
    }
  }
  
  // Main loop delay. Limits how often this loop is running
  //////////////////////////////////////////////////////////////////
  long myDelay = (POLL_INTERVAL - (long)(micros() - currentTime) ); // how long do we need to wait?
  if (myDelay > 0) {
    delayMicroseconds(myDelay);  // wait
  }
  
} 

// *********************************************************************************************//
// Interrupt Service Routines                                                                   //
// *********************************************************************************************//

// Frame Trigger Interrupt Service Routine
//  analgoWrite(pin,0) takes 2.5-3 microseconds on Teensy 3.2
//  analgoWrite(pin,some value > 0) takes 5-6 microseconds on Teensy 3.2
//
// Turns off current LED
// Increment to next LED
// If current LED is BACKGND, do not turn on/off anything
// Set intensity
// Turns on LED


//////////////////////////////////////////////////////////////////
void frameISR() {
  if ( AutoAdvance ) {
    unsigned long triggerTime = micros();
    if ( (triggerTime - lastTriggerTime) > DEBOUNCEBLOCK ) { // debounce
      // Turn OFF previous LED, skip the background channel
      if (LEDs[chCurrent] != BACKGND) { digitalWriteFast(LEDs[chCurrent], TURN_OFF); } 
      // Increment channel
      chCurrent += 1; 
      if (chCurrent >= NUM_CHANNELS) {chCurrent = 0;}
      // Turn ON next LED, skip background channel
      if (LEDs[chCurrent] != BACKGND) { 
        analogWrite(PWM, LEDsIntenI[chCurrent]); // set intensity 
        digitalWriteFast(LEDs[chCurrent], TURN_ON); // turn on enable pin
      }
      frameTriggerOccurred += 1;  // signal to main loop
      lastTriggerTime = triggerTime;
    }
  }
}

// Button ISR, toggles On/Off state of system
//
// turns off all LEDs for Off state
// Debounces 20ms
//////////////////////////////////////////////////////////////////
void onOff() {
  unsigned long onOffTime = micros();
  if (onOffTime - lastInterrupt > 20000) {                       // debounce condition, 20ms
    if (AutoAdvance) {
      myPreviousAdvance = AutoAdvance;                                   // keep track if state was manual or auto
      AutoAdvance = false;
      for (int i=0; i<NUM_CHANNELS; i++)  {                      // Turen off all PWM except last channel which is background
        if ( LEDs[i] != BACKGND ) { digitalWriteFast(LEDs[i],  TURN_OFF); }
      }
    } else {
      AutoAdvance = myPreviousAdvance; // switch back to auto or manual
    }
    lastInterrupt = onOffTime;  // keep track of time for debouncing
  }  
}

// *********************************************************************************************//
// Support Functions                                                                            //
// *********************************************************************************************//

// Teensy 4.0 realtion between PWM frequency and PWM resolution
//////////////////////////////////////////////////////////////////
float GetMaxPWMFreqValue(long FREQ, int PWM_Resolution)
{
  /* for Teensy CPU frequencies are
      24MHZ
      396MHZ & 450MHZ
      528MHZ & 600MHZ
  */
  // FlexPWM 4.482kHz
  // QuadTimer 3.611kHz

  int FREQ_pointer = -1;
  float PWM_ideal_frequency[3][14]
  {
    { 6000000,  3000000, 1500000,  750000,  375000,  187500,  93750,    46875,     23437.5,  11718.75,  5859.375, 2929.687, 1464.843, 732.421},
    {33000000, 16500000, 8250000, 4125000, 2062500, 1031250, 515625,   257812.5,  128906.25, 64453.13, 32226.56, 16113.28,  8056.64, 4028.32 },
    {37500000, 18750000, 9375000, 4687500, 2343750, 1171875, 585937.5, 292968.75, 146484.38, 73242.19, 36621.09, 18310.55,  9155.27, 4577.64 }
  };

  switch (FREQ) {
    case 24:  FREQ_pointer =  0; break;
    case 396: FREQ_pointer =  1; break;
    case 450: FREQ_pointer =  2; break;
    case 528: FREQ_pointer =  1; break;
    case 600: FREQ_pointer =  2; break;
    default:  FREQ_pointer = -1; break;
  }
  if (FREQ_pointer >= 0) { return (PWM_ideal_frequency[FREQ_pointer][PWM_Resolution - 2]); } 
  else { return (4482); }
} // end of getMaxPWMFreqValue

// Setup PWM modulation on a pin
// This is slow and should only be used for setup and not to change dutycyle
//////////////////////////////////////////////////////////////////
void setupPWM(float PWM_Freq, float Duty, unsigned int Resolution) {
  if ((PWM_Resolution >= 2) && (PWM_Resolution <= 15)) { // check bounds
    analogWriteResolution(PWM_Resolution);    // change resolution
    PWM_MaxValue = pow(2, PWM_Resolution)-1; // update global
  }
  if (PWM_Freq > GetMaxPWMFreqValue(CPU_Frequency, PWM_Resolution)) {
    PWM_Freq = GetMaxPWMFreqValue(CPU_Frequency, PWM_Resolution);
  }
  analogWriteFrequency(PWM, PWM_Freq);
  if ( (Duty > ((1./float(PWM_MaxValue))*100.)) && (Duty < 100.0)) {    
    if (PWM_INV) {
      // PWM signal high turns the LED off
      analogWrite(PWM, uint16_t((100.0-Duty) / 100.0 * float(PWM_MaxValue)));
    } else {
      // PWM signal high turns the LED on
      analogWrite(PWM, uint16_t(Duty / 100.0 * float(PWM_MaxValue)));
    }
  } else if (Duty <= (1./float(PWM_MaxValue))*100.) {
      pinMode(PWM, OUTPUT);
      if (PWM_INV) { digitalWrite(PWM,HIGH); } else { digitalWrite(PWM,LOW); }
  } else if (Duty >= 100.0) {
      pinMode(PWM, OUTPUT);
      if (PWM_INV) { digitalWrite(PWM,LOW); } else { digitalWrite(PWM,HIGH); }
  }
}

// What are the software options?
//////////////////////////////////////////////////////////////////
void printHelp() {
  Serial.println("-------------------------------------------------");
  Serial.println("BlackFly Camera to LED Illumination Sync Help");
  Serial.println("-------------------------------------------------");
  Serial.println("https://github.com/uutzinger/blackflysync  ");
  Serial.println("0 Disable Auto Advance: a");
  Serial.println("  f: set the PWM frequency");
  Serial.println("  d: set the duty cycle");
  Serial.println("  r: set resolution in bits");
  Serial.println("1 Load working channel from EEPROM: e.g. s0");
  Serial.println("2 Adjust working channel");
  Serial.println("  d: set intensity of working channel 0..100");
  Serial.println("3 Save working channel: S0");
  Serial.println("4 Enable Auto Advance: A");
  Serial.println("5 Save channel configurations to EEPROM: E");
  Serial.println("------- Data Input--------------------------------");
  Serial.println("d50  set duty cyle to 50%");
  Serial.println("f512 set frequency to 512Hz");
  Serial.println("r8   set PWM resolution to 8 bits");  
  Serial.println("-------------------------------------------------");
  Serial.println("i/I  system information");
  Serial.println("x    print channel settings");
  Serial.println("-------------------------------------------------");
  Serial.println("s0   load channel 0 to current working settings");
  Serial.println("S0   save channel 0 from current working settings");
  Serial.println("save to EEPROM to make presistant");
  Serial.println("-------------------------------------------------");
  Serial.println("a/A  disable/enable Auto Advance"); 
  Serial.println("m/M  turn on/off current working channel");
  Serial.println("Z    turn on/off all channels");  
  Serial.println("e/E  read/save settings to EEPROM");
  Serial.println("-------------------------------------------------");
  Serial.println("Shannon McCoy, Urs Utzinger, 2020-21");
  Serial.println("-------------------------------------------------");
}

void printSystemInformation() {
  Serial.println("-------------------------------------------------");
  Serial.println("BlackFly Camera to LED Illumination Sync Status");
  Serial.println("-------------------------------------------------");
  Serial.printf( "Software version:     %s\r\n", VERSION);
  Serial.printf( "CPU:                  %2d MHz\r\n", CPU_Frequency);
  Serial.printf( "PWM pin:              %d\r\n", PWM);
  Serial.printf( "Frequency:            %.2f Hz\r\n", PWM_Frequency);
  Serial.printf( "Current Duty:         %.2f [%%]\r\n", DutyCycle);
  Serial.printf( "Current Resolution:   %d bit\r\n", PWM_Resolution);
  Serial.printf( "PWM Max:              %d\r\n", PWM_MaxValue);
  Serial.printf( "Camera trigger is on: %d\r\n", CAMTRG);
  Serial.printf( "Power switch:         %d\r\n", POWERSWITCH);
  Serial.println("-------------------------------------------------");
  Serial.printf( "State is:             %s\r\n", AutoAdvance?"Auto":"Manual");
  Serial.println("-------------------------------------------------");
  Serial.printf( "Working on channel: %2d which is on pin %2d\r\n", chWorking, LEDs[chWorking]);
  printChannels();
}

void printChannels() {
  Serial.println("-------------------------------------------------");
  for (int i=0; i<NUM_CHANNELS; i++) {
    Serial.printf( "Channel: %2d pin: %2d", i, LEDs[i]);
    Serial.printf( " Duty: %6.2f",  LEDsInten[i]); 
    Serial.printf( " Duty Raw: %hu\r\n",  LEDsIntenI[i]); 
  }
}

void printPinInformation() {
  Serial.println("-------------------------------------------------");
  Serial.println("Maximum Values:");
  listFrequencies();
  listPins();
}

void listFrequencies()
{
  Serial.println("-------------------------------------------------");
  for(int i=2; i<=15; i++) {
    Serial.printf("Resolution: %2d bit, Frequency: %11.2f\r\n", i, GetMaxPWMFreqValue(CPU_Frequency, i));
  }
  Serial.println("Resolution: 16 bit, Frequency:        N.A.");
} 

boolean isPWM(uint8_t mypin) {
  const uint8_t last = 31; // number of pins -1
  const uint8_t pwmpins[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,18,19,22,23,24,25,28,29,33,34,35,36,37,38,39};
  return std::binary_search(pwmpins, &pwmpins[last], mypin);
}

boolean isIO(uint8_t mypin) {
  const uint8_t last = 38; // number of pins -1
  const uint8_t iopins[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
  return std::binary_search(iopins, &iopins[last], mypin);
}

void listPins() {
  char pinType[] = "Hard";
  Serial.println("Available Pins for PWM");
  Serial.println("-------------------------------------------------");
  for (int i=0; i<40; i++){
    if      (isPWM(i))     {strcpy(pinType, "PWM");}
    else if (isIO(i))      {strcpy(pinType, "DIO");}
    else                   {strcpy(pinType, "N.A.");}
    Serial.printf("Pin: %2d, %s\n", i, pinType );
  }
}

// SerialCommand handlers
//////////////////////////////////////////////////////////////////

void processInstruction(String instruction) {
  String value    = "0.01";
  String command  = "o";
  float  tempFloat;
  long   tempInt;
  int instructionLength = instruction.length();
  if (instructionLength > 0) { command = instruction.substring(0,1); } 
  if (instructionLength > 1) {   value = instruction.substring(1,instructionLength); }
  //mySerial.println(command);
  //mySerial.println(value);

  if        (command == 'a') { // manual mode
    // ENABLE/DISABLE Autodavance based on Frame Trigger////////////////////////////
    AutoAdvance = false;    
    Serial.println("Manual");
  } else if (command == 'A') { // auto advance
    AutoAdvance = true;
    Serial.println("Auto");
        
  } else if (command == 'x') { // channel settings
    printChannels();

  } else if ( command == 'i') { // system information
    printSystemInformation();

  } else if ( command == 'I') { // system information
    printPinInformation();

  } else if ( command == 'e') { // read EEPROM
    EEPROM.get(eepromAddress, mySettings);
    if (mySettings.valid == EEPROM_VALID) { // apply settings because EEPROM has valid settings
      PWM_Frequency  = mySettings.PWM_Frequency;
      PWM_Resolution = mySettings.PWM_Resolution;
      PWM_MaxValue   = pow(2,PWM_Resolution)-1;  
      DutyCycle      = mySettings.DutyCycle;
      for (int i=0; i<NUM_CHANNELS; i++) {
        LEDsInten[i] = mySettings.LEDsInten[i];
        if (PWM_INV) { LEDsIntenI[i] = uint16_t(( 100.0 - LEDsInten[i]) / 100. * float(PWM_MaxValue) ); }
        else         { LEDsIntenI[i] = uint16_t(          LEDsInten[i]  / 100. * float(PWM_MaxValue) ); }
      }
      AutoAdvance    = mySettings.AutoAdvance;
      Serial.println("EEPROM read.");
    } else { Serial.println("EEPROM settings not valid, not applied."); }

  } else if (command == 'E') { // saveEEPROM
    mySettings.valid           = EEPROM_VALID;  // make EEPROM settings valid
    mySettings.PWM_Frequency   = PWM_Frequency;
    mySettings.PWM_Resolution  = PWM_Resolution;
    mySettings.DutyCycle       = DutyCycle;   
    for (int i=0; i<NUM_CHANNELS; i++) {
      mySettings.LEDsInten[i]  = LEDsInten[i];
    }
    mySettings.AutoAdvance     = AutoAdvance;
    EEPROM.put(eepromAddress, mySettings);
    Serial.println("Settings saved to EEPROM");

  } else if (command == 'm') { // turn on/off
    // ENABLE/DISABLE  //////////////////////////////////////////////////////////
    digitalWrite(LEDs[chWorking],  TURN_OFF);
    PWM_Enabled = false;
    Serial.printf("Pin %d is off\r\n", LEDs[chWorking]);

  } else if (command == 'M') { // turn on PWM
    digitalWrite(LEDs[chWorking],  TURN_ON);
    PWM_Enabled = true;
    Serial.printf("Pin %d is on\n", LEDs[chWorking]);

  } else if (command == 's') { // load duty cycle for channel
    // Load & Save Channel Settings////////////////////////////////////////////////
    tempInt = value.toInt();      
    if ((tempInt >=0) || (tempInt < NUM_CHANNELS)) { // cehck boundaries      
      chWorking   = tempInt;
      DutyCycle   = LEDsInten[chWorking];
      Serial.printf("Current Channel:     %2d\r\n",   chWorking);
      Serial.printf("Pin:                 %2d\r\n",   LEDs[chWorking]);
      Serial.printf("PWM Duty:            %5.2f\r\n", DutyCycle);
      Serial.printf("PWM Frequency: %11.2f\r\n",PWM_Frequency);
      Serial.printf("Channel:       %s\r\n",    PWM_Enabled?" Enabled":"Disabled");
      if (PWM_Enabled) { digitalWrite(LEDs[chWorking], TURN_ON); }
      else             { digitalWrite(LEDs[chWorking], TURN_OFF); }
    } else { Serial.println("Channel out of valid Range.");   }
    
  } else if (command == 'S') { // save duty cycle and enable/disable and pin into selected channel
    tempInt = value.toInt();      
    if ((tempInt >=0) || (tempInt < NUM_CHANNELS)) {      
      chWorking = tempInt;
      LEDsInten[chWorking] = DutyCycle;       
      if (PWM_INV) { LEDsIntenI[chWorking] = uint16_t(( 100.0 - DutyCycle) / 100. * float(PWM_MaxValue) ); }
      else         { LEDsIntenI[chWorking] = uint16_t(          DutyCycle  / 100. * float(PWM_MaxValue) ); }
      Serial.printf("Channel %d saved.\r\n",chWorking);
    } else { Serial.printf("Channel %d out of valid Range.\r\n",chWorking);}
      
  } else if (command == 'd') { // duty cycle
    // SET DUTY CYCLE /////////////////////////////////////////////////////////////
    tempFloat = value.toFloat();
    if ((tempFloat < 0.0) || (tempFloat > 100.0)) { // check boundaries
      Serial.println("Duty cyle out of valid Range.");
    } else {
      DutyCycle = tempFloat;
      setupPWM(PWM_Frequency, DutyCycle, PWM_Resolution);
      Serial.printf("Duty Cycle set to: %+4.3f\n", DutyCycle);
    }

  } else if (command == 'f') { // frequency
    // SET Frequency //////////////////////////////////////////////////////////////
    float tempFloat = value.toFloat();
    if (isPWM(PWM)) {
      Serial.printf("Desired Frequency: %10.2f\r\n", tempFloat);
      if (tempFloat <= GetMaxPWMFreqValue(CPU_Frequency, PWM_Resolution)) {
        PWM_Frequency = tempFloat;
        setupPWM(PWM_Frequency, DutyCycle, PWM_Resolution);
        Serial.printf("Frequency set to: %10.2f\r\n", PWM_Frequency);
      } else {
        Serial.println("Frequency to high. Not changed. Change Resolution first.");
      }
    }

  } else if (command == 'r') { // resolution of pulse width modulation
    // SET Resolution ////////////////////////////////////////////////////////////
    tempInt = value.toInt();
      if ((tempInt < 2) || (tempInt > 15)) { // check boundary
        Serial.println("PWM Resolution out of valid Range.");
      } else {
        PWM_Resolution = tempInt;
        PWM_MaxValue = pow(2, PWM_Resolution)-1;
        if (PWM_Frequency > GetMaxPWMFreqValue(CPU_Frequency, PWM_Resolution)) {
          PWM_Frequency = GetMaxPWMFreqValue(CPU_Frequency, PWM_Resolution);
        }
        setupPWM(PWM_Frequency, DutyCycle, PWM_Resolution);
        Serial.printf("PWM Resolution set to: %2d\r\n", PWM_Resolution);
        Serial.printf("PWM Max Value: %5d\r\n", PWM_MaxValue);
        Serial.printf("PWM Frequency adjusted to: %10.2f\r\n", PWM_Frequency);
      }
    
  } else if (command == 'Z') { // resolution of pulse width modulation
    for (int i=0; i<NUM_CHANNELS-1; i++) { digitalWriteFast(LEDs[i],TURN_OFF); } 
    Serial.println("Turned all channels off");
  
  } else if (command == '\n') { // ignore
    // Ignore Carriage Return //////////////////////////////////////////////////////////////
    printHelp();

  } else if ((command == '?') || (command == 'h')) { // send HELP information
    // HELP //////////////////////////////////////////////////////////////
    printHelp();
  }
} // end process instruction
