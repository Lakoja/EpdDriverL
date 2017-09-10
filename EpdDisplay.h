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

#include <Adafruit_GFX.h>

#include "SpiLine.h"

template <typename T> static inline void
swap(T& a, T& b)
{
  T t = a;
  a = b;
  b = t;
}

// mirror a pixel around a center line
static inline uint16_t mirror(uint16_t value, uint16_t maxi)
{
  return maxi - value - 1;
}

#define D1 5
#define D2 4
#define D4 2

#define EPD_2x9_DISPLAY_WIDTH 128
#define EPD_2x9_DISPLAY_HEIGHT 296

#define EPD_1x54_DISPLAY_WIDTH 200
#define EPD_1x54_DISPLAY_HEIGHT 200

#define EPD_BLACK 0x0000
#define EPD_WHITE 0xFFFF

#define CMD_DISPLAY_ACTIVATION 0x20
#define CMD_DISPLAY_UPDATE 0x22
#define CMD_PIXEL_DATA 0x24
#define CMD_WRITE_LUT 0x32
#define CMD_SET_RAM_X 0x44
#define CMD_SET_RAM_Y 0x45
#define CMD_SET_RAM_X_COUNTER 0x4e
#define CMD_SET_RAM_Y_COUNTER 0x4f
#define CMD_NOP_TERMINATE_WRITE 0xff

#define DATA_CLK_CP_OFF 0x03
#define DATA_CLK_CP_ON 0xc0
#define DATA_CLK_CP_ON_OFF 0xc3

const uint8_t LUTDefault_full[] =
{
  CMD_WRITE_LUT,
  0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22, 0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99,
  0x88, 0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51, 0x35, 0x51, 0x51, 0x19, 0x01, 0x00
};

