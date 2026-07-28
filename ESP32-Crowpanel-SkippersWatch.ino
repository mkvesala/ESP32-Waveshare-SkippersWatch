 // === M A I N  P R O G R A M ===
 //
 // - Owns the CrowPanelApplication instance
 // - Initates Serial
 // - Initiates the app
 // - Executes app.loop() within the main loop()

#include <Arduino.h>
#include "CrowPanelApplication.h"

CrowPanelApplication app;

// === S E T U P ===

void setup() {

    Serial.begin(115200);
    delay(100);
    app.begin();

}

// === L O O P ===

void loop() {

    app.loop();
    
}
