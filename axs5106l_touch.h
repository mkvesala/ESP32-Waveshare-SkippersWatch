#pragma once                                                              // Compile this header only once per translation unit.

#include <Arduino.h>                                                      // Load Arduino types and the IRAM_ATTR macro.
#include <Wire.h>                                                         // Load the TwoWire I2C class used by the touch controller.

class Axs5106lTouch {                                                     // Define a small local driver for the AXS5106L touch chip.
 public:                                                                 // Expose the methods that the lesson sketch calls.
  bool begin(TwoWire& wire, uint8_t resetPin, uint8_t interruptPin,       // Start the driver with the I2C bus and hardware pins.
             uint16_t width, uint16_t height, uint8_t rotation);          // Store display geometry so raw points can be transformed.
  bool readPoint(uint16_t& x, uint16_t& y);                               // Read one touch point and return true when a valid point exists.

 private:                                                                // Keep low-level helper functions hidden from the sketch.
  static void IRAM_ATTR interruptHandler();                               // Mark that a touch interrupt happened without doing I2C in the ISR.
  bool readRegister(uint8_t registerAddress, uint8_t* data, size_t length); // Read bytes from one AXS5106L register.
  void transformPoint(uint16_t rawX, uint16_t rawY, uint16_t& x, uint16_t& y) const; // Rotate raw coordinates into screen coordinates.

  static volatile bool interruptPending_;                                 // Share the interrupt flag between the ISR and the main loop.
  TwoWire* wire_ = nullptr;                                               // Store the I2C bus pointer after begin() succeeds.
  uint16_t width_ = 0;                                                    // Store the display width used for coordinate bounds.
  uint16_t height_ = 0;                                                   // Store the display height used for coordinate bounds.
  uint8_t rotation_ = 0;                                                  // Store the display rotation value from 0 to 3.
};                                                                        // End the Axs5106lTouch class.
