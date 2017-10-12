/*
 * Copyright (C) 2017 Lakoja on github.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include "EpdDisplay.h"

/**
 * This program will use the following pin mapping on the esp8266. All these
 * pins must be provided.
 * DIN = GPIO13 (HMOSI)
 * CLK = GPIO14 (HSCLK)
 * CS  = GPIO15 (HCS)
 * DC  = GPIO05 = D1
 * RST = GPIO02 = D4
 * BUS = GPIO04 = D2 (also HMISO (GPIO12) could be used)
 */
 
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Change to EPD_2x9_DISPLAY_WIDTH, EPD_2x9_DISPLAY_HEIGHT for 2.9 display
EpdDisplay display(EPD_1x54_DISPLAY_WIDTH, EPD_1x54_DISPLAY_HEIGHT);

unsigned long systemStart;

void setup() {

  systemStart = millis();
  
  Serial.begin(115200);
  Serial.println(" Setup"); // Most importantly: move to a new line after boot message
  
  display.init(NULL); // could store/restore a display state; but currently simply start fresh
  display.setRotation(2);

  display.initFullMode();
  display.fillScreen(0x6b); // a line pattern
  display.update();
}

int counter = 0;
void loop() {
  display.fillScreen(EPD_WHITE);
  display.drawLine(++counter * 3,0,50 + counter * 3,20, EPD_BLACK);

  if (counter > 2) {
    display.setTextColor(EPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(23, 122);
    display.print("Hallo");
  }
  
  unsigned long m1 = millis();
  display.update();
  unsigned long m2 = millis();
  Serial.print("Update ");
  Serial.println(m2-m1);
  
  if (counter == 10)
    display.initPartialMode(); // quicker but unclean
  delay(2000);
}
