/*
  This sketch was modified to work with the Chinese KIT from AliExpress 

  This sketch works on DVE (David/Martins) KIT. See: https://davidmartinsengineering.wordpress.com/si4735-radio-kit/
  If you are using the DVE kit, set the OLED I2C bus address to 0X3D (see I2C_ADDRESS defined constan below)

  SI473X all in one with SSB Support
  It is important to know the SSB support works on SI4735-D60 and SI4732-A10 devices. 

  This sketch uses I2C OLED/I2C, buttons and  Encoder.

  This sketch uses the Rotary Encoder Class implementation from Ben Buxton (the source code is included
  together with this sketch) and Tiny4kOLED Library (look for this library on Tools->Manage Libraries). 

  ABOUT SSB PATCH:  
  This sketch will download a SSB patch to your SI4735-D60 or SI4732-A10 devices (patch_init.h). It will take about 8KB of the Arduino memory.

  First of all, it is important to say that the SSB patch content is not part of this library. The paches used here were made available by Mr. 
  Vadim Afonkin on his Dropbox repository. It is important to note that the author of this library does not encourage anyone to use the SSB patches 
  content for commercial purposes. In other words, this library only supports SSB patches, the patches themselves are not part of this library.

  In this context, a patch is a piece of software used to change the behavior of the SI4735 device.
  There is little information available about patching the SI4735 or Si4732 devices. The following information is the understanding of the author of
  this project and it is not necessarily correct. A patch is executed internally (run by internal MCU) of the device.
  Usually, patches are used to fixes bugs or add improvements and new features of the firmware installed in the internal ROM of the device.
  Patches to the SI4735 are distributed in binary form and have to be transferred to the internal RAM of the device by
  the host MCU (in this case Arduino). Since the RAM is volatile memory, the patch stored into the device gets lost when you turn off the system.
  Consequently, the content of the patch has to be transferred again to the device each time after turn on the system or reset the device.

  ATTENTION: The author of this project does not guarantee that procedures shown here will work in your development environment.
  Given this, it is at your own risk to continue with the procedures suggested here.
  This library works with the I2C communication protocol and it is designed to apply a SSB extension PATCH to CI SI4735-D60.
  Once again, the author disclaims any liability for any damage this procedure may cause to your SI4735 or other devices that you are using.

  Features of this sketch:

  1) FM, AM (MW and SW) and SSB (LSB and USB);
  2) Audio bandwidth filter 0.5, 1, 1.2, 2.2, 3 and 4kHz;
  3) 22 commercial and ham radio bands pre configured;
  4) BFO Control; and
  5) Frequency step switch (1, 5 and 10kHz);
  6) RDS

  Prototype documentation: https://pu2clr.github.io/SI4735/
  PU2CLR Si47XX API documentation: https://pu2clr.github.io/SI4735/extras/apidoc/html/

  TO DO:

  1) Setup mode to: 
    1.1. allow the user to select MW band space 9kHz or 10kHz;
    1.2. set the defaul volume level;
  2) Implement the seek (ATS) functions. If AM (LW/MW and SW) or FM modes, the encoder push button will start seeking station.
     The direction of the seek will depend on the last encoder rotation direction.     

  By Ricardo Lima Caratti, Nov 2021.
*/

#include <SI4735.h>
#include <EEPROM.h>
#include <Tiny4kOLED.h>
#include "Rotary.h"

// Test it with patch_init.h or patch_full.h. Do not try load both.
#include "patch_init.h" // SSB patch for whole SSBRX initialization string
// #include "patch_full.h"    // SSB patch for whole SSBRX full download

const uint16_t size_content = sizeof ssb_patch_content; // see ssb_patch_content in patch_full.h or patch_init.h

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

// OLED Diaplay constants 
#define I2C_ADDRESS 0x3C  // Check your I2C bus address (0X3D is also very commom) 
#define RST_PIN -1        // Define proper RST_PIN if required.

#define RESET_PIN 12

// Enconder PINs --> you may have to invert pins 2 and 3 to get the right clockwise and counterclockwise rotation.
#define ENCODER_PIN_A 2 
#define ENCODER_PIN_B 3

