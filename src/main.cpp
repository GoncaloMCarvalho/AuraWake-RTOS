// Libraries
#include <Arduino.h>
#include <SPI.h>                  //OLED
#include <Wire.h>                 //RTC/OLED
#include <RTClib.h>               //RTC
#include <Adafruit_NeoPixel.h>    //LED STRIP
#include <Adafruit_GFX.h>         //OLED
#include <Adafruit_SSD1306.h>     //OLED

// Define pins and components
//#define LED_BUILTIN 2             // Esp32 builtin led pin (defined in Arduino.h)
#define PHOTORESISTOR_PIN 34      // Photoresistor pin
#define BUTTON_1 26               // Botão para mudar a luminosidade ou desativar o alarme
#define BUTTON_2 32               // Botão para escolher o efeito da iluminacao dos leds
#define BUTTON_3 33               // Botão long press ativa mudança de hora, curto incrementa parâmetro
#define BUTTON_4 25               // Botão long press ativa mudança de alarme, curto incrementa parâmetro

// OLED parameters
#define SCREEN_WIDTH    128       // OLED display width, in pixels
#define SCREEN_HEIGHT    64       // OLED display height, in pixels
//#define OLED_RESET       -1       // Reset pin # (or -1 if sharing Arduino reset pin) (not needed, using external reset pin)
#define SCREEN_ADDRESS 0x3C       // See datasheet for Address

// OLED SPI (VSPI interface)
#define OLED_MOSI 23              // VSPI MOSI
#define OLED_CLK  18              // VSPI SCK
#define OLED_CS    5              // VSPI SS
#define OLED_DC   19              // VSPI MISC
#define OLED_RESET 4              // Reset pin

//LED strip parameters
#define LED_PIN 27                // LED strip control pin
#define NUM_LEDS 60               // Number of LEDs on the LED strip

// Define the objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
RTC_DS3231 rtc;

// Define the cycle of the different luminosities of the LED strip
enum LightLuminosity { OFF_LUMINOSITY, DAY_LUMINOSITY, NIGHT_LUMINOSITY, AUTO_LUMINOSITY };
LightLuminosity currentLightLuminosity = OFF_LUMINOSITY;
int currentLightLuminosityIndex = 0;

// LED strip patterns
#define FRAMES_PER_SECOND 60
uint8_t gCurrentPatternNumber = 0;       // Index of the current pattern
uint8_t gHue = 0;                        // Base color used by the patterns

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
bool sunriseStarted = false;
unsigned long previousMillisSunrise = 0;
const long sunriseDuration = 600000;  // 10 minutes of sunrise simulation

// Time and alarm editing variables
bool editTime = false, editAlarm = false;
int timePart = 0, alarmPart = 0;  // 0=hour, 1=minute, 2=second, 3=day, 4=month, 5=year for time

// Blinking variables for editing
unsigned long lastBlink = 0;
bool showValue = true;

// Task handles
TaskHandle_t taskDisplayHandle;
TaskHandle_t taskLEDHandle;
TaskHandle_t taskButtonHandle;
TaskHandle_t taskAlarmHandle;

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

// Display information function
void displayInfo() {
  DateTime now = rtc.now();
  
  // Dim display at night
  display.dim((now.hour() < 7) || (now.hour() > 22));
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(3);

  // Display current time with blinking for editing
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
  const char* diasSemana[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};
  display.print(diasSemana[now.dayOfTheWeek()]);

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

  // Display ambient luminosity
  int sensorValue = analogRead(PHOTORESISTOR_PIN);
  int ambientLuminosity = map(sensorValue, 0, 4095, 0, 100);
  
  display.setCursor(0, 32);
  display.print("B");
  display.setCursor(12, 32);
  display.print("%");
  display.setCursor(24, 32);
  display.print(ambientLuminosity);

  // Display temperature
  float temperature = rtc.getTemperature();
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
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillisDisplay >= displayInterval) {
      previousMillisDisplay = currentMillis;
      
      // Update blinking for editing
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
      
      if (btn.currentState == LOW) {  // Button pressed
        btn.pressStartTime = millis();
        btn.longPressExecuted = false;
      } else {  // Button released
        if (!btn.longPressExecuted && (millis() - btn.pressStartTime) < PRESSED_TIME) {
          buttonPressed = true;  // Short press detected
        }
      }
    }
    
    // Check for long press
    if (btn.currentState == LOW && !btn.longPressExecuted) {
      if ((millis() - btn.pressStartTime) >= PRESSED_TIME) {
        btn.longPressExecuted = true;
        return false;  // Will be handled separately
      }
    }
  }
  
  btn.lastState = reading;
  return buttonPressed;
}