const uint8_t LUTDefault_part[] =
{
  CMD_WRITE_LUT,
  0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x44, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

uint8_t GDOControl[] = {0x01, 295 % 256, 295 / 256, 0x00};
uint8_t softstart[] = {0x0c, 0xd7, 0xd6, 0x9d};
uint8_t VCOMVol[] = {0x2c, 0xa8};
uint8_t DummyLine[] = {0x3a, 0x1a};
uint8_t Gatetime[] = {0x3b, 0x08}; // 2us per line
uint8_t RamDataEntryMode[] = {0x11, 0x01};

class EpdDisplayState {
public:
  uint32_t partialUpdateCount = 0;
  bool isInitialized = false;
  bool isFullMode = true;
  bool isPowerOff = true;
};

class EpdDisplay : public Adafruit_GFX 
{
private:
  EpdDisplayState state;
  SpiLine spiOutput;
  uint8_t busyPin;
  uint8_t *pixelBuffer;
  bool isSyncOperation = true;

  uint8_t partialUpdateThreshold = 10;
  
public:
  EpdDisplay(int16_t width, int16_t height, bool operateAsync = false,
      uint8_t cs = SS, uint8_t dc = D1, uint8_t rst = D4, uint8_t busy = D2) : 
    Adafruit_GFX(width, height), spiOutput(SpiLine(SPI, cs, dc, rst))
  {
    busyPin = busy;
    isSyncOperation = !operateAsync;
    
    pixelBuffer = (uint8_t*)malloc(width * height / 8);
    
    GDOControl[1] = height % 256;
    GDOControl[2] = height / 256;
  }

  virtual void init(EpdDisplayState *storedState)
  {
    spiOutput.init(4000000);

    pinMode(busyPin, INPUT);
      
    initializeRegisters();
	
    if (storedState != NULL) {
      state = *storedState;
      state.isInitialized = false;
    }
  }
  
  EpdDisplayState* getState()
  {
    return &state;
  }

  void initFullMode()
  {
    if (state.isFullMode && state.isInitialized)
      return; // TODO PowerOn?

    state.isInitialized = true;
    state.isFullMode = true;

    // Having it here has some slight advantages (in update cleanliness)
    setAddresses(0x00, WIDTH - 1, HEIGHT - 1, 0x00); 

    // NOTE also works with LUTDefault_part here (only no full update then)
    writeCommandData(LUTDefault_full, sizeof(LUTDefault_full));
    sendPowerOnCommands();
  }

  void initPartialMode()
  {
    if (!state.isFullMode && state.isInitialized)
      return;

    state.isInitialized = true;
    state.isFullMode = false;

    writeCommandData(LUTDefault_part, sizeof(LUTDefault_part));
    sendPowerOnCommands();
  }
  
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color)
  {
    if ((x < 0) || (x >= width()) || (y < 0) || (y >= height())) 
      return;

    // TODO maybe a direction can be specified? in ram area
    switch (getRotation()) {
      case 1:
        swap(x, y);
        break;
      case 2:
        // bottom-up portrait: this is the most natural for the display; esp. x-order is correct
        y = mirror(y, HEIGHT);
        break;
      default:
        Serial.println("!!! Rotation value not supported.");
    }
    
    // TODO / NOTE usage of WIDTH and HEIGHT here: they have the wrong meaning but it (only) works with them

    uint16_t lineWidth = WIDTH / 8;
    uint16_t idx = x / 8 + y * lineWidth;
    byte value = color == EPD_BLACK ? 0 : 1;
    byte bitInByte = x % 8;

    byte currentData = pixelBuffer[idx];
    if (value != 0)
      currentData = currentData | (1 << (7 - bitInByte));
    else
      currentData = currentData & (0xFF ^ (1 << (7 - bitInByte)));

    pixelBuffer[idx] = currentData;
  }

  virtual void fillScreen(uint16_t color)
  {
    memset(pixelBuffer, (uint8_t)color, width() * height() / 8);
  }

  void updatePartOrFull()
  {
    if (state.partialUpdateCount >= partialUpdateThreshold) {
      state.partialUpdateCount = 0;

      // TODO necessary?
      update();
      
      initFullMode();
      waitWhileBusy();
      
      update();
	  
      initPartialMode();
    } else {
      state.partialUpdateCount++;
      update();
    }
  }

  virtual void update()
  {
    if (!isSyncOperation && isBusy())
      return;

    // NOTE / TODO mixing reset/init and partial update (set ram pointers) toegether is very problematic

    showBuffer((uint8_t *)pixelBuffer, false);
	
    sendPowerOffCommands();
    waitWhileBusy();
  }

  bool isBusy()
  {
    return digitalRead(busyPin) == HIGH;
  }

private:

  void showBuffer(uint8_t *data, bool mono)
  {
    showBuffer(0, WIDTH - 1, 0, HEIGHT - 1, data, mono);
  }
  
  void showBuffer(uint8_t xStart, uint8_t xEnd,
      uint16_t yStart, uint16_t yEnd, uint8_t *data, bool mono)
  {
    if (state.isPowerOff) {
      sendPowerOnCommands();
    }

    if (!state.isFullMode)
      setAddresses(xStart, xEnd, yEnd, yStart);
    // else was set in init...
  
    writeDisplayData(xEnd-xStart+1, yEnd-yStart+1, data, mono);

    if (state.isFullMode)
      sendUpdateFullCommands();
    else
      sendUpdatePartCommands();

    if (isSyncOperation) {
      waitWhileBusy();
    }
    // TODO else
  
    if (!state.isFullMode) {
      // Disabling those enables a two screen flashing...
      //setAddresses(xStart, xEnd, yEnd, yStart);
      writeDisplayData(xEnd-xStart+1, yEnd-yStart+1, data, mono);
    }
  }

  void writeDisplayData(uint8_t XSize, uint16_t YSize, uint8_t *data, bool mono)
  {
    XSize = (XSize + 7)/8; // ceil
    
    waitWhileBusy(); // TODO important: Otherwise no clear before partial update
    
    writeCommand(CMD_PIXEL_DATA);
    
    for (uint16_t i=0; i<XSize; i++){
      for (uint16_t j=0; j<YSize; j++){
        writeData(*data);
  
        if (!mono)
          data++;
      }
    }
  }

  void setAddresses(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend)
  {
    setRamArea(Xstart / 8, Xend / 8, Ystart, Yend);
    setRamPointer(Xstart / 8, Ystart);
  }
  
  void setRamArea(uint8_t Xstart, uint8_t Xend, uint16_t Ystart, uint16_t Yend)
  {
    writeCommand(CMD_SET_RAM_X);
    writeData(Xstart);
    writeData(Xend);
    writeCommand(CMD_SET_RAM_Y);
    writeData(Ystart % 256);
    writeData(Ystart / 256);
    writeData(Yend % 256);
    writeData(Yend / 256);
  }
  
  void setRamPointer(uint8_t addrX, uint16_t addrY)
  {
    writeCommand(CMD_SET_RAM_X_COUNTER);
    writeData(addrX);
    writeCommand(CMD_SET_RAM_Y_COUNTER);
    writeData(addrY % 256);
    writeData(addrY / 256);
  }
  
  void sendPowerOnCommands()
  {
    if (state.isPowerOff == false)
      Serial.println("!Wrong power on");
    
    writeCommand(CMD_DISPLAY_UPDATE);
    writeData(DATA_CLK_CP_ON);
    writeCommand(CMD_DISPLAY_ACTIVATION);

    state.isPowerOff = false;
  }
  
  void sendPowerOffCommands()
  {
    if (state.isPowerOff == true)
      Serial.println("!Wrong power off");
    
    writeCommand(CMD_DISPLAY_UPDATE);
    writeData(DATA_CLK_CP_OFF);
    writeCommand(CMD_DISPLAY_ACTIVATION);

    state.isPowerOff = true;
  }
  
  void initializeRegisters()
  {
    writeCommandData(GDOControl, sizeof(GDOControl)); // Pannel configuration, Gate selection
    writeCommandData(softstart, sizeof(softstart)); // X decrease, Y decrease
    writeCommandData(VCOMVol, sizeof(VCOMVol));
    writeCommandData(DummyLine, sizeof(DummyLine));
    writeCommandData(Gatetime, sizeof(Gatetime));
    writeCommandData(RamDataEntryMode, sizeof(RamDataEntryMode)); // X increase, Y decrease
  }
  
  void sendUpdateFullCommands()
  {
    writeCommand(CMD_DISPLAY_UPDATE);
    writeData(0xc4); // TODO use c7?
    writeCommand(CMD_DISPLAY_ACTIVATION);
    writeCommand(CMD_NOP_TERMINATE_WRITE);
  }
  
  void sendUpdatePartCommands()
  {
    writeCommand(CMD_DISPLAY_UPDATE);
    writeData(0x04);
    writeCommand(CMD_DISPLAY_ACTIVATION);
    writeCommand(CMD_NOP_TERMINATE_WRITE);
  }
  
  void writeCommand(uint8_t command)
  {
    spiOutput.writeCommandTransaction(command);
  }
  
  void writeData(uint8_t data)
  {
    spiOutput.writeDataTransaction(data);
  }
  
  void writeCommandData(const uint8_t* pCommandData, uint8_t datalen)
  {
    spiOutput.startTransaction();
    spiOutput.writeCommand(*pCommandData++);
    for (uint8_t i = 0; i < datalen - 1; i++)
    {
      spiOutput.writeData(*pCommandData++);
    }
    spiOutput.endTransaction();
  }
  
  void waitWhileBusy()
  {
    for (uint16_t i=0; i<400; i++)
    {
      if (digitalRead(busyPin) == LOW) 
        break;
      delay(5);
    }
  }
};

