/*************************************************************
******************* INCLUDES & DEFINITIONS *******************
**************************************************************/

// Necessary libraries
#include <Arduino.h>     // core Arduino library
#include <esp_adc_cal.h> // for ADC calibration
#include <TFT_eSPI.h>    // for TFT display control
#include <WiFi.h>        // for WiFi connectivity
#include "time.h"        // for time functions

// Display objects
TFT_eSPI lcd = TFT_eSPI();              // main display object
TFT_eSprite sprite = TFT_eSprite(&lcd); // sprite for double buffering

// WiFi credentials - replace with your network info
const char* wifiSSID = "YOUR_SSID"; // change to your SSID name
const char* wifiPassword = "YOUR_PASSWORD"; // change to your password

// NTP time server configuration
#define TIME_ZONE_OFFSET 2 // GMT+(change to your offset)
const char* ntpServer = "pool.ntp.org";
int selectedTimeZone = TIME_ZONE_OFFSET; // default time zone
bool dstEnabled = false;                 // change to true if you use Daylight Savings Time
const int daylightOffsetSeconds = 3600;  // DST offset in seconds (1 hour)

// Data storage for graph values (24 data points)
int graphValues[24] = {0};         // current values
int previousGraphValues[24] = {0}; // previous values for smooth transitions

// Time string buffers
char currentHour[3] = "00";
char currentMinute[3] = "00";
char currentSecond[3] = "00";
char currentMonth[4] = "Mmm";
char currentYear[5] = "YY";
char currentDay[3] = "DD";

// Graph dimensions and position constants
const int GRAPH_WIDTH = 204;
const int GRAPH_HEIGHT = 104; // visual height of graph in pixels
const int VALUE_CAP = 100;    // maximum value to display
const int GRAPH_X_POSITION = 110;
const int GRAPH_Y_POSITION = 144;
int currentValue = 0; // current sensor reading

// Colour definitions
#define COLOUR_GRAY 0x6B6D
#define COLOUR_BLUE 0x0967
#define COLOUR_PURPLE 0x604D
#define COLOUR_GREEN 0x1AE9

// Button variables
int displayMode = 0;      // 0 = simulated data, 1 = real sensor data
const int keyButton = 14; // GPIO pin for right button
const int bootButton = 0; // GPIO pin for boot button
bool lastKeyButtonState = HIGH;
bool lastBootButtonState = HIGH;

// Battery measurement
#define PIN_BAT_VOLT 4  // GPIO4 for battery voltage
#define PIN_POWER_ON 15 // GPIO15 for LCD power via battery
unsigned long lastBatteryRead = 0;
const unsigned long BATTERY_READ_INTERVAL = 5000; // read every 5 seconds
int batteryVoltage = 0; // in mV
bool batteryConnected = false;

// Runtime variables
int updateCounter = 0;           // counter for periodic updates
int minValue = VALUE_CAP / 2;    // track minimum value (using VALUE_CAP)
int maxValue = VALUE_CAP / 2;    // track maximum value (using VALUE_CAP)
int averageValue = 0;            // track average value
String minValueTimestamp = "";   // time when min occurred
String maxValueTimestamp = "";   // time when max occurred
long lastFrameTime = 0;          // for FPS calculation
int framesPerSecond = 0;         // current FPS

unsigned long lastNtpSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 1800000; // sync with NTP every 30min
unsigned long lastMillisTimeUpdate = 0;
const unsigned long TIME_UPDATE_INTERVAL = 1000; // update time display every second


/*************************************************************
********************** HELPER FUNCTIONS **********************
**************************************************************/

// Function to force update all time components
void updateTimeComponents() {
  struct tm timeinfo;

  if (getLocalTime(&timeinfo)) {
    strftime(currentHour, 3, "%H", &timeinfo);
    strftime(currentMinute, 3, "%M", &timeinfo);
    strftime(currentSecond, 3, "%S", &timeinfo);
    strftime(currentYear, 5, "%y", &timeinfo);  // changed to %y for 2-digit year
    strftime(currentMonth, 4, "%b", &timeinfo); // changed to %b for abbreviated month
    strftime(currentDay, 3, "%d", &timeinfo);
  }
}