// Buttons controllers
#define MODE_SWITCH 4      // Switch MODE (Am/LSB/USB)
#define BANDWIDTH_BUTTON 5 // Used to select the banddwith. Values: 1.2, 2.2, 3.0, 4.0, 0.5, 1.0 kHz
#define VOL_UP 6           // Volume Up
#define VOL_DOWN 7         // Volume Down
#define BAND_BUTTON_UP 8   // Next band
#define BAND_BUTTON_DOWN 9 // Previous band
#define AGC_SWITCH 11      // Switch AGC ON/OF
#define STEP_SWITCH 10     // Used to select the increment or decrement frequency step (1, 5 or 10 kHz)
#define BFO_SWITCH 14      // Used to select the enconder control (BFO or VFO)

#define MIN_ELAPSED_TIME 100
#define MIN_ELAPSED_RSSI_TIME 150
#define DEFAULT_VOLUME 45 // change it for your favorite sound volume

#define FM 0
#define LSB 1
#define USB 2
#define AM 3
#define LW 4

#define SSB 1

#define STORE_TIME 10000

const uint8_t app_id = 35; // Useful to check the EEPROM content before processing useful data
const int eeprom_address = 0;
long storeTime = millis();


const char *bandModeDesc[] = {"FM ", "LSB", "USB", "AM "};
uint8_t currentMode = FM;
uint8_t seekDirection = 1;

bool bfoOn = false;
bool ssbLoaded = false;
bool fmStereo = true;

int currentBFO = 0;
long elapsedRSSI = millis();
long elapsedButton = millis();

// Encoder control variables
volatile int encoderCount = 0;

// Some variables to check the SI4735 status
uint16_t currentFrequency;
uint16_t previousFrequency;
uint8_t currentStep = 1;
uint8_t currentBFOStep = 25;

uint8_t bwIdxSSB = 2;
const char *bandwitdthSSB[] = {"1.2", "2.2", "3.0", "4.0", "0.5", "1.0"};

uint8_t bwIdxAM = 1;
const char *bandwitdthAM[] = {"6", "4", "3", "2", "1", "1.8", "2.5"};

// Atenuação and AGC
uint8_t agcIdx = 0;
uint8_t disableAgc = 0;
uint8_t agcNdx = 0;

/*
   Band data structure
*/
typedef struct
{
  uint8_t bandType;     // Band type (FM, MW or SW)
  uint16_t minimumFreq; // Minimum frequency of the band
  uint16_t maximumFreq; // maximum frequency of the band
  uint16_t currentFreq; // Default frequency or current frequency
  uint16_t currentStep; // Defeult step (increment and decrement)
} Band;

/*
 *  Band table
 */
Band band[] = {
    {FM_BAND_TYPE, 8400, 10800, 10570, 10},
    {LW_BAND_TYPE, 100, 510, 300, 1},
    {MW_BAND_TYPE, 520, 1720, 810, 10},
    {SW_BAND_TYPE, 1800, 3500, 1900, 1}, // 160 meters
    {SW_BAND_TYPE, 3500, 4500, 3700, 1}, // 80 meters
    {SW_BAND_TYPE, 4500, 5500, 4850, 5},
    {SW_BAND_TYPE, 5600, 6300, 6000, 5},
    {SW_BAND_TYPE, 6800, 7800, 7200, 5}, // 40 meters
    {SW_BAND_TYPE, 9200, 10000, 9600, 5},
    {SW_BAND_TYPE, 10000, 11000, 10100, 1}, // 30 meters
    {SW_BAND_TYPE, 11200, 12500, 11940, 5},
    {SW_BAND_TYPE, 13400, 13900, 13600, 5},
    {SW_BAND_TYPE, 14000, 14500, 14200, 1}, // 20 meters
    {SW_BAND_TYPE, 15000, 15900, 15300, 5},
    {SW_BAND_TYPE, 17200, 17900, 17600, 5},
    {SW_BAND_TYPE, 18000, 18300, 18100, 1},  // 17 meters
    {SW_BAND_TYPE, 21000, 21900, 21200, 1},  // 15 mters
    {SW_BAND_TYPE, 24890, 26200, 24940, 1},  // 12 meters
    {SW_BAND_TYPE, 26200, 27900, 27500, 1},  // CB band (11 meters)
    {SW_BAND_TYPE, 28000, 30000, 28400, 1}}; // 10 meters

