#include "axs5106l_touch.h"                                      // Load the class declaration for the local touch driver.

namespace {                                                       // Keep register constants private to this source file.
constexpr uint8_t kDeviceAddress = 0x63;                          // Use the AXS5106L I2C address from the validated reference project.
constexpr uint8_t kIdRegister = 0x08;                             // Select the register that returns the touch-controller ID bytes.
constexpr uint8_t kTouchDataRegister = 0x01;                      // Select the register that returns the current touch packet.
constexpr size_t kTouchPacketSize = 14;                           // Read the full 14-byte touch packet used by this controller.
}                                                                 // End the private namespace.

volatile bool Axs5106lTouch::interruptPending_ = false;           // Initialize the shared touch-interrupt flag to idle.

void IRAM_ATTR Axs5106lTouch::interruptHandler() {                // Run this function inside the GPIO interrupt context.
  interruptPending_ = true;                                       // Record that a touch packet is ready for the main loop to read.
}                                                                 // End the interrupt handler.

bool Axs5106lTouch::begin(TwoWire& wire, uint8_t resetPin, uint8_t interruptPin,
                         uint16_t width, uint16_t height, uint8_t rotation) { // Initialize the touch controller.
  wire_ = &wire;                                                  // Store the I2C bus object that the sketch already configured.
  width_ = width;                                                 // Store the display width for later coordinate validation.
  height_ = height;                                               // Store the display height for later coordinate validation.
  rotation_ = rotation & 0x03;                                    // Keep only the two valid rotation bits.

  pinMode(resetPin, OUTPUT);                                      // Configure the reset pin as a digital output.
  pinMode(interruptPin, INPUT);                                   // Configure the interrupt pin as a digital input.
  digitalWrite(resetPin, LOW);                                    // Pull reset low to restart the touch controller.
  delay(200);                                                     // Hold reset low long enough for the chip to enter reset.
  digitalWrite(resetPin, HIGH);                                   // Release reset so the touch controller can boot.
  delay(300);                                                     // Wait for the touch firmware to become ready on I2C.

  attachInterrupt(digitalPinToInterrupt(interruptPin), interruptHandler, FALLING); // Trigger the ISR on the active-low touch interrupt.

  uint8_t id[3] = {};                                             // Allocate a three-byte buffer for the controller ID.
  if (!readRegister(kIdRegister, id, sizeof(id))) {               // Try to read the ID register before accepting the device.
    return false;                                                 // Report failure if the controller does not answer.
  }                                                               // End the ID-read failure check.

  Serial.printf("AXS5106L ID bytes: %02X %02X %02X\n", id[0], id[1], id[2]); // Print the ID bytes for hardware confirmation.
  return true;                                                    // Report that the touch controller is ready.
}                                                                 // End begin().

bool Axs5106lTouch::readRegister(uint8_t registerAddress, uint8_t* data, size_t length) { // Read one register block.
  if (wire_ == nullptr || data == nullptr || length == 0) {       // Reject the read if the bus, buffer, or length is invalid.
    return false;                                                 // Report an invalid read request.
  }                                                               // End the parameter check.

  wire_->beginTransmission(kDeviceAddress);                       // Start an I2C write transaction to select a register.
  wire_->write(registerAddress);                                  // Send the register address that should be read next.
  if (wire_->endTransmission() != 0) {                             // Finish the write transaction and check for an ACK.
    return false;                                                 // Report failure if the device did not acknowledge.
  }                                                               // End the register-select check.

  const size_t received = wire_->requestFrom(kDeviceAddress, length); // Request the requested number of bytes from the controller.
  if (received != length) {                                       // Check whether the controller returned the full packet.
    return false;                                                 // Report failure when the I2C read was short.
  }                                                               // End the length check.

  return wire_->readBytes(data, length) == length;                // Copy the bytes into the buffer and report whether all bytes were read.
}                                                                 // End readRegister().

void Axs5106lTouch::transformPoint(uint16_t rawX, uint16_t rawY,
                                  uint16_t& x, uint16_t& y) const { // Convert raw touch coordinates to display coordinates.
  switch (rotation_) {                                            // Select the coordinate transform that matches the display rotation.
    case 1:                                                       // Handle 90-degree rotation.
      x = rawY;                                                   // Map raw Y to screen X.
      y = height_ - 1U - rawX;                                    // Invert raw X and map it to screen Y.
      break;                                                      // Stop processing this switch case.
    case 2:                                                       // Handle 180-degree rotation.
      x = width_ - 1U - rawX;                                     // Invert raw X and map it to screen X.
      y = height_ - 1U - rawY;                                    // Invert raw Y and map it to screen Y.
      break;                                                      // Stop processing this switch case.
    case 3:                                                       // Handle 270-degree rotation.
      x = width_ - 1U - rawY;                                     // Invert raw Y and map it to screen X.
      y = rawX;                                                   // Map raw X to screen Y.
      break;                                                      // Stop processing this switch case.
    default:                                                      // Handle the normal 0-degree orientation.
      x = rawX;                                                   // Use raw X directly.
      y = rawY;                                                   // Use raw Y directly.
      break;                                                      // Stop processing this switch case.
  }                                                               // End the rotation switch.
}                                                                 // End transformPoint().

bool Axs5106lTouch::readPoint(uint16_t& x, uint16_t& y) {         // Try to read one valid touch point.
  if (!interruptPending_) {                                       // Skip I2C traffic when the controller has not signaled a touch update.
    return false;                                                 // Report that no new point is available.
  }                                                               // End the interrupt flag check.
  interruptPending_ = false;                                      // Clear the flag before reading the new packet.

  uint8_t packet[kTouchPacketSize] = {};                          // Allocate and clear the touch-packet buffer.
  if (!readRegister(kTouchDataRegister, packet, sizeof(packet))) { // Read the current touch packet from the controller.
    return false;                                                 // Report failure if the packet cannot be read.
  }                                                               // End the packet-read check.

  const uint8_t touchCount = packet[1];                           // Read the number of touches reported in the packet.
  if (touchCount == 0) {                                          // Check whether the finger has been released.
    return false;                                                 // Report no active touch point.
  }                                                               // End the touch-count check.

  const uint16_t rawX = (static_cast<uint16_t>(packet[2] & 0x0F) << 8) | packet[3]; // Decode the 12-bit raw X coordinate.
  const uint16_t rawY = (static_cast<uint16_t>(packet[4] & 0x0F) << 8) | packet[5]; // Decode the 12-bit raw Y coordinate.
  transformPoint(rawX, rawY, x, y);                               // Rotate the raw point into display coordinates.

  return x < width_ && y < height_;                               // Accept the point only when it falls inside the display area.
}                                                                 // End readPoint().