// Function to get and format current time
void updateCurrentTime() {
  unsigned long currentMillis = millis();
  
  // Only sync with NTP periodically to avoid flooding the server
  if (currentMillis - lastNtpSync >= NTP_SYNC_INTERVAL) {
    struct tm timeinfo;
    
    // Calculate effective offset considering DST
    long effectiveOffset = selectedTimeZone * 3600;

    if (dstEnabled) {
      effectiveOffset += daylightOffsetSeconds;
    }
    
    configTime(effectiveOffset, 0, ntpServer); // 0 for DST offset as we handle it manually
    updateTimeComponents(); // update time components during NTP sync
    lastNtpSync = currentMillis;
  }
  
  // Update the time display every second using millis()
  if (currentMillis - lastMillisTimeUpdate >= TIME_UPDATE_INTERVAL) {
    struct tm timeinfo;

    if (getLocalTime(&timeinfo)) {
      // Update hours, minutes, seconds
      strftime(currentHour, 3, "%H", &timeinfo);
      strftime(currentMinute, 3, "%M", &timeinfo);
      strftime(currentSecond, 3, "%S", &timeinfo);
    }
    else {
      // If NTP fails, increment seconds manually
      int sec = atoi(currentSecond);
      sec++;

      if (sec >= 60) {
        sec = 0;
        int min = atoi(currentMinute);
        min++;

        if (min >= 60) {
          min = 0;
          int hour = atoi(currentHour);
          hour++;

          if (hour >= 24) hour = 0;
          snprintf(currentHour, 3, "%02d", hour);
        }
        snprintf(currentMinute, 3, "%02d", min);
      }
      snprintf(currentSecond, 3, "%02d", sec);
    }
    lastMillisTimeUpdate = currentMillis;
  }
}

// Function to reset statistics
void resetStatistics() {
  // Reset to current values
  minValue = graphValues[23];
  maxValue = graphValues[23];
  minValueTimestamp = String(currentHour) + ":" + String(currentMinute) + ":" + String(currentSecond);
  maxValueTimestamp = String(currentHour) + ":" + String(currentMinute) + ":" + String(currentSecond);
}

// Function to read battery voltage
void readBatteryVoltage() {
  esp_adc_cal_characteristics_t adc_chars;
  
  // Get the internal calibration value of the chip
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
    ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  uint32_t raw = analogRead(PIN_BAT_VOLT);
  batteryVoltage = esp_adc_cal_raw_to_voltage(raw, &adc_chars) * 2; // voltage divider ratio (2)
  
  // Check if battery is connected
  batteryConnected = (batteryVoltage > 1000); // 1000mV
}

/*************************************************************
*********************** MAIN FUNCTIONS ***********************
**************************************************************/