const int lastBand = (sizeof band / sizeof(Band)) - 1;
int bandIdx = 0;

uint8_t rssi = 0;
uint8_t stereo = 1;
uint8_t volume = DEFAULT_VOLUME;

// Devices class declarations
Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);
SI4735 rx;

void setup()
{
  // Encoder pins
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);

  pinMode(BANDWIDTH_BUTTON, INPUT_PULLUP);
  pinMode(BAND_BUTTON_UP, INPUT_PULLUP);
  pinMode(BAND_BUTTON_DOWN, INPUT_PULLUP);
  pinMode(VOL_UP, INPUT_PULLUP);
  pinMode(VOL_DOWN, INPUT_PULLUP);
  pinMode(BFO_SWITCH, INPUT_PULLUP);
  pinMode(AGC_SWITCH, INPUT_PULLUP);
  pinMode(STEP_SWITCH, INPUT_PULLUP);
  pinMode(MODE_SWITCH, INPUT_PULLUP);

  oled.begin();
  oled.clear();
  oled.on();
  oled.setFont(FONT6X8);

  // Splash - Change it for your introduction text.
  oled.setCursor(40, 0);
  oled.print("SI473X");
  oled.setCursor(20, 1);
  oled.print("Arduino Library");
  delay(500);
  oled.setCursor(15, 2);
  oled.print("All in One Radio");
  delay(500);
  oled.setCursor(10, 3);
  oled.print("V2.0.4 - By PU2CLR");
  delay(4000);
  // end Splash

  // If you want to reset the eeprom, keep the VOLUME_UP button pressed during statup
  if (digitalRead(BFO_SWITCH) == LOW)
  {
    oled.clear();
    EEPROM.write(eeprom_address, 0);
    oled.setCursor(0, 0);
    oled.print("EEPROM RESETED");
    delay(3000);
    oled.clear();
  }

  // Encoder interrupt
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

  // Uncomment the lines below if you experience some unstable behaviour. Default values were optimized to make the SSB patch load fast
  // rx.setMaxDelayPowerUp(500);      // Time to the external crystal become stable after power up command (default is 10ms).
  // rx.setMaxDelaySetFrequency(100); // Time needed to process the next frequency setup (default is 30 ms)

  rx.getDeviceI2CAddress(RESET_PIN); // Looks for the I2C bus address and set it.  Returns 0 if error

  rx.setup(RESET_PIN, FM_BAND_TYPE);

  delay(300);

  // Checking the EEPROM content
  if (EEPROM.read(eeprom_address) == app_id)
  {
    readAllReceiverInformation();
  }

  // Set up the radio for the current band (see index table variable bandIdx )
  useBand();

  currentFrequency = previousFrequency = rx.getFrequency();

  rx.setVolume(volume);
  oled.clear();
  showStatus();
}

// Use Rotary.h and  Rotary.cpp implementation to process encoder via interrupt
void rotaryEncoder()
{ // rotary encoder events
  uint8_t encoderStatus = encoder.process();
  if (encoderStatus)
  {
    if (encoderStatus == DIR_CW)
    {
      encoderCount = 1;
    }
    else
    {
      encoderCount = -1;
    }
  }
}

/*
 * EEPROM receiver status 
 */

