/*
   This sketch turns a M5Stack Core2 + hx711 Load Cell into a kitchen utility:
   1) standard kitchen weight
   2) weight with flow graph for pour over
   3) Espresso Timer with shot flow profiling and stats
*/

#include <M5Unified.h>

#include <Preferences.h>
Preferences preferences;

struct PreferencesData {
  float scaleCalFac;
  long scaleOffset;
  uint8_t espressoGraphMaxWeight;
  uint8_t maxExtractionTimeSec;
};
PreferencesData prefsData;

enum MenuState {
  MENU_SCALE_CAL_FAC,
  MENU_SCALE_OFFSET,
  MENU_GRAPH_MAX_WEIGHT,
  MENU_MAX_EXTRACTION_TIME,
  MENU_SAVE_PREFERENCES
};
MenuState currentMenuState = MENU_SCALE_CAL_FAC;

// Graphics
#include <M5GFX.h>
M5GFX display;
M5Canvas canvas(&display);
M5Canvas scroll_1(&canvas);
M5Canvas scroll_2(&canvas);

M5Canvas fixedGraphOverlay(&canvas);
M5Canvas fixedGraph(&fixedGraphOverlay);
int32_t displayWidth;
int32_t displayHeight;

// Buttons
enum Button {
  None,
  BtnA,
  BtnB,
  BtnC,
  BtnPWR,
  TouchEvent  // Add a TouchEvent enum value
};

// sleep & wakeup
// #include "AXP192.h"   //using this results in an error when compiling .... sometimes.  dunno why tho
unsigned long int lastActionMillis;
#define SLEEP_MILLIS 240000  // go to sleep after x millis
#define TOUCH_WAKEUP_S 600

// HX711 weight module
#include "HX711.h"
HX711 scale;
#define WEIGHT_SAMPLE_MILLIS 15  // hx711 can do ~80x/s = 12,5 millis min
#define LOADCELL_DOUT_PIN 33
#define LOADCELL_SCK_PIN 32
float weight = 0.0;
int lastScaleMillis = 0;

// battery
unsigned long int lastBatteryCheckMillis = 0;
int32_t batteryLevel = 0;

// design // get color values from from http://www.barth-dev.de/online/rgb565-color-picker/
#define BACKGROUND_COLOR BLACK
#define TEXT_COLOR WHITE
#define ACCENT_COLOR WHITE
#define SHADOW_COLOR 0x2945
#define HIGHLIGHT_COLOR RED
#define MENU_HEIGHT 25
#define GRAPH_LINE_WIDTH 3

// draw quicker onto the canvas using sprites and sprintf
char stringBuffer[100];
char stringBuffer2[100];

//prototyping functions
void drawMenu(String, String, String);
void playSound(String);
void sleepCheck();
float getWeight(float);
void modeKitchenScale();
void modeTimer();
void modeEspresso();
void modeScaleCalibrate();
void loadPreferences();
void refreshCanvas();

void setup() {
  preferences.begin("M5-espr-scale", true);  // readonly = true
  loadPreferences();

  M5.begin();          // Init M5Core2.
  M5.Speaker.begin();  // Initialize the speaker.
  M5.Speaker.setVolume(64);

  //M5.Power.setPowerBoostSet(true); // power ON / OFF in one short press. // does not work in 

  display.begin();
  displayWidth = display.width();
  displayHeight = display.height();
  canvas.createSprite(displayWidth, displayHeight);
  canvas.setTextColor(TEXT_COLOR);
  canvas.drawCentreString("Grind finer!", displayWidth / 2, displayHeight / 2);
  canvas.pushSprite(0, 0);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_offset(prefsData.scaleOffset);
  scale.set_scale(prefsData.scaleCalFac);
  scale.tare();
  lastActionMillis = millis();
  playSound("tick");

  // powerdown after TOUCH_WAKEUP_S
  if (esp_sleep_get_wakeup_cause() == 4) {  // when you wakeup after deepsleep timer (touch == 2, restet / power == 0)
    playSound("beep");
    playSound("tick");
    playSound("beep");
    M5.Power.powerOff();
  }
}
void loop() {
  refreshCanvas();
  weight = getWeight(weight);
  canvas.setTextSize(6);
  sprintf(stringBuffer, " %.1fg", weight);
  canvas.drawRightString(String(stringBuffer), displayWidth - 10, 80);

  if (lastBatteryCheckMillis == 0 || millis() > lastBatteryCheckMillis + 30000) {  // update batteryLevel every 30s
    lastBatteryCheckMillis = millis();
    batteryLevel = M5.Power.getBatteryLevel();
  }
  canvas.setTextSize(1);
  sprintf(stringBuffer, "Battery: %d%%", batteryLevel);
  canvas.drawRightString(String(stringBuffer), displayWidth, 5);

  drawMenu("Scale", "Tare", "Espresso");
  switch (getPressedButton()) {
    case None:
      // No button pressed
      break;
    case BtnA:
      modeKitchenScale();
      break;
    case BtnB:
      // modeTimer();
      scale.tare();
      flashDisplay();
      break;
    case BtnC:
      modeEspresso();
      break;
  }
}