// SETUP
void setup(void) {
  // Initialize hardware
  pinMode(keyButton, INPUT_PULLUP);
  pinMode(bootButton, INPUT_PULLUP);

  // GPIO15 must be set to HIGH, otherwise nothing will be displayed when USB is not connected
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  
  // Initialize display
  lcd.init();
  lcd.setRotation(1); // landscape orientation
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setCursor(0, 0);
  
  // Display WiFi connection message
  lcd.println("\nConnecting to WiFi - please wait...");
  
  // Connect to WiFi
  WiFi.begin(wifiSSID, wifiPassword);
  
  // Show connection progress with timeout
  unsigned long connectionStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - connectionStart > 10000) { // 10 second timeout
      lcd.println("\nConnection failed!\nProgram halted.\nCheck credentials or network & try again.");
      while(1); // halt execution
    }
  }
  
  // Connection successful
  lcd.println("\nWiFi connected!");
  lcd.print("SSID: ");
  lcd.print(WiFi.SSID());
  lcd.print("\nIP: ");
  lcd.print(WiFi.localIP());
  delay(2000);
  
  // Time sync message
  lcd.println("\n\nSyncing time - please wait...");
  
  // Configure NTP time with initial settings
  long effectiveOffset = selectedTimeZone * 3600;
  if (dstEnabled) {
    effectiveOffset += daylightOffsetSeconds;
  }
  configTime(effectiveOffset, 0, ntpServer);
  
  // Wait for time sync with timeout
  struct tm timeinfo;
  unsigned long syncStart = millis();
  while(!getLocalTime(&timeinfo)) {
    if (millis() - syncStart > 10000) { // 10 second timeout
      lcd.println("\nTime synchronization failed!\nProgram halted.\nCheck internet connection and try again.");
      while(1); // halt execution
    }
  }
  
  // Time sync complete
  lcd.println("\nTime synchronized!");
  updateTimeComponents(); // Force initial time update
  lcd.print("Current time: ");
  lcd.print(currentHour);
  lcd.print(":");
  lcd.print(currentMinute);
  lcd.print(":");
  lcd.print(currentSecond);
  delay(2000);
  
  // Final initialization message
  lcd.println("\n\nInitializing sensors...");
  
  // Initialize sprite for double buffering
  sprite.createSprite(320, 170);
  sprite.setTextDatum(3); // middle center text alignment
  sprite.setSwapBytes(true);
  
  // Initialize first value in the middle of graph
  graphValues[23] = VALUE_CAP / 2;
  
  // Set ADC resolution for analog readings
  analogReadResolution(10);
  delay(2000);
  
  // Ready message
  lcd.println("\nSystem ready!\nStarting main display...");
  delay(2000);
  
  // Clear screen for main display
  lcd.fillScreen(TFT_BLACK);
  
  // Initialize time tracking
  lastNtpSync = millis();
  lastMillisTimeUpdate = millis();
}