void saveAllReceiverInformation()
{
  int addr_offset;
  EEPROM.update(eeprom_address, app_id);                 // stores the app id;
  EEPROM.update(eeprom_address + 1, rx.getVolume()); // stores the current Volume
  EEPROM.update(eeprom_address + 2, bandIdx);            // Stores the current band
  EEPROM.update(eeprom_address + 3, currentMode);        // Stores the current Mode (FM / AM / SSB)
  EEPROM.update(eeprom_address + 4, currentBFO >> 8);
  EEPROM.update(eeprom_address + 5, currentBFO & 0XFF);

  addr_offset = 6;
  band[bandIdx].currentFreq = currentFrequency;

  for (int i = 0; i < lastBand; i++)
  {
    EEPROM.update(addr_offset++, (band[i].currentFreq >> 8));   // stores the current Frequency HIGH byte for the band
    EEPROM.update(addr_offset++, (band[i].currentFreq & 0xFF)); // stores the current Frequency LOW byte for the band
    EEPROM.update(addr_offset++, band[i].currentStep);          // Stores current step of the band
  }
  // Serial.println("All information was saved!");
}

void readAllReceiverInformation()
{
  int addr_offset;
  volume = EEPROM.read(eeprom_address + 1); // Gets the stored volume;
  bandIdx = EEPROM.read(eeprom_address + 2);
  currentMode = EEPROM.read(eeprom_address + 3);
  currentBFO = EEPROM.read(eeprom_address + 4) << 8;
  currentBFO |= EEPROM.read(eeprom_address + 5);

  addr_offset = 6;
  for (int i = 0; i < lastBand; i++)
  {
    band[i].currentFreq = EEPROM.read(addr_offset++) << 8;
    band[i].currentFreq |= EEPROM.read(addr_offset++);
    band[i].currentStep = EEPROM.read(addr_offset++);
  }

  previousFrequency = currentFrequency = band[bandIdx].currentFreq;
  currentStep = band[bandIdx].currentStep;

  if (currentMode == LSB || currentMode == USB)
  {
    loadSSB();
  }
}


void clearLine4()
{
  oled.setCursor(0, 2);
  oled.print("                    ");
}

// Show current frequency

void showFrequency()
{
  String freqDisplay;
  String unit;
  String bandMode;
  int divider = 1;
  int decimals = 3;
  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    divider = 100;
    decimals = 1;
    unit = "MHz";
  }
  else if (band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE)
  {
    divider = 1;
    decimals = 0;
    unit = "kHz";
  }
  else
  {
    divider = 1000;
    decimals = 3;
    unit = "kHz";
  }

  if (!bfoOn)
    freqDisplay = String((float)currentFrequency / divider, decimals);
  else
    freqDisplay = ">" + String((float)currentFrequency / divider, decimals) + "<";

  oled.setCursor(38, 0);
  oled.print("        ");
  oled.setCursor(38, 0);
  oled.print(freqDisplay);

  if (currentFrequency < 520)
    bandMode = "LW  ";
  else
    bandMode = bandModeDesc[currentMode];

  oled.setCursor(0, 0);
  oled.print(bandMode);

  oled.setCursor(95, 0);
  oled.print(unit);
}

/**
 *  This function is called by the seek function process.
 */
void showFrequencySeek(uint16_t freq)
{
  currentFrequency = freq;
  showFrequency();
}

/**
 * Checks the stop seeking criterias.  
 * Returns true if the user press the touch or rotates the encoder. 
 */
bool checkStopSeeking()
{
  // Checks the touch and encoder
  return (bool)encoderCount || (digitalRead(BFO_SWITCH) == LOW); // returns true if the user rotates the encoder or press the push button
}

/*
    Show some basic information on display
*/
void showStatus()
{

  showFrequency();

  oled.setCursor(80, 1);
  oled.print("      ");
  oled.setCursor(80, 1);
  oled.print("St: ");
  oled.print(currentStep);

  oled.setCursor(0, 3);
  oled.print("           ");
  oled.setCursor(0, 3);

  if (currentMode == LSB || currentMode == USB)
  {
    oled.print("BW:");
    oled.print(String(bandwitdthSSB[bwIdxSSB]));
    oled.print("kHz");
    showBFO();
  }
  else if (currentMode == AM)
  {
    oled.print("BW:");
    oled.print(String(bandwitdthAM[bwIdxAM]));
    oled.print("kHz");
  }

  // Show AGC Information
  // rx.getAutomaticGainControl();
  oled.setCursor(0, 1);
  if (agcIdx == 0 ) {
    oled.print("AGC ON");
  } else {
    oled.print("ATT: ");
    oled.print(agcNdx);
  }

  showRSSI();
  showVolume();
}