// handle buttons and return the one that was pressed
Button getPressedButton() {
  if(millis() - lastActionMillis < 1000) return None; // prevent double taps when entering new mode
  M5.update();  // This function reads the state of Button A, B, C
  if (M5.BtnA.wasReleased()) {
    playSound("tick");
    return BtnA;
  } else if (M5.BtnB.wasReleased()) {
    playSound("tick");
    return BtnB;
  } else if (M5.BtnC.wasReleased()) {
    playSound("tick");
    return BtnC;
  } else if (M5.BtnPWR.wasReleased()) {
    playSound("tick");
    return BtnPWR;
  }
  /*
  // Check if the touch coordinates are within the specified area
  if (touchX >= areaXMin && touchX <= areaXMax && touchY >= areaYMin && touchY <= areaYMax) {
    // The touch event is within the specified area
    // You can perform additional actions or return a custom value here if needed.
    return TouchEvent;
  }
  */
  return None;  // No button or touch event detected
}
void drawMenu(String TextBtnA, String TextBtnB, String TextBtnC) {
  sleepCheck();
  canvas.setTextSize(2);
  canvas.drawLine(0, displayHeight - MENU_HEIGHT, displayWidth, displayHeight - MENU_HEIGHT, ACCENT_COLOR);
  canvas.drawCentreString(TextBtnA, 55, displayHeight - 20);
  canvas.drawCentreString(TextBtnB, displayWidth / 2, displayHeight - 20);
  canvas.drawCentreString(TextBtnC, displayWidth - 55, displayHeight - 20);
}
void playSound(String sound) {
  lastActionMillis = millis();  // we reset the inactivity counter to delay the sleep timer
  if (sound == "beep") M5.Speaker.tone(2000, 100);
  if (sound == "tick") M5.Speaker.tone(5000, 20);
}
void sleepCheck() {  // when in deep sleep, you can wake by tapping the display. poweroff saves battery
  if (millis() - lastActionMillis > SLEEP_MILLIS) {
    M5.Power.deepSleep(TOUCH_WAKEUP_S * 1000000, true);  /// after TOUCH_WAKEUP_S of sleeping,power off
  }
}
float getWeight(float prevWeight) {  // if measured weight is similar to stored weight, smoothen with a higher factor.
  float measuredWeight = scale.get_units(1);
  float factor = 1.0;                            // default: get only measured weight ...
  if (abs(prevWeight - measuredWeight) < 0.6) {  // ... unless we get into fluctuation territory....
    factor = 0.01;                               // .... then smoothen the curve
  }
  return (1.0 - factor) * prevWeight + factor * measuredWeight;
}
void modeKitchenScale() {
  refreshCanvas();
  boolean runLoop = true;
  int i = 0;
  int lastScaleMillis = 0;
  unsigned long int scrollMillis = millis();
  int scrollOffset = 0;
  int scrollSpeedMillis = 20;
  boolean scrollOrder12 = true;
  int scroll_1_Pos = 0;
  int scroll_2_Pos = displayWidth;
  int yPos_weight = 0;
  int scrollCanvasHeight = displayHeight / 2;
  int scrollCanvasMaxWeight = 200;
  scroll_1.createSprite(displayWidth, scrollCanvasHeight);
  scroll_1.fillSprite(BACKGROUND_COLOR);
  scroll_1.drawLine(0, 0, scroll_1.width(), 0, ACCENT_COLOR);
  scroll_1.drawLine(0, scroll_1.height() / 2, scroll_1.width(), scroll_1.height() / 2, ACCENT_COLOR);
  scroll_1.drawLine(0, scroll_1.height(), scroll_1.width(), scroll_1.height(), ACCENT_COLOR);
  scroll_2.createSprite(displayWidth, scrollCanvasHeight);
  scroll_2.fillSprite(BACKGROUND_COLOR);
  scroll_2.drawLine(0, 0, scroll_2.width(), 0, ACCENT_COLOR);
  scroll_2.drawString(String(scrollCanvasMaxWeight), 0, 0);
  scroll_2.drawLine(0, scroll_2.height() / 2, scroll_2.width(), scroll_2.height() / 2, ACCENT_COLOR);
  scroll_2.drawString(String(scrollCanvasMaxWeight / 2), 0, scroll_2.height() / 2);
  scroll_2.drawLine(0, scroll_2.height(), scroll_2.width(), scroll_2.height(), ACCENT_COLOR);
  scale.tare();
  while (runLoop) {
    refreshCanvas();
    weight = getWeight(weight);

    canvas.setTextSize(3);
    sprintf(stringBuffer, "%.2fg", weight);
    canvas.drawRightString(String(stringBuffer), displayWidth - 10, 10);

    // scrolling graph using 2 (sub)canvas
    scrollOffset = millis() - scrollMillis;
    if (scrollOffset > (displayWidth * scrollSpeedMillis)) {  // switch both canvases!
      scrollCanvasMaxWeight = ceil(weight / 100) * 100;
      scrollOffset = 0;
      scrollMillis = millis();
      if (scrollOrder12) {
        scroll_1.fillSprite(BACKGROUND_COLOR);
        scroll_1.drawLine(0, 0, scroll_1.width(), 0, ACCENT_COLOR);
        scroll_1.drawString(String(scrollCanvasMaxWeight), 0, 0);
        scroll_1.drawLine(0, scroll_1.height() / 2, scroll_1.width(), scroll_1.height() / 2, ACCENT_COLOR);
        scroll_1.drawString(String(scrollCanvasMaxWeight / 2), 0, scroll_1.height() / 2);
        scroll_1.drawLine(0, scroll_1.height() - 1, scroll_1.width(), scroll_1.height() - 1, ACCENT_COLOR);

      } else {
        scroll_2.fillSprite(BACKGROUND_COLOR);
        scroll_2.drawLine(0, 0, scroll_2.width(), 0, ACCENT_COLOR);
        scroll_2.drawString(String(scrollCanvasMaxWeight), 0, 0);
        scroll_2.drawLine(0, scroll_2.height() / 2, scroll_2.width(), scroll_2.height() / 2, ACCENT_COLOR);
        scroll_2.drawString(String(scrollCanvasMaxWeight / 2), 0, scroll_2.height() / 2);
        scroll_2.drawLine(0, scroll_2.height() - 1, scroll_2.width(), scroll_2.height() - 1, ACCENT_COLOR);
      }
      scrollOrder12 = !scrollOrder12;
    }
    scroll_1_Pos = -(scrollOffset / scrollSpeedMillis);
    scroll_2_Pos = scroll_1_Pos;
    yPos_weight = scrollCanvasHeight - map(weight, 0, scrollCanvasMaxWeight, 0, scrollCanvasHeight);
    if (scrollOrder12) {
      scroll_2_Pos += displayWidth;
      for (int i = 1; i < GRAPH_LINE_WIDTH; i++) {
        scroll_2.drawCircle(displayWidth - scroll_2_Pos, yPos_weight, i, ACCENT_COLOR);
      }
    } else {
      scroll_1_Pos += displayWidth;
      for (int i = 1; i < GRAPH_LINE_WIDTH; i++) {
        scroll_1.drawCircle(displayWidth - scroll_2_Pos, yPos_weight, i, ACCENT_COLOR);
      }
    }
    scroll_1.pushSprite(scroll_1_Pos, displayHeight / 4);
    scroll_2.pushSprite(scroll_2_Pos, displayHeight / 4);

    // Menu
    drawMenu("back", "tare", "calibrate");
    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        runLoop = false;
        break;
      case BtnB:
        scale.set_offset(prefsData.scaleOffset);
        scale.tare();
        canvas.fillSprite(BACKGROUND_COLOR);
        break;
      case BtnC:
        modeScaleCalibrate();
        break;
    }
  }
}
void modeScaleCalibrate() {  // NOTE: check out https://docs.m5stack.com/en/app/scales_kit
  boolean runLoop = true;
  boolean runLoopCalibrate = true;
  float calWeight = 100.0;
  scale.tare();
  prefsData.scaleOffset = scale.get_offset();
  while (runLoop) {
    refreshCanvas();
    
    sprintf(stringBuffer, "calib factor: %.4f", prefsData.scaleCalFac);
    canvas.drawString(String(stringBuffer), 10, 15);
    sprintf(stringBuffer, "Calibration weight: %.0fg", calWeight);
    canvas.drawString(String(stringBuffer), 10, 45);
    sprintf(stringBuffer, "Offset: %i", prefsData.scaleOffset);
    canvas.drawString(String(stringBuffer), 10, 75);
    sprintf(stringBuffer, "put calibration weight ");
    canvas.drawString(String(stringBuffer), 10, 105);
    sprintf(stringBuffer, "and press start");
    canvas.drawString(String(stringBuffer), 10, 135);

    drawMenu("back", "start", "test sensor");
    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        runLoop = false;
        break;
      case BtnB:
        runLoopCalibrate = true;
        runLoop = false;
        break;
      case BtnC:
        modeFluctuation();
        break;
    }
  }  // RunLoop


  uint8_t flipDirCount = 0;
  int8_t difDirection = -1;
  float dirScale = 100.0;
  prefsData.scaleCalFac = 1000;  // reset scale factor
  scale.set_scale(prefsData.scaleCalFac);
  float currWeight = scale.get_units(10);
  float prevWeight = currWeight;


  if (scale.get_units(10) < calWeight) difDirection = 1;

  while (runLoopCalibrate) {
    currWeight = scale.get_units(10);
    if (abs(currWeight - calWeight) > 0.01) {  // define accuray

      if (abs(currWeight - calWeight) > abs(prevWeight - calWeight)) {  //when it gets more false, goin the other direction
        difDirection = -difDirection;
        flipDirCount++;
      }

      if (flipDirCount > 2) {
        if (dirScale > 0.001) {
          dirScale = dirScale / 10;
          flipDirCount = 0;
        }
      }

      prefsData.scaleCalFac += difDirection * dirScale;
      scale.set_scale(prefsData.scaleCalFac);
      prevWeight = currWeight;
    } else {  // when we are within accuracy
      preferences.begin("M5-espr-scale", false);
      preferences.putLong("scaleOffset", prefsData.scaleOffset);
      preferences.putFloat("scaleCalFac", prefsData.scaleCalFac);
      // Close the Preferences
      preferences.end();
      scale.set_offset(prefsData.scaleOffset);
      scale.set_scale(prefsData.scaleCalFac);
      runLoopCalibrate = false;
    }

    refreshCanvas();
    sprintf(stringBuffer, "%.2fg", currWeight);
    canvas.drawRightString(String(stringBuffer), displayWidth - 10, 10);
    sprintf(stringBuffer, "Offset: %i", prefsData.scaleOffset);
    canvas.drawCentreString(String(stringBuffer), displayWidth / 2, 45);
    sprintf(stringBuffer, "Factor: %.2f", prefsData.scaleCalFac);
    canvas.drawCentreString(String(stringBuffer), displayWidth / 2, 70);

    drawMenu("back", "", "");
    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        runLoopCalibrate = false;
        break;
    }
  }
}
void modeTimer() {
  refreshCanvas();
  boolean runLoop = true;
  int timer = 0;
  boolean runTimer = false;
  unsigned long int lastMillis = millis();
  while (runLoop) {
    canvas.setTextSize(3);
    if (runTimer) {
      if (millis() - lastMillis > 1000) {  // is a little inaccurate ... better work with millis instead if an int.
        timer--;
        lastMillis = millis();
        lastActionMillis = millis();  // for sleep prevention while counter is running
        if (timer < 0) playSound("beep");
      }
    }
    refreshCanvas();
    sprintf(stringBuffer, "Timer: %d min %d s", timer / 60, timer % 60);
    canvas.drawCentreString(String(stringBuffer), displayWidth / 2, displayHeight / 2);

    drawMenu("back", "+30s", "start/pause");
    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        runLoop = false;
        break;
      case BtnB:
        timer += 30;
        break;
      case BtnC:
        modeFluctuation();
        break;
    }
  }
}
void modeEspresso() {
  /*
  Espresso Mode:
  1) timer is started manually (klick start)
  2) timer is in pre-infusion mode ...
  3) ... until 1g weight is noticed on the scale, then:
  4) extraction mode with flow profile is starting ...
  5) until weight does not increase anymore for 1s (min 10g in the cup)
  6) waiting for review and dismiss by going back 
  */

  float preinfusionSeconds = 0.0;
  unsigned long int preinfusionMillis;
  float extrationSeconds = 0.0;
  unsigned long int extractionMillis;
  unsigned long int lastWeightCheckMillis = millis();
  float lastWeightCheckYieldGrams = 0.0;
  float currentWeight = 0.01;
  float yieldGrams = 0.0;
  boolean runTimer = false;
  boolean isPreinfusion = false;
  boolean isExtraction = false;
  boolean runLoop = true;

  // prepare graph canvas - we have one that only stores the graph, and one overlay that adds overlay per frame
  int fixedGraphCanvasHeight = (displayHeight / 2);
  int fixedGraphCanvasWidth = displayWidth;
  fixedGraph.createSprite(fixedGraphCanvasWidth, fixedGraphCanvasHeight);
  fixedGraphOverlay.createSprite(fixedGraphCanvasWidth, fixedGraphCanvasHeight);
  fixedGraph.fillSprite(BACKGROUND_COLOR);  //
  fixedGraphOverlay.setTextSize(2);
  fixedGraph.drawString(String(prefsData.espressoGraphMaxWeight) + 'g', 0, 0);
  fixedGraph.drawRightString(String(prefsData.maxExtractionTimeSec) + 's', fixedGraphCanvasWidth, fixedGraphCanvasHeight / 2);
  int yPos_weight = 0;
  int xPos_time = 0;

  while (runLoop) {
    refreshCanvas();
    sprintf(stringBuffer, "Pre: %.1fs", preinfusionSeconds);
    canvas.drawString(String(stringBuffer), 15, 15);
    sprintf(stringBuffer, "Ext: %.1fs", extrationSeconds);
    canvas.drawString(String(stringBuffer), 15, 40);
    sprintf(stringBuffer, "Tot: %.1fs", preinfusionSeconds + extrationSeconds);
    canvas.drawString(String(stringBuffer), 15, 65);

    canvas.setTextSize(4);
    sprintf(stringBuffer, "%.1fg", yieldGrams);
    canvas.drawRightString(String(stringBuffer), displayWidth, 20);

    if (runTimer) {
      currentWeight = getWeight(currentWeight);
      if (currentWeight < 0) currentWeight = 0;

      if (currentWeight > 1.0 && isExtraction == false) {  // trigger when 1g has been added after tare.
        isExtraction = true;
        isPreinfusion = false;
        extractionMillis = millis();
      }

      if (isPreinfusion) {
        preinfusionSeconds = (millis() - preinfusionMillis) * 1.0 / 1000;
      }

      if (isExtraction) {
        extrationSeconds = (millis() - extractionMillis) * 1.0 / 1000;
        if (yieldGrams < currentWeight) yieldGrams = currentWeight;
      }

      if (isExtraction && yieldGrams > 10 && millis() - lastWeightCheckMillis > 1000) {  // check if flow hast stopped every second
        lastWeightCheckMillis = millis();
        if (yieldGrams - lastWeightCheckYieldGrams < 0.3) {  // when we have not more that 0.3g added in the last second
          runTimer = false;
          isExtraction = false;
        }
        lastWeightCheckYieldGrams = yieldGrams;
      }
    }

    // draw the graph on fixedGraph canvas
    if (isPreinfusion) {
      fixedGraphOverlay.fillSprite(BACKGROUND_COLOR);  //
      fixedGraphOverlay.setTextSize(4);
      sprintf(stringBuffer, "%.1fs", preinfusionSeconds);
      fixedGraphOverlay.drawCentreString(String(stringBuffer), fixedGraphCanvasWidth / 2, fixedGraphCanvasHeight / 2);
      fixedGraphOverlay.setTextSize(2);
    }
    if (isExtraction) {
      xPos_time = fixedGraphCanvasWidth * ((millis() - extractionMillis) / (prefsData.maxExtractionTimeSec * 1000.0));
      yPos_weight = fixedGraphCanvasHeight - fixedGraphCanvasHeight * (yieldGrams / prefsData.espressoGraphMaxWeight);

      for (int i = 1; i < GRAPH_LINE_WIDTH; i++) {
        fixedGraph.drawCircle(xPos_time, yPos_weight, i, ACCENT_COLOR);
      }

      // create overlay
      fixedGraph.pushSprite(0, 0);  // copy the graph without overlay
      fixedGraphOverlay.drawCircle(xPos_time, yPos_weight, 4, HIGHLIGHT_COLOR);
      fixedGraphOverlay.drawCircle(xPos_time, yPos_weight, 3, HIGHLIGHT_COLOR);
      fixedGraphOverlay.drawCircle(xPos_time, yPos_weight, 2, HIGHLIGHT_COLOR);
      sprintf(stringBuffer, "%.1fg", yieldGrams);
      fixedGraphOverlay.drawString(String(stringBuffer), xPos_time, yPos_weight + 10);
      sprintf(stringBuffer, "%.1fs", preinfusionSeconds + extrationSeconds);
      fixedGraphOverlay.drawString(String(stringBuffer), xPos_time, yPos_weight + 30);
    }
    fixedGraphOverlay.pushSprite((displayWidth - fixedGraphCanvasWidth) / 2, displayHeight - MENU_HEIGHT - fixedGraphCanvasHeight);

    drawMenu("back", "start", "prefs");
    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        runLoop = false;
        break;
      case BtnB:
        scale.tare();
        preinfusionMillis = millis();
        isPreinfusion = true;
        runTimer = true;
        preinfusionSeconds = 0.0;
        extrationSeconds = 0.0;
        lastWeightCheckYieldGrams = 0.0;
        currentWeight = 0.01;
        yieldGrams = 0.0;
        isExtraction = false;
        break;
      case BtnC:
        modeEditPreferences();
        break;
    }
  }
}
void modeFluctuation() {
  scale.tare();
  boolean runLoop = true;
  uint samples = 1;
  uint measureCounter = 0;
  weight = scale.get_units(samples);
  float minVal = weight;
  float maxVal = weight;
  unsigned long sampleMillis = millis();
  while (runLoop) {
    lastActionMillis = millis() - 2000;  // deactivate idle sleep shutdown
    weight = scale.get_units(samples);
    minVal = min(minVal, weight);
    maxVal = max(maxVal, weight);
    measureCounter++;

    refreshCanvas();
    sprintf(stringBuffer, "Min: %.1fg", minVal);
    canvas.drawString(String(stringBuffer), 15, 15);
    sprintf(stringBuffer, "Max: %.1fg", maxVal);
    canvas.drawString(String(stringBuffer), 15, 45);
    sprintf(stringBuffer, "Measurements: %i", measureCounter);
    canvas.drawString(String(stringBuffer), 15, 75);
    sprintf(stringBuffer, "Speed: %ims", millis() - sampleMillis);
    canvas.drawString(String(stringBuffer), 15, 105);
    sampleMillis = millis();
    sprintf(stringBuffer, "Samples: %i", samples);
    canvas.drawString(String(stringBuffer), 15, 135);

    drawMenu("back", "+sample", "-sample");
    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        runLoop = false;
        break;
      case BtnB:
        samples++;
        if (samples > 10) samples = 10;
        measureCounter = 0;
        minVal = weight;
        maxVal = weight;
        delay(100);
        break;
      case BtnC:
        modeEditPreferences();
        break;
    }
  }
}
void loadPreferences() {
  // Remove all preferences under the opened namespace
  //preferences.clear();
  // Or remove the counter key only
  //preferences.remove("mykey");
  // Get the mykey value, if the key does not exist, return a default value of 0
  // Note: Key name is limited to 15 chars.
  prefsData.scaleCalFac = preferences.getFloat("scaleCalFac", 745.1);
  prefsData.scaleOffset = preferences.getLong("scaleOffset", 95226);
  prefsData.espressoGraphMaxWeight = preferences.getUChar("graMaxWeight", 50);
  prefsData.maxExtractionTimeSec = preferences.getUChar("graMaxExSec", 30);
}
void savePreferences() {
  preferences.putFloat("scaleCalFac", prefsData.scaleCalFac);
  preferences.putLong("scaleOffset", prefsData.scaleOffset);
  preferences.putUChar("graMaxWeight", prefsData.espressoGraphMaxWeight);
  preferences.putUChar("graMaxExSec", prefsData.maxExtractionTimeSec);
  preferences.end();  // Save and close the preferences file
}
void modeEditPreferences() {
  boolean runLoop = true;
  String prefDesc = "Description";
  String prefValue = "value";
  while (runLoop) {
    refreshCanvas();
    if (currentMenuState == MENU_SAVE_PREFERENCES) {
      drawMenu("Next", "Save", "Cancel");
    } else {
      drawMenu("Next", "Up", "Down");
    }

    switch (currentMenuState) {
      case MENU_SCALE_CAL_FAC:
        // Implement logic to edit scaleCalFac
        // You can use M5.Touch to interact with the user for input
        sprintf(stringBuffer, "scale factor");
        sprintf(stringBuffer2, "%.1f", prefsData.scaleCalFac);
        break;
      case MENU_SCALE_OFFSET:
        sprintf(stringBuffer, "scale offset");
        sprintf(stringBuffer2, "%i", prefsData.scaleOffset);
        break;
      case MENU_GRAPH_MAX_WEIGHT:
        sprintf(stringBuffer, "max weight");
        sprintf(stringBuffer2, "%ig", prefsData.espressoGraphMaxWeight);
        break;
      case MENU_MAX_EXTRACTION_TIME:
        sprintf(stringBuffer, "max extraction time");
        sprintf(stringBuffer2, "%is", prefsData.maxExtractionTimeSec);
        break;
      case MENU_SAVE_PREFERENCES:
        sprintf(stringBuffer, "Save Preferences?");
        sprintf(stringBuffer2, "Cancel = load defaults");
        break;
    }

    canvas.setTextSize(3);
    canvas.drawString(String(stringBuffer), 10, 10);
    canvas.drawRightString(String(stringBuffer2), displayWidth - 10, 50);

    switch (getPressedButton()) {
      case None:
        // No button pressed
        break;
      case BtnA:
        currentMenuState = static_cast<MenuState>((currentMenuState + 1) % (MENU_SAVE_PREFERENCES + 1));
        break;
      case BtnB:
        switch (currentMenuState) {
          case MENU_SCALE_CAL_FAC:
            prefsData.scaleCalFac += 1;
            break;
          case MENU_SCALE_OFFSET:
            prefsData.scaleOffset += 1;
            break;
          case MENU_GRAPH_MAX_WEIGHT:
            prefsData.espressoGraphMaxWeight += 1;
            break;
          case MENU_MAX_EXTRACTION_TIME:
            prefsData.maxExtractionTimeSec += 1;
            break;
          case MENU_SAVE_PREFERENCES:
            // Implement logic to save preferences
            savePreferences();
            runLoop = false;
            break;
        }
        break;
      case BtnC:
        switch (currentMenuState) {
          case MENU_SCALE_CAL_FAC:
            prefsData.scaleCalFac -= 1;
            break;
          case MENU_SCALE_OFFSET:
            prefsData.scaleOffset -= 1;
            break;
          case MENU_GRAPH_MAX_WEIGHT:
            prefsData.espressoGraphMaxWeight -= 1;
            break;
          case MENU_MAX_EXTRACTION_TIME:
            prefsData.maxExtractionTimeSec -= 1;
            break;
          case MENU_SAVE_PREFERENCES:
            // user cancels
            loadPreferences();
            runLoop = false;
            break;
        }
        break;
    }
  }
}
void flashDisplay() {
  canvas.fillSprite(ACCENT_COLOR);
  canvas.pushSprite(0, 0);
  canvas.fillSprite(BACKGROUND_COLOR);
  canvas.pushSprite(0, 0);
}
void refreshCanvas(){
  canvas.pushSprite(0, 0);
  canvas.fillSprite(BACKGROUND_COLOR);
}