 // === M A I N  P R O G R A M ===
 //
 // - Owns the WatchApplication instance
 // - Initates Serial (native USB CDC on this board: "USB CDC On Boot" = Enabled)
 // - Initiates the app
 // - Executes app.loop() within the main loop()

#include <Arduino.h>
#include "WatchApplication.h"

WatchApplication app;

// === S E T U P ===

void setup() {

    Serial.begin(115200);
    // On battery there is no USB host: never let a full CDC TX buffer block the wake path
    Serial.setTxTimeoutMs(0);
    delay(100);
    app.begin();

}

// === L O O P ===

void loop() {

    app.loop();
    
}