/* *******************************
   Shows RSSI status
*/
void showRSSI()
{
  char c[2] = ">";

  int bars = ((rssi / 10.0) / 2.0) + 1;

  oled.setCursor(80, 3);
  oled.print("       ");
  oled.setCursor(80, 3);
  oled.print("S:");
  if (bars > 5)
  {
    bars = 5;
    c[0] = '+';
  }
  for (int i = 0; i < bars; i++)
    oled.print(c);

  if (currentMode == FM)
  {
    oled.setCursor(0, 3);
    oled.print((rx.getCurrentPilot()) ? "STEREO   " : "MONO     ");
  }
}

/*
   Shows the volume level on LCD
*/
void showVolume()
{
  oled.setCursor(60, 3);
  oled.print("  ");
  oled.setCursor(60, 3);
  oled.print(rx.getCurrentVolume());
}

/*
   Shows the BFO current status.
   Must be called only on SSB mode (LSB or USB)
*/
void showBFO()
{

  String bfo;

  if (currentBFO > 0)
    bfo = "+" + String(currentBFO);
  else
    bfo = String(currentBFO);

  oled.setCursor(0, 2);
  oled.print("         ");
  oled.setCursor(0, 2);
  oled.print("BFO:");
  oled.print(bfo);
  oled.print("Hz ");

  oled.setCursor(80, 2);
  oled.print("       ");
  oled.setCursor(80, 2);
  oled.print("St: ");
  oled.print(currentBFOStep);
}

char *rdsMsg;
char *stationName;
char *rdsTime;
char bufferStatioName[50];
char bufferRdsMsg[100];
char bufferRdsTime[32];


void showRDSStation()
{
  clearLine4();
  oled.setCursor(0, 2);
  oled.print(stationName);
  delay(250);
}


void checkRDS()
{
  rx.getRdsStatus();
  if (rx.getRdsReceived())
  {
    if (rx.getRdsSync() && rx.getRdsSyncFound())
    {
      stationName = rx.getRdsText0A();
      if (stationName != NULL)
        showRDSStation();
    }
  }
}

/*
   Goes to the next band (see Band table)
*/
void bandUp()
{
  // save the current frequency for the band
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStep = currentStep;

  if (bandIdx < lastBand)
    bandIdx++;
  else
    bandIdx = 0;
  useBand();
}

/*
   Goes to the previous band (see Band table)
*/
void bandDown()
{
  // save the current frequency for the band
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStep = currentStep;
  if (bandIdx > 0)
    bandIdx--;
  else
    bandIdx = lastBand;
  useBand();
}

/*
   This function loads the contents of the ssb_patch_content array into the CI (Si4735) and starts the radio on
   SSB mode.
*/
void loadSSB()
{
  oled.setCursor(0, 2);
  oled.print("  Switching to SSB  ");

  rx.reset();
  rx.queryLibraryId(); // Is it really necessary here?  Just powerDown() maigh work!
  rx.patchPowerUp();
  delay(50);
  rx.setI2CFastMode(); // Recommended
  // rx.setI2CFastModeCustom(500000); // It is a test and may crash.
  rx.downloadPatch(ssb_patch_content, size_content);
  rx.setI2CStandardMode(); // goes back to default (100kHz)
  clearLine4();

  // delay(50);
  // Parameters
  // AUDIOBW - SSB Audio bandwidth; 0 = 1.2kHz (default); 1=2.2kHz; 2=3kHz; 3=4kHz; 4=500Hz; 5=1kHz;
  // SBCUTFLT SSB - side band cutoff filter for band passand low pass filter ( 0 or 1)
  // AVC_DIVIDER  - set 0 for SSB mode; set 3 for SYNC mode.
  // AVCEN - SSB Automatic Volume Control (AVC) enable; 0=disable; 1=enable (default).
  // SMUTESEL - SSB Soft-mute Based on RSSI or SNR (0 or 1).
  // DSP_AFCDIS - DSP AFC Disable or enable; 0=SYNC MODE, AFC enable; 1=SSB MODE, AFC disable.
  rx.setSSBConfig(bwIdxSSB, 1, 0, 0, 0, 1);
  delay(25);
  ssbLoaded = true;
  oled.clear();
}

