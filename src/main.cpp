// Libraries
#include <Arduino.h>
#include <SPI.h>                  // OLED
#include <Wire.h>                 // RTC/OLED
#include <RTClib.h>               // RTC
#include <Adafruit_NeoPixel.h>    // LED STRIP
#include <Adafruit_GFX.h>         // OLED
#include <Adafruit_SSD1306.h>     // OLED

// Define pins and components
//#define LED_BUILTIN 2             // ESP32 builtin LED pin (defined in Arduino.h)
#define PHOTORESISTOR_PIN 34      // Photoresistor pin
#define BUTTON_1 26               // Button to change luminosity or deactivate the alarm
#define BUTTON_2 32               // Button to choose the LED lighting pattern
#define BUTTON_3 33               // Button long press activates time edit, short press increments parameter
#define BUTTON_4 25               // Button long press activates alarm edit, short press increments parameter

// OLED parameters
#define SCREEN_WIDTH    128       // OLED display width, in pixels
#define SCREEN_HEIGHT    64       // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C       // See datasheet for Address

// OLED SPI (VSPI interface)
#define OLED_MOSI 23              // VSPI MOSI
#define OLED_CLK  18              // VSPI SCK
#define OLED_CS    5              // VSPI SS
#define OLED_DC   19              // VSPI MISC
#define OLED_RESET 4              // Reset pin

// LED strip parameters
#define LED_PIN 27                // LED strip control pin
#define NUM_LEDS 60               // Number of LEDs on the LED strip

// Define the objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
RTC_DS3231 rtc;

// Define the cycle of the different luminosities of the LED strip
enum LightLuminosity { OFF_LUMINOSITY, DAY_LUMINOSITY, NIGHT_LUMINOSITY, AUTO_LUMINOSITY };

// --- VOLATILE Variables (Shared across Cores 0 and 1) ---
volatile LightLuminosity currentLightLuminosity = OFF_LUMINOSITY;
volatile int currentLightLuminosityIndex = 0;
volatile uint8_t gCurrentPatternNumber = 0;
volatile bool sunriseStarted = false;
volatile int globalFilteredLDR = 0;       
volatile bool isScreensaverActive = false;
volatile unsigned long lastInteractionTime = 0;

// LED strip patterns
#define FRAMES_PER_SECOND 60
uint8_t gHue = 0; // Base color used by patterns (Only used by TaskLED, no volatile needed)

// --- Screensaver & Smart Wake Constants ---
const unsigned long SCREENSAVER_TIMEOUT = 60000; // 60 seconds of inactivity
int ssX = 50; // Screensaver X position
int ssY = 30; // Screensaver Y position
unsigned long lastScreensaverMove = 0;

// --- Digital Filter (EMA - Exponential Moving Average) ---
float smoothedLDR = 0.0;
const float LDR_ALPHA = 0.05;    // 5% smoothing factor
const int WAKE_THRESHOLD = 1000; // Sudden light jump threshold to wake the screen

// Button variables
#define DEBOUNCE_DELAY 50
#define PRESSED_TIME 1000

// Button states
struct ButtonState {
  bool currentState;
  bool lastState;
  unsigned long lastDebounceTime;
  unsigned long pressStartTime;
  bool longPressExecuted;
};

ButtonState button1 = {HIGH, HIGH, 0, 0, false};
ButtonState button2 = {HIGH, HIGH, 0, 0, false};
ButtonState button3 = {HIGH, HIGH, 0, 0, false};
ButtonState button4 = {HIGH, HIGH, 0, 0, false};

// Display update interval
unsigned long previousMillisDisplay = 0;
const long displayInterval = 100;

// Alarm variables
int alarmHour = 7;
int alarmMinute = 30;
bool alarmActive = false;
unsigned long previousMillisSunrise = 0;
const long sunriseDuration = 600000;  // 10 minutes of sunrise simulation

// Time and alarm editing variables
bool editTime = false, editAlarm = false;
int timePart = 0, alarmPart = 0;

// Blinking variables for editing
unsigned long lastBlink = 0;
bool showValue = true;

// Task handles
TaskHandle_t taskDisplayHandle;
TaskHandle_t taskLEDHandle;
TaskHandle_t taskButtonHandle;
TaskHandle_t taskAlarmHandle;

// I2C Mutex
SemaphoreHandle_t i2cMutex;

// Function prototypes
void rainbow();
void rainbowWithGlitter();
void confetti();
void sinelon();
void juggle();
void bpm();
void solidRed();
void solidGreen();
void solidBlue();
void solidWhite();
void warmWhite();
void addGlitter(uint8_t chanceOfGlitter);
uint32_t dimColor(uint32_t color, uint8_t scale);
int beatsin16(int beatsPerMinute, int lowest, int highest);
void simulateSunrise();