// Check for long press
bool checkLongPress(ButtonState &btn) {
  return btn.longPressExecuted && btn.currentState == HIGH;
}

// Task for button handling
void taskButton(void *parameters) {
  for(;;) {
    // Button 1: Change luminosity mode or turn off alarm
    if (debounceButton(button1, BUTTON_1)) {
      if (sunriseStarted) {
        // Turn off alarm
        sunriseStarted = false;
        strip.setBrightness(0);
        strip.clear();
        strip.show();
      } else {
        // Change luminosity mode
        currentLightLuminosityIndex = (currentLightLuminosityIndex + 1) % 4;
        currentLightLuminosity = (LightLuminosity)currentLightLuminosityIndex;
      }
    }

    // Button 2: Change LED pattern
    if (debounceButton(button2, BUTTON_2)) {
      gCurrentPatternNumber = (gCurrentPatternNumber + 1) % PATTERN_COUNT;
    }

    // Button 3: Time editing
    if (checkLongPress(button3)) {
      if (editTime) {
        timePart = (timePart + 1) % 6;  // 0-5 for hour, minute, second, day, month, year
        if (timePart == 0) editTime = false;  // Exit edit mode after year
      } else {
        editTime = true;
        timePart = 0;
      }
      button3.longPressExecuted = false;
    } else if (debounceButton(button3, BUTTON_3) && editTime) {
      // Increment time component
      DateTime now = rtc.now();
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
      rtc.adjust(DateTime(y, mo, d, h, m, s));
    }

    // Button 4: Alarm editing
    if (checkLongPress(button4)) {
      if (editAlarm) {
        alarmPart = (alarmPart + 1) % 2;  // 0-1 for hour, minute
        if (alarmPart == 0 && editAlarm) editAlarm = false;  // Exit edit mode
      } else {
        editAlarm = true;
        alarmPart = 0;
      }
      button4.longPressExecuted = false;
    } else if (debounceButton(button4, BUTTON_4) && editAlarm) {
      // Increment alarm component
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
  DateTime now = rtc.now();
  
  if (now.hour() == alarmHour && now.minute() == alarmMinute && now.second() == 0 && !sunriseStarted) {
    sunriseStarted = true;
    previousMillisSunrise = millis();
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
    
    // Color gradient from deep orange to bright white
    uint8_t red = (uint8_t)(255 * (0.8 + 0.2 * progress));
    uint8_t green = (uint8_t)(100 * progress + 50);
    uint8_t blue = (uint8_t)(255 * progress * progress);  // Slower blue progression
    
    uint8_t brightness = (uint8_t)(255 * progress);
    
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(red, green, blue));
    }
    
    strip.setBrightness(brightness);
    strip.show();
  } else if (sunriseStarted && (currentMillis - previousMillisSunrise) > sunriseDuration) {
    // Full bright white after sunrise
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
  if (random(100) < 20) {  // 20% chance of glitter
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

// Solid color patterns
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

//Temperature R, G, B
//2600K	255, 177, 110
//2200K	255, 147, 44
//2000K	255, 110, 18
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
      strip.setBrightness(0);
      break;
    case DAY_LUMINOSITY:
      strip.setBrightness(255);
      break;
    case NIGHT_LUMINOSITY:
      strip.setBrightness(20);
      break;
    case AUTO_LUMINOSITY:
      int sensorValue = analogRead(PHOTORESISTOR_PIN);
      int brightness = map(sensorValue, 0, 4095, 10, 255);
      strip.setBrightness(brightness);
      break;
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
  
  // Initialize I2C
  Wire.begin(21, 22);
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1);
  }
  
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, adjusting...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Failed to initialize OLED");
    while (1);
  }
  display.clearDisplay();
  display.display();
  
  // Initialize LED strip
  strip.begin();
  strip.clear();
  strip.show();
  
  // Configure pins
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);
  pinMode(PHOTORESISTOR_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Startup indication
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Show welcome message
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 20);
  display.println("Welcome");
  display.display();
  delay(2000);
  
  // Create tasks
  xTaskCreatePinnedToCore(taskDisplay, "Display", 4096, NULL, 1, &taskDisplayHandle, 0);
  xTaskCreatePinnedToCore(taskLED, "LED", 4096, NULL, 2, &taskLEDHandle, 1);
  xTaskCreatePinnedToCore(taskButton, "Button", 4096, NULL, 3, &taskButtonHandle, 0);
  xTaskCreatePinnedToCore(taskAlarm, "Alarm", 2048, NULL, 1, &taskAlarmHandle, 0);
}

void loop() {
  // Empty - everything handled by FreeRTOS tasks
}