/*
   Switch the radio to current band.
   The bandIdx variable points to the current band. 
   This function change to the band referenced by bandIdx (see table band).
*/
void useBand()
{
  clearLine4();
  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    currentMode = FM;
    rx.setTuneFrequencyAntennaCapacitor(0);
    rx.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
    rx.setSeekFmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
    rx.setSeekFmSpacing(1);
    bfoOn = ssbLoaded = false;
    rx.setRdsConfig(1, 2, 2, 2, 2);
  }
  else
  {
    if (band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE)
      rx.setTuneFrequencyAntennaCapacitor(0);
    else
      rx.setTuneFrequencyAntennaCapacitor(1);

    if (ssbLoaded)
    {
      rx.setSSB(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep, currentMode);
      rx.setSSBAutomaticVolumeControl(1);
      rx.setSsbSoftMuteMaxAttenuation(0); // Disable Soft Mute for SSB
    }
    else
    {
      currentMode = AM;
      rx.setAM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
      rx.setAutomaticGainControl(disableAgc, agcNdx);
      rx.setAmSoftMuteMaxAttenuation(0); // // Disable Soft Mute for AM
      bfoOn = false;
    }
    // Sets the seeking limits and space.
    rx.setSeekAmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);               // Consider the range all defined current band
    rx.setSeekAmSpacing((band[bandIdx].currentStep > 10) ? 10 : band[bandIdx].currentStep); // Max 10kHz for spacing
    
  }
  delay(100);
  currentFrequency = band[bandIdx].currentFreq;
  currentStep = band[bandIdx].currentStep;
  showStatus();
  storeTime = millis();
  previousFrequency = 0;
}

