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

#ifndef _SpiLine_H_
#define _SpiLine_H_

#define SPI_DEFAULT_FREQUENCY 16000000

#include <SPI.h>

class SpiLine
{
private:
  SPIClass& spiSink;
  uint8_t selectPin, dataCommandPin, resetPin;
  bool transactionStarted = false;
  
public:
  SpiLine(SPIClass& spi, uint8_t cs, uint8_t dc, uint8_t rst) : spiSink(spi)
  {
    selectPin = cs;
    dataCommandPin = dc;
    resetPin = rst;
  }
  
  void reset()
  {
    digitalWrite(resetPin, LOW);
    delay(40);
    digitalWrite(resetPin, HIGH);
    delay(40);
  }
  
  void init(uint32_t freq = SPI_DEFAULT_FREQUENCY)
  {
    digitalWrite(selectPin, HIGH);
    pinMode(selectPin, OUTPUT);
    digitalWrite(dataCommandPin, HIGH);
    pinMode(dataCommandPin, OUTPUT);
    digitalWrite(resetPin, HIGH);
    pinMode(resetPin, OUTPUT);
      
    reset();
    
    spiSink.begin();
    spiSink.setDataMode(SPI_MODE0);
    spiSink.setBitOrder(MSBFIRST);
    spiSink.setFrequency(freq);
  }
  
  void writeCommandTransaction(uint8_t c)
  {
    startTransaction();
    writeCommand(c);
    endTransaction();
  }
  
  void writeDataTransaction(uint8_t d)
  {
    startTransaction();
    writeData(d);
    endTransaction();
  }
  
  void writeCommand(uint8_t c)
  {
    if (!transactionStarted) {
      Serial.print("NO TRANSACTION ");
      Serial.println(c);
    }
    
    digitalWrite(dataCommandPin, LOW);
    spiSink.transfer(c);
    digitalWrite(dataCommandPin, HIGH);
  }
  
  void writeData(uint8_t d)
  {
    if (!transactionStarted)
      Serial.println("NO TRANSACTION");
    
    spiSink.transfer(d);
  }
  
  void startTransaction()
  {
    digitalWrite(selectPin, LOW);
    transactionStarted = true;
  }
  
  void endTransaction()
  {
    digitalWrite(selectPin, HIGH);
    transactionStarted = false;
  }
};

#endif