// Pattern array
typedef void (*SimplePatternList[])();
SimplePatternList gPatterns = {rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm, solidRed, solidGreen, solidBlue, solidWhite, warmWhite};
const int PATTERN_COUNT = sizeof(gPatterns) / sizeof(gPatterns[0]);

// Smart LDR Filter Function
int getFilteredLDR() {
  int rawValue = analogRead(PHOTORESISTOR_PIN);
  
  if (smoothedLDR == 0.0) {
    smoothedLDR = rawValue;
  }
  
  // SMART WAKE LOGIC: If screen is sleeping and light turns on suddenly
  if (isScreensaverActive && ((rawValue - (int)smoothedLDR) > WAKE_THRESHOLD)) {
    isScreensaverActive = false;
    lastInteractionTime = millis(); // Reset interaction timer
  }
  
  // Apply the EMA filter
  smoothedLDR = (LDR_ALPHA * rawValue) + ((1.0 - LDR_ALPHA) * smoothedLDR);
  return (int)smoothedLDR;
}

// Display information function
void displayInfo() {
  // Initialize with safe default values (Year 2000 prevents garbage data if Mutex fails)
  DateTime now = DateTime(2000, 1, 1, 0, 0, 0);
  float temperature = 0.0;
  
  // I2C PROTECTION: Safely read RTC data
  if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    now = rtc.now();
    temperature = rtc.getTemperature();
    xSemaphoreGive(i2cMutex);
  }

  display.dim((now.hour() < 7) || (now.hour() > 22));
  display.clearDisplay();
  
  // SCREENSAVER MODE (Burn-in Protection)
  if (isScreensaverActive) {
    if (millis() - lastScreensaverMove > 20000) {    // Changes position every 20 seconds
      ssX = random(0, 65);                          // 128 (width) - approx 60 (text width)
      ssY = random(0, 45);                          // 64 (height) - 16 (text height)
      lastScreensaverMove = millis();
    }

    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(ssX, ssY);

    if (now.hour() < 10) 
      display.print("0");

    display.print(now.hour());
    display.print(":");

    if (now.minute() < 10) 
      display.print("0");

    display.print(now.minute());
    display.display();
    
    return; // Ignores drawing the rest of the UI!
  }

  // NORMAL MODE
  display.setTextColor(WHITE);
  display.setTextSize(3);

  bool showHour = !editTime || timePart != 0 || showValue;
  bool showMinute = !editTime || timePart != 1 || showValue;
  bool showSecond = !editTime || timePart != 2 || showValue;

  if (showHour) {
    display.setCursor(now.hour() < 10 ? 23 : 5, 0);
    display.print(now.hour());
  }
  
  display.setCursor(35, 0);
  display.print(":");
  
  if (showMinute) {
    display.setCursor(47, 0);
    if (now.minute() < 10) display.print("0");
    display.print(now.minute());
  }
  
  display.setCursor(77, 0);
  display.print(":");
  
  if (showSecond) {
    display.setCursor(89, 0);
    if (now.second() < 10) display.print("0");
    display.print(now.second());
  }

  display.setTextSize(1);

  // Display day of week and date
  display.setCursor(23, 24);
  const char* weekDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  display.print(weekDays[now.dayOfTheWeek()]);

  bool showDay = !editTime || timePart != 3 || showValue;
  bool showMonth = !editTime || timePart != 4 || showValue;
  bool showYear = !editTime || timePart != 5 || showValue;

  if (showDay) {
    display.setCursor(now.day() < 10 ? 49 : 43, 24);
    display.print(now.day());
  }
  display.setCursor(55, 24);
  display.print("/");
  
  if (showMonth) {
    display.setCursor(61, 24);
    if (now.month() < 10) display.print("0");
    display.print(now.month());
  }
  display.setCursor(73, 24);
  display.print("/");
  
  if (showYear) {
    display.setCursor(79, 24);
    display.print(now.year());
  }

  display.setTextSize(2);

  // Display ambient luminosity using the GLOBAL Filtered LDR
  int ambientLuminosity = map(globalFilteredLDR, 0, 4095, 0, 100);
  
  display.setCursor(0, 32);
  display.print("B");
  display.setCursor(12, 32);
  display.print("%");
  display.setCursor(24, 32);
  display.print(ambientLuminosity);

  // Display temperature
  display.setCursor(66, 32);
  display.print(temperature, 1);
  display.setCursor(114, 32);
  display.print("C");

  // Display alarm time with blinking for editing
  bool showAlarmHour = !editAlarm || alarmPart != 0 || showValue;
  bool showAlarmMinute = !editAlarm || alarmPart != 1 || showValue;

  if (showAlarmHour) {
    display.setCursor(alarmHour < 10 ? 50 : 38, 48);
    display.print(alarmHour);
  }
  display.setCursor(58, 48);
  display.print(":");
  
  if (showAlarmMinute) {
    display.setCursor(66, 48);
    if (alarmMinute < 10) display.print("0");
    display.print(alarmMinute);
  }

  display.setTextSize(1);

  // Display LED strip mode
  display.setCursor(0, 56);
  switch(currentLightLuminosityIndex) {
    case 0: display.print("OFF"); break;
    case 1: display.print("DAY"); break;
    case 2: display.print("NIGHT"); break;
    case 3: display.print("AUTO"); break;
  }

  // Display LED pattern number
  display.setCursor(110, 56);
  display.print("P");
  display.print(gCurrentPatternNumber);

  // Show edit mode indicators
  if (editTime) {
    display.setCursor(100, 0);
    display.print("T");
  }
  if (editAlarm) {
    display.setCursor(100, 48);
    display.print("A");
  }

  display.display();
}

// Task for display updates
void taskDisplay(void *parameters) {
  for(;;) {
    // Read sensor continuously (Updates EMA and detects light regardless of screen state)
    globalFilteredLDR = getFilteredLDR();

    unsigned long currentMillis = millis();
    
    // Check Screensaver timeout
    if (!isScreensaverActive && (currentMillis - lastInteractionTime > SCREENSAVER_TIMEOUT)) {
      isScreensaverActive = true;
      editTime = false;   // Prevention: exit edit modes when sleeping
      editAlarm = false;
    }

    // Update Display
    if (currentMillis - previousMillisDisplay >= displayInterval) {
      previousMillisDisplay = currentMillis;
      
      // Update blinking for the current editing field
      if (millis() - lastBlink > 500) {
        showValue = !showValue;
        lastBlink = millis();
      }
      
      displayInfo();
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// Button debouncing function
bool debounceButton(ButtonState &btn, int pin) {
  bool reading = digitalRead(pin);
  bool buttonPressed = false;
  
  if (reading != btn.lastState) {
    btn.lastDebounceTime = millis();
  }
  
  if ((millis() - btn.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != btn.currentState) {
      btn.currentState = reading;
      
      if (btn.currentState == LOW) {
        btn.pressStartTime = millis();
        btn.longPressExecuted = false;
      } else {
        if (!btn.longPressExecuted && (millis() - btn.pressStartTime) < PRESSED_TIME) {
          buttonPressed = true;
        }
      }
    }
    
    if (btn.currentState == LOW && !btn.longPressExecuted) {
      if ((millis() - btn.pressStartTime) >= PRESSED_TIME) {
        btn.longPressExecuted = true;
        return false;
      }
    }
  }
  
  btn.lastState = reading;
  return buttonPressed;
}

bool checkLongPress(ButtonState &btn) {
  return btn.longPressExecuted && btn.currentState == HIGH;
}

// Task for button handling
void taskButton(void *parameters) {
  for(;;) {
    // Analyze the state of all buttons in the cycle
    bool b1 = debounceButton(button1, BUTTON_1);
    bool b2 = debounceButton(button2, BUTTON_2);
    bool b3 = debounceButton(button3, BUTTON_3);
    bool b4 = debounceButton(button4, BUTTON_4);
    bool lp3 = checkLongPress(button3);
    bool lp4 = checkLongPress(button4);

    // The Screensaver "Shield"
    if (b1 || b2 || b3 || b4 || lp3 || lp4) {
      lastInteractionTime = millis(); // Any touch resets the timer
      
      if (isScreensaverActive) {
        isScreensaverActive = false; // Wakes up the screen
        button3.longPressExecuted = false; // Clears flags to avoid entering edit mode accidentally
        button4.longPressExecuted = false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
        continue; // Ignores the actions below in this exact cycle!
      }
    }

    // Button 1: Change luminosity mode or turn off alarm
    if (b1) {
      if (sunriseStarted) {
        sunriseStarted = false;
        currentLightLuminosityIndex = 0; // Forces OFF mode safely
        currentLightLuminosity = OFF_LUMINOSITY; 
      } else {
        currentLightLuminosityIndex = (currentLightLuminosityIndex + 1) % 4;
        currentLightLuminosity = (LightLuminosity)currentLightLuminosityIndex;
      }
    }

    // Button 2: Change LED pattern
    if (b2) {
      gCurrentPatternNumber = (gCurrentPatternNumber + 1) % PATTERN_COUNT;
    }

    // Button 3: Time editing
    if (lp3) {
      if (editTime) {
        timePart = (timePart + 1) % 6;
        if (timePart == 0) editTime = false;
      } else {
        editTime = true;
        timePart = 0;
      }
      button3.longPressExecuted = false;
    } else if (b3 && editTime) {
      // Initialize with safe default value
      DateTime now = DateTime(2000, 1, 1, 0, 0, 0);
      
      // I2C Protection: Read time before modifying
      if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
        now = rtc.now();
        xSemaphoreGive(i2cMutex);
      }
      
      // Only proceed if we got a valid reading
      if (now.year() > 2000) {
        int h = now.hour(), m = now.minute(), s = now.second();
        int d = now.day(), mo = now.month(), y = now.year();
        
        switch(timePart) {
          case 0: h = (h + 1) % 24; break;
          case 1: m = (m + 1) % 60; break;
          case 2: s = (s + 1) % 60; break;
          case 3: d = (d % 31) + 1; break;
          case 4: mo = (mo % 12) + 1; break;
          case 5: y = y + 1; break;
        }
        
        // I2C Protection: Safely adjust new time
        if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
          rtc.adjust(DateTime(y, mo, d, h, m, s));
          xSemaphoreGive(i2cMutex);
        }
      }
    }

    // Button 4: Alarm editing
    if (lp4) {
      if (editAlarm) {
        alarmPart = (alarmPart + 1) % 2;
        if (alarmPart == 0 && editAlarm) editAlarm = false;
      } else {
        editAlarm = true;
        alarmPart = 0;
      }
      button4.longPressExecuted = false;
    } else if (b4 && editAlarm) {
      switch(alarmPart) {
        case 0: alarmHour = (alarmHour + 1) % 24; break;
        case 1: alarmMinute = (alarmMinute + 1) % 60; break;
      }
    }

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// Check alarm function
void checkAlarm() {
  // Initialize with a default value to prevent garbage data
  DateTime now = DateTime(2000, 1, 1, 0, 0, 0);
  static int lastAlarmDay = -1; // Tracks the last day the alarm was triggered
  
  // I2C Protection: Safely reads RTC data
  if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    now = rtc.now();
    xSemaphoreGive(i2cMutex);
  }
  
  // Check if it's the correct time, ensuring it only triggers once per day
  if (now.year() > 2000 && now.hour() == alarmHour && now.minute() == alarmMinute && !sunriseStarted) {
    if (lastAlarmDay != now.day()) {
      sunriseStarted = true;
      previousMillisSunrise = millis();
      lastAlarmDay = now.day();
    }
  }
}

// Task for alarm checking
void taskAlarm(void *parameters) {
  for(;;) {
    checkAlarm();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Sunrise simulation
void simulateSunrise() {
  unsigned long currentMillis = millis();
  if (sunriseStarted && (currentMillis - previousMillisSunrise) <= sunriseDuration) {
    float progress = (float)(currentMillis - previousMillisSunrise) / sunriseDuration;
    
    uint8_t red = (uint8_t)(255 * (0.8 + 0.2 * progress));
    uint8_t green = (uint8_t)(100 * progress + 50);
    uint8_t blue = (uint8_t)(255 * progress * progress);
    uint8_t brightness = (uint8_t)(255 * progress);
    
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(red, green, blue));
    }
    
    strip.setBrightness(brightness);
    strip.show();
  } else if (sunriseStarted && (currentMillis - previousMillisSunrise) > sunriseDuration) {
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(255, 255, 255));
    }
    strip.setBrightness(255);
    strip.show();
  }
}

// LED pattern functions
void rainbow() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t color = strip.ColorHSV((gHue * 65536 / 255) + (i * 65536 / NUM_LEDS), 255, 255);
    strip.setPixelColor(i, color);
  }
}

void rainbowWithGlitter() {
  rainbow();
  if (random(100) < 20) {
    addGlitter(80);
  }
}

void addGlitter(uint8_t chanceOfGlitter) {
  if (random(255) < chanceOfGlitter) {
    int pos = random(NUM_LEDS);
    strip.setPixelColor(pos, strip.Color(255, 255, 255));
  }
}

void confetti() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t currentColor = strip.getPixelColor(i);
    currentColor = dimColor(currentColor, 250);
    strip.setPixelColor(i, currentColor);
  }
  int pos = random(NUM_LEDS);
  uint32_t color = strip.ColorHSV(gHue * 65536 / 255 + random(65536 / 4), 255, 255);
  strip.setPixelColor(pos, color);
}

uint32_t dimColor(uint32_t color, uint8_t scale) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  return strip.Color(r * scale / 255, g * scale / 255, b * scale / 255);
}

void sinelon() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t currentColor = strip.getPixelColor(i);
    currentColor = dimColor(currentColor, 250);
    strip.setPixelColor(i, currentColor);
  }
  int pos = beatsin16(13, 0, NUM_LEDS - 1);
  strip.setPixelColor(pos, strip.ColorHSV(gHue * 65536 / 255, 255, 255));
}

void bpm() {
  uint8_t BeatsPerMinute = 62;
  uint8_t beat = beatsin16(BeatsPerMinute, 64, 255);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t color = strip.ColorHSV(gHue * 65536 / 255 + (i * 65536 / NUM_LEDS), 255, beat);
    strip.setPixelColor(i, color);
  }
}

void solidRed() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(255, 0, 0));
  }
}

void solidGreen() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(0, 255, 0));
  }
}

void solidBlue() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 255));
  }
}

void solidWhite() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(255, 255, 255));
  }
}

void warmWhite() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(255, 110, 18));
  }
}

void juggle() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t currentColor = strip.getPixelColor(i);
    currentColor = dimColor(currentColor, 20);
    strip.setPixelColor(i, currentColor);
  }
  for (int i = 0; i < 8; i++) {
    int pos = beatsin16(i + 7, 0, NUM_LEDS - 1);
    strip.setPixelColor(pos, strip.ColorHSV(gHue * 65536 / 255 + (i * 65536 / 8), 255, 255));
  }
}

int beatsin16(int beatsPerMinute, int lowest, int highest) {
  unsigned long ms = millis();
  float cycle = 60000.0 / beatsPerMinute;
  float phase = fmod(ms, cycle) / cycle * 2 * PI;
  return lowest + (highest - lowest) * (0.5 + 0.5 * sin(phase));
}

// Set LED brightness based on mode
void setLights() {
  if (sunriseStarted) {
    simulateSunrise();
    return;
  }

  switch (currentLightLuminosity) {
    case OFF_LUMINOSITY:
      strip.clear(); // Ensure perfect shutdown
      strip.setBrightness(0);
      break;
    case DAY_LUMINOSITY:
      strip.setBrightness(255);
      break;
    case NIGHT_LUMINOSITY:
      strip.setBrightness(20);
      break;
    case AUTO_LUMINOSITY: {
      // Map the sensor reading to a 0-255 scale (Linear)
      long linearBase = map(globalFilteredLDR, 0, 4095, 0, 255);
      
      // Apply Exponential (Quadratic) mathematical curve
      // Multiplying by itself and dividing by the maximum (255) creates the perfect curve!
      int brightness = 10 + ((linearBase * linearBase) / 255);
      
      // Ensure safety limit
      if (brightness > 255) brightness = 255;
      
      strip.setBrightness(brightness);

      break;
    }
  }
}

// Task for LED control
void taskLED(void *parameters) {
  for(;;) {
    setLights();
    
    if (!sunriseStarted && currentLightLuminosity != OFF_LUMINOSITY) {
      gPatterns[gCurrentPatternNumber]();
    }
    
    strip.show();
    gHue++;
    
    vTaskDelay(1000 / FRAMES_PER_SECOND / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize MUTEX (Before starting I2C)
  i2cMutex = xSemaphoreCreateMutex();
  
  Wire.begin(21, 22);
  
  // Ensure I2C is protected during startup (Safety precaution)
  if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    if (!rtc.begin()) {
      Serial.println("RTC not found!");
      while (1);
    }
    
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, adjusting...");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    xSemaphoreGive(i2cMutex);
  }
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Failed to initialize OLED");
    while (1);
  }
  display.clearDisplay();
  display.display();
  
  strip.begin();
  strip.clear();
  strip.show();
  
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);
  pinMode(PHOTORESISTOR_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 20);
  display.println("Welcome");
  display.display();
  delay(2000);
  
  xTaskCreatePinnedToCore(taskDisplay, "Display", 4096, NULL, 1, &taskDisplayHandle, 0);
  xTaskCreatePinnedToCore(taskLED, "LED", 4096, NULL, 2, &taskLEDHandle, 1);
  xTaskCreatePinnedToCore(taskButton, "Button", 4096, NULL, 3, &taskButtonHandle, 0);
  xTaskCreatePinnedToCore(taskAlarm, "Alarm", 2048, NULL, 1, &taskAlarmHandle, 0);
}

void loop() {}