// MAIN LOOP
void loop() {
  // Read battery voltage periodically
  if (millis() - lastBatteryRead >= BATTERY_READ_INTERVAL) {
    readBatteryVoltage();
    lastBatteryRead = millis();
  }

  // Handle button presses with edge detection
  bool currentKeyState = digitalRead(keyButton);
  bool currentBootState = digitalRead(bootButton);

  // KEY button falling edge detection (HIGH -> LOW)
  if (lastKeyButtonState == HIGH && currentKeyState == LOW) {
    displayMode = (displayMode + 1) % 2;  // toggle between 0 and 1
  }
  lastKeyButtonState = currentKeyState; // update last state

  // BOOT button falling edge detection (HIGH -> LOW)
  if (lastBootButtonState == HIGH && currentBootState == LOW) {
    resetStatistics();
  }
  lastBootButtonState = currentBootState; // update last state
  
  // Calculate FPS
  framesPerSecond = 1000 / (millis() - lastFrameTime);
  lastFrameTime = millis();
  
  // Reset average calculation
  averageValue = 0;
  
  // Update time continuously
  updateCurrentTime();

  // Periodically force update all time components
  static int timeUpdateCounter = 0;
  if (timeUpdateCounter++ >= 50) {
    updateTimeComponents();
    timeUpdateCounter = 0;
  }

  // Get new value based on current mode
  if (displayMode == 0) {
    // Simulation mode - generate random values near previous value
    if (graphValues[23] > 12) {
      currentValue = random(graphValues[23] - 12, graphValues[23] + 12);
    }
    else {
      currentValue = random(1, graphValues[23] + 14);
    }

    // Clamp value to our cap (100) not graph height (104)
    if(currentValue > VALUE_CAP) {
      currentValue = random(VALUE_CAP - 10, VALUE_CAP);
    }
  }
  else if (displayMode == 1) {
    // Real sensor mode - read from analog pin and map to 0-100
    int analogValue = analogRead(1); // read from GPIO 1 (ADC1 channel 0)
    currentValue = map(analogValue, 0, 1024, 0, VALUE_CAP);
  }

  // Shift values in the array (FIFO)
  for (int i = 0; i < 24; i++) {
    previousGraphValues[i] = graphValues[i];
  }

  for (int i = 23; i > 0; i--) {
    graphValues[i - 1] = previousGraphValues[i];
  }
  graphValues[23] = currentValue;

  // Update min/max tracking
  if (graphValues[23] > maxValue) {
    maxValue = graphValues[23];
    maxValueTimestamp = String(currentHour) + ":" + String(currentMinute) + ":" + String(currentSecond);
  }

  if (graphValues[23] < minValue) {
    minValue = graphValues[23];
    minValueTimestamp = String(currentHour) + ":" + String(currentMinute) + ":" + String(currentSecond);
  }
 
  // Calculate average
  for (int i = 0; i < 24; i++) {
    averageValue += graphValues[i];
  }
  averageValue /= 24;

  // Begin drawing to sprite (double buffer)
  sprite.fillSprite(TFT_BLACK);
  
  // Draw display boxes
  sprite.setTextColor(TFT_WHITE, COLOUR_BLUE);
  //------------------(x,  y,  w,  h,  r, colour);
  sprite.fillRoundRect(6,  5,  38, 32, 4, COLOUR_BLUE);   // Hour box
  sprite.fillRoundRect(48, 5,  38, 32, 4, COLOUR_BLUE);   // Minute box (was 52)
  sprite.fillRoundRect(90, 7,  20, 18, 2, COLOUR_BLUE);   // Seconds box
  sprite.fillRoundRect(6,  42, 80, 13, 2, COLOUR_BLUE);   // Date box
  sprite.fillRoundRect(6,  60, 80, 18, 2, COLOUR_GREEN);  // FPS box
  sprite.fillRoundRect(6,  82, 80, 78, 4, COLOUR_PURPLE); // Stats box


  // Draw time & date text
  sprite.drawString(String(currentHour), 10, 24, 4);
  sprite.drawString(String(currentMinute), 52, 24, 4); // was 56
  sprite.drawString(String(currentSecond), 92, 16, 2);
  sprite.drawString(String(currentDay) + " " + String(currentMonth) + " '" + String(currentYear), 15, 49);

  // Draw FPS
  sprite.setTextColor(TFT_WHITE, COLOUR_GREEN);
  sprite.drawString("FPS: " + String(framesPerSecond), 25, 69);

  // Draw statistics
  sprite.setTextColor(TFT_WHITE, COLOUR_PURPLE);
  sprite.drawString("VAL:    " + String(averageValue), 12, 92, 2);
  sprite.drawString("MIN:    " + String(minValue), 12, 108, 2);
  sprite.drawString("MAX:   " + String(maxValue), 12, 138, 2);

  sprite.setTextColor(TFT_SILVER, COLOUR_PURPLE);
  sprite.drawString(minValueTimestamp, 12, 122);
  sprite.drawString(maxValueTimestamp, 12, 152);

  // Draw graph title
  sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
  sprite.drawString("ANALOG READINGS", GRAPH_X_POSITION + 10, 16, 2);
  sprite.drawString("ADC1_CH0 (GPIO01)", GRAPH_X_POSITION + 10, 30);
  sprite.setFreeFont();

  // Draw graph grid (y-axis lines and labels)
  for (int i = 0; i < 6; i++) {
    int xPos = GRAPH_X_POSITION + (i*40);  // 40px spacing for 5 intervals (20,16,12,8,4,0)
    sprite.drawLine(xPos, GRAPH_Y_POSITION, 
                  xPos, GRAPH_Y_POSITION - GRAPH_HEIGHT, 
                  COLOUR_GRAY);
    
    // Calculate label (20 - (i*4))
    int yLabel = 20 - (i*4);
    
    // Special adjustments for 0 and 20
    if (yLabel == 20) {
      // Shift 20 right by 1px (xPos + 1)
      sprite.drawString(String(yLabel), xPos - 4 + 1, GRAPH_Y_POSITION + 8);
    } 
    else if (yLabel == 0) {
      // Shift 0 up by 1px (y position -1)
      sprite.drawString("0" + String(yLabel), xPos - 3, GRAPH_Y_POSITION + 8 - 1);
    }
    else if (yLabel < 10) {
      sprite.drawString("0" + String(yLabel), xPos - 3, GRAPH_Y_POSITION + 8);
    }
    else {
      sprite.drawString(String(yLabel), xPos - 4, GRAPH_Y_POSITION + 8);
    }
  }

  // Draw graph grid (x-axis lines and labels)
  sprite.setTextDatum(4); // set text alignment to top-right (for all labels except 0)

  for (int i = 0; i <= 5; i++) {
    int yPos = GRAPH_Y_POSITION - (i * (GRAPH_HEIGHT / 5));
    String xLabel;
    
    // Format labels with right alignment
    if (i == 0) {
      xLabel = "  0"; // 0 with 2 leading spaces
      sprite.setTextDatum(6); // bottom-right alignment for 0
    } 
    else if (i*20 < 100) {
      xLabel = " " + String(i*20); // single space for 20-80
    }
    else {
      xLabel = String(i*20); // no space for 100
    }
    
    // Draw labels (adjusted positions left 20px & down 5px)
    sprite.drawString(xLabel, GRAPH_X_POSITION - 20, yPos + 5);
    
    // Skip drawing the bottom line when i = 0 since it's the X-axis
    if (i > 0) {
      sprite.drawLine(GRAPH_X_POSITION, yPos, 
                    GRAPH_X_POSITION + GRAPH_WIDTH, yPos, 
                    COLOUR_GRAY);
    }
  }
  
  sprite.setTextDatum(3); // reset to default alignment (middle-center)
  
  // Draw graph axes
  sprite.drawLine(GRAPH_X_POSITION, GRAPH_Y_POSITION, 
                 GRAPH_X_POSITION + GRAPH_WIDTH, GRAPH_Y_POSITION, TFT_WHITE);
  sprite.drawLine(GRAPH_X_POSITION, GRAPH_Y_POSITION, 
                 GRAPH_X_POSITION, GRAPH_Y_POSITION - GRAPH_HEIGHT, TFT_WHITE);
  
  // Draw graph line (with scaling from 0-100 to 0-GRAPH_HEIGHT)
  for(int i = 0; i < 23; i++) {
    int y1 = GRAPH_Y_POSITION - map(graphValues[i], 0, VALUE_CAP, 0, GRAPH_HEIGHT);
    int y2 = GRAPH_Y_POSITION - map(graphValues[i+1], 0, VALUE_CAP, 0, GRAPH_HEIGHT);
    
    // Draw 1st line
    sprite.drawLine(GRAPH_X_POSITION + (i*20), y1, 
                   GRAPH_X_POSITION + ((i+1) * 20), y2, 
                   TFT_RED);
    
    // Draw 2nd line (to make thicker line)
    sprite.drawLine(GRAPH_X_POSITION + (i*20), y1 - 1, 
                   GRAPH_X_POSITION + ((i+1) * 20), y2 - 1, 
                   TFT_RED);
  }
  
  // Draw additional info
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  // Battery info
  if (batteryConnected) {
    sprite.drawString("BAT:" + String(batteryVoltage) + "mV", GRAPH_X_POSITION + 150, 16);
  } else {
    sprite.drawString("BAT: N/C", GRAPH_X_POSITION + 150, 16);
  }
  // Display mode
  sprite.drawString("MODE:" + String(displayMode), GRAPH_X_POSITION + 150, 26);
  
  // Push sprite to display
  sprite.pushSprite(0, 0);
  
  delay(1); // short delay to free up CPU cycles
}