void loop()
{
  // Check if the encoder has moved.
  if (encoderCount != 0)
  {
    if (bfoOn)
    {
      currentBFO = (encoderCount == 1) ? (currentBFO + currentBFOStep) : (currentBFO - currentBFOStep);
      rx.setSSBBfo(currentBFO);
      showBFO();
    }
    else
    {
      if (encoderCount == 1) {
        rx.frequencyUp();
        seekDirection = 1;
      }
      else {
        rx.frequencyDown();
        seekDirection = 0;
      }
      // Show the current frequency only if it has changed
      currentFrequency = rx.getFrequency();
      showFrequency();
    }
    encoderCount = 0;
  }

  // Check button commands
  if ((millis() - elapsedButton) > MIN_ELAPSED_TIME)
  {
    // check if some button is pressed
    if (digitalRead(BANDWIDTH_BUTTON) == LOW)
    {
      if (currentMode == LSB || currentMode == USB)
      {
        bwIdxSSB++;
        if (bwIdxSSB > 5)
          bwIdxSSB = 0;
        rx.setSSBAudioBandwidth(bwIdxSSB);
        // If audio bandwidth selected is about 2 kHz or below, it is recommended to set Sideband Cutoff Filter to 0.
        if (bwIdxSSB == 0 || bwIdxSSB == 4 || bwIdxSSB == 5)
          rx.setSBBSidebandCutoffFilter(0);
        else
          rx.setSBBSidebandCutoffFilter(1);
      }
      else if (currentMode == AM)
      {
        bwIdxAM++;
        if (bwIdxAM > 6)
          bwIdxAM = 0;
        rx.setBandwidth(bwIdxAM, 1);
      }
      showStatus();
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(BAND_BUTTON_UP) == LOW)
      bandUp();
    else if (digitalRead(BAND_BUTTON_DOWN) == LOW)
      bandDown();
    else if (digitalRead(VOL_UP) == LOW)
    {
      rx.volumeUp();
      volume = rx.getVolume();
      showVolume();
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(VOL_DOWN) == LOW)
    {
      rx.volumeDown();
      volume = rx.getVolume();
      showVolume();
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(BFO_SWITCH) == LOW)
    {
      if (currentMode == LSB || currentMode == USB)
      {
        bfoOn = !bfoOn;
        if (bfoOn)
          showBFO();
        showStatus();
      }
      else if (currentMode == FM || currentMode == AM)
      {
        // Jumps up or down one space 
        if (seekDirection ) 
           rx.frequencyUp();
        else
           rx.frequencyDown();

        rx.seekStationProgress(showFrequencySeek, checkStopSeeking, seekDirection);
        delay(30);
        currentFrequency = rx.getFrequency();
        showFrequency();
      }
      delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    }
    else if (digitalRead(AGC_SWITCH) == LOW)
    {
      if (agcIdx == 0)
      {
        disableAgc = 0; // Turns AGC ON
        agcNdx = 0;
        agcIdx = 1;
      } else if (agcIdx == 1)
      {
        disableAgc = 1; // Turns AGC OFF
        agcNdx = 0;     // Sets minimum attenuation
        agcIdx = 2;
      } else if (agcIdx == 2)
      {
        disableAgc = 1; // Turns AGC OFF
        agcNdx = 10;    // Increases the attenuation AM/SSB AGC Index  = 10
        agcIdx = 3;
      } else if (agcIdx == 3)
      {
        disableAgc = 1; // Turns AGC OFF
        agcNdx = 20;    // Increases the attenuation AM/SSB AGC Index  = 20
        agcIdx = 4;
      } else if (agcIdx == 4)
      {
        disableAgc = 1; // Turns AGC OFF
        agcNdx = 36;    // Sets maximum attenuation
        agcIdx = 0;
      }
      // Sets AGC on/off an gain
      rx.setAutomaticGainControl(disableAgc, agcNdx);
      showStatus();
    }
    else if (digitalRead(STEP_SWITCH) == LOW)
    {
      if (currentMode == FM)
      {
        fmStereo = !fmStereo;
        if (fmStereo)
          rx.setFmStereoOn();
        else
          rx.setFmStereoOff(); // It is not working so far.
      }
      else
      {

        // This command should work only for SSB mode
        if (bfoOn && (currentMode == LSB || currentMode == USB))
        {
          currentBFOStep = (currentBFOStep == 25) ? 10 : 25;
          showBFO();
        }
        else
        {
          if (currentStep == 1)
            currentStep = 5;
          else if (currentStep == 5)
            currentStep = 9;
          else if (currentStep == 9)
            currentStep = 10;
          else if (currentStep == 10)
            currentStep = 50;
          else currentStep = 1;
          rx.setFrequencyStep(currentStep);
          band[bandIdx].currentStep = currentStep;
          showStatus();
        }
        delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
      }
    }
    else if (digitalRead(MODE_SWITCH) == LOW)
    {
      if (currentMode != FM)
      {
        if (currentMode == AM)
        {
          // If you were in AM mode, it is necessary to load SSB patch (avery time)
          loadSSB();
          currentMode = LSB;
        }
        else if (currentMode == LSB)
        {
          currentMode = USB;
        }
        else if (currentMode == USB)
        {
          currentMode = AM;
          ssbLoaded = false;
          bfoOn = false;
        }
        // Nothing to do if you are in FM mode
        band[bandIdx].currentFreq = currentFrequency;
        band[bandIdx].currentStep = currentStep;
        useBand();
      }
    }
    elapsedButton = millis();
  }

  // Show RSSI status only if this condition has changed
  if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 9)
  {
    rx.getCurrentReceivedSignalQuality();
    int aux = rx.getCurrentRSSI();
    if (rssi != aux)
    {
      rssi = aux;
      showRSSI();
    }
    elapsedRSSI = millis();
  }

  if (currentMode == FM)
  {
    if (currentFrequency != previousFrequency)
    {
      clearLine4();
    }
    checkRDS();
  }

  // Show the current frequency only if it has changed
  if (currentFrequency != previousFrequency)
  {
    if ((millis() - storeTime) > STORE_TIME)
    {
      saveAllReceiverInformation();
      storeTime = millis();
      previousFrequency = currentFrequency;
    }
  }
  delay(10);
}