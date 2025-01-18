#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WS2812FX.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncE131.h>


const char* ssid = "9titoch";
const char* password = "        M";
ESP8266WebServer server(80);

// E1.31 configuration
ESPAsyncE131 e131; // E1.31 object

bool MusicMode = false ; 

// NTP settings
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", 3600, 60000); // NTP server, offset (UTC), update interval

unsigned long lastCheckTime = 0; // To manage non-blocking timer checks
const unsigned long checkInterval = 1000; // Interval to check timers (in milliseconds)

struct Timer {
  int onHour;
  int onMinute;
  int offHour;
  int offMinute;
  bool lastActionOn; // Tracks whether the last action was "on" (true) or "off" (false)
};

Timer timers[3];
unsigned int speed = 1000; // OK
int LED_COUNT = 8 ;
int LED_PIN  = D2 ;
int animationNUM = 0;
int brightness = 255; // Default brightness
uint32_t currentColor = 0xFFFFFF;
bool powerState = false; // LED strip power state



WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(9600);

  // Initialize WiFi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());


  // Initialize E1.31
  if (!e131.begin(E131_MULTICAST, 1, 1)) { // Listening to Universe 1
    Serial.println("E1.31 setup failed");
    while (true);
  }
  Serial.println("E1.31 listening");
  
  
  // Initialize mDNS
  if (MDNS.begin("esp8266")) {  // Set the hostname
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up mDNS responder");
  }
  

  
  // Initialize EEPROM and load saved state
  EEPROM.begin(512);
  loadState(); // Load state from EEPROM

  // Debugging: Confirm the loaded values
  Serial.println("Loaded EEPROM values:");
  Serial.println("  LED_PIN: " + String(LED_PIN));
  Serial.println("  LED_COUNT: " + String(LED_COUNT));
  Serial.println("  Animation Number: " + String(animationNUM));
  Serial.println("  Brightness: " + String(brightness));
  Serial.println("  Color: #" + String(currentColor, HEX));
  Serial.println("  Power State: " + String(powerState ? "ON" : "OFF"));
  for (int i = 0; i < 3; i++) {
    Serial.print("Timer ");
    Serial.print(i + 1);
    Serial.print(" -> onHour: ");
    Serial.print(timers[i].onHour);
    Serial.print(", onMinute: ");
    Serial.print(timers[i].onMinute);
    Serial.print(", offHour: ");
    Serial.print(timers[i].offHour);
    Serial.print(", offMinute: ");
    Serial.println(timers[i].offMinute);
  }


  // Force WS2812FX to use loaded values
  ws2812fx.setPin(LED_PIN);
  ws2812fx.setLength(LED_COUNT);
  ws2812fx.init(); // Initialize WS2812FX
  ws2812fx.setSpeed(speed);       // Set animation speed in milliseconds
  ws2812fx.setBrightness(brightness);
  ws2812fx.setMode(animationNUM);
  ws2812fx.setColor(currentColor);

  // Start or stop based on power state
  if (powerState) {
    ws2812fx.start();
  } else {
    ws2812fx.stop();
  }

  // Start HTTP server
//  server.on("/setGradient", handleSetGradient);
server.on("/fetchSpeed", handleFetchSpeed);
   server.on("/setTimers", HTTP_POST, handleSetTimers);
  server.on("/updateSpeed", handleUpdateSpeed);
  // Handle OPTIONS request for CORS preflight
   server.on("/get-music-mode", handleGetMusicMode);
  server.on("/setMusicMode", HTTP_GET, handleMusicModeControl);
  server.on("/setTimers", HTTP_OPTIONS, handleOptions);
  server.on("/setAnimation", handleSetAnimation);
  server.on("/setColor", handleSetColor);
  server.on("/setBrightness", handleSetBrightness);
  server.on("/togglePower", handleTogglePower);
  server.on("/getState", handleGetState);
  server.on("/setPin", handleSetPin);
  server.on("/setLedCount", handleSetLedCount);

  server.begin();
  Serial.println("HTTP server started");

  // Initialize NTP client
  timeClient.begin();
  timeClient.update();  // Fetch the current time from NTP server
  
  // Set the time to current NTP time
  unsigned long epochTime = timeClient.getEpochTime();  // Get the current time in seconds
  setTime(epochTime);  // Set the time on the ESP8266

  Serial.println("Time synchronized with NTP");
  
  // Print current time (Optional, just to verify)
  Serial.println(timeClient.getFormattedTime());
  setTime(epochTime);
}


void loop() {
  // Ensure the server handles client requests efficiently
  server.handleClient();
  
  MDNS.update();  // Keep mDNS responder active
  
  if (MusicMode == true){
    LedFX();
  }
  else {
  // Call the WS2812FX service to update the LED animations
  ws2812fx.service();
  // Update mDNS service
  checkTimers();
  }
}

// Function to handle fetching the current speed
void handleFetchSpeed() {
  // Set CORS header to allow all origins (or specify a particular domain if needed)
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String response = String(speed); // Send speed as plain text
  server.send(200, "text/plain", response);
}


void LedFX() {
  // Handle incoming E1.31 packets
  if (!e131.isEmpty()) {
    e131_packet_t packet;
    e131.pull(&packet); // Get the latest packet

    // Map DMX data to the LED array
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      uint16_t index = i * 3; // DMX data is in RGB triplets
      if (index + 2 < sizeof(packet.property_values)) { // Ensure valid index
        // Map the E1.31 data to the LED strip
        uint8_t r = packet.property_values[index + 1]; // Red
        uint8_t g = packet.property_values[index + 2]; // Green
        uint8_t b = packet.property_values[index + 3]; // Blue

        // Set the color for each LED in the strip
        ws2812fx.setPixelColor(i, r, g, b);
      }
    }

    // Show updated LED colors
    ws2812fx.show();
  }
  // Run WS2812FX effects
  ws2812fx.service();
}


// Function to handle sending MusicMode state
void handleGetMusicMode() {
  // Set CORS header to allow all origins (or specify a particular domain if needed)
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String response = MusicMode ? "true" : "false"; // Convert the state to a string
  server.send(200, "text/plain", response);      // Send the response to the web app
}


void handleMusicModeControl() {
    // Check if the client sent a "mode" parameter
    if (server.hasArg("mode")) {
        String mode = server.arg("mode");
        MusicMode = (mode == "1"); // Set MusicMode to true if mode is "1", otherwise false
        Serial.printf("MusicMode set to: %s\n", MusicMode ? "true" : "false");
        server.send(200, "text/plain", "MusicMode updated");
    } else {
        server.send(400, "text/plain", "Missing 'mode' parameter");
    }
}


// Function to handle speed updates
void handleUpdateSpeed() {
  Serial.println("[DEBUG] Received request to /updateSpeed");
  
  if (server.hasArg("value")) {
    String speedValue = server.arg("value"); // Get the value argument
    Serial.println("[DEBUG] Received value: " + speedValue); // Debug: print the received value
    
    speed = speedValue.toInt(); // Convert the value to an integer and update the speed
    Serial.println("[DEBUG] Updated speed variable: " + String(speed) + " ms"); // Debug: print the updated speed

    server.send(200, "text/plain", "Speed updated to: " + String(speed) + " ms");
  } else {
    Serial.println("[ERROR] Missing value parameter in request!"); // Debug: error message
    server.send(400, "text/plain", "Error: Missing value parameter");
  }
  saveState();
  ws2812fx.setSpeed(speed);       // Set animation speed in milliseconds
}



void handleSetTimers() {
  // Create a JSON object to parse the incoming request
  DynamicJsonDocument doc(1024);

  // Parse the JSON data from the request
  DeserializationError error = deserializeJson(doc, server.arg("plain"));

  if (error) {
    // Send error response if JSON parsing failed
    Serial.println("Error parsing JSON data");
    Serial.println(error.f_str());
    server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
    return;
  }

  // Debugging: Print received JSON
  Serial.println("Received Timer Data:");
  serializeJsonPretty(doc, Serial);  // Print the received JSON data

  for (int i = 0; i < 3; i++) {
    // Retrieve timer data and convert to integers
    timers[i].onHour = doc["onHour" + String(i + 1)].as<int>();
    timers[i].onMinute = doc["onMinute" + String(i + 1)].as<int>();
    timers[i].offHour = doc["offHour" + String(i + 1)].as<int>();
    timers[i].offMinute = doc["offMinute" + String(i + 1)].as<int>();

    // Debugging: Print the timer values
    Serial.print("Timer ");
    Serial.print(i + 1);
    Serial.print(": ON at ");
    Serial.print(timers[i].onHour);
    Serial.print(":");
    Serial.println(timers[i].onMinute);
    Serial.print(" OFF at ");
    Serial.print(timers[i].offHour);
    Serial.print(":");
    Serial.println(timers[i].offMinute);
  }

  saveState();
  // Send success response
  server.send(200, "application/json", "{\"status\":\"success\"}");
}


void handleOptions() {
  // Handle the OPTIONS preflight request for CORS
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  server.send(200);
}

void saveState() {
  Serial.println("Saving state to EEPROM...");
  
  // Write values to EEPROM with debug messages
  EEPROM.put(0, LED_PIN);
  Serial.println("Saved LED_PIN: " + String(LED_PIN));
  
  EEPROM.put(4, LED_COUNT);
  Serial.println("Saved LED_COUNT: " + String(LED_COUNT));
  
  EEPROM.put(8, animationNUM);
  Serial.println("Saved animationNUM: " + String(animationNUM));
  
//   Uncomment these if you want to save color, brightness, and power state
   EEPROM.put(12, currentColor);
   Serial.println("Saved currentColor: " + String(currentColor, HEX));
   
   EEPROM.put(40, timers);
    // Debugging: Print the timer values being saved
//  for (int i = 0; i < 3; i++) {
////    Serial.print("Timer ");
////    Serial.print(i + 1);
////    Serial.print(" -> onHour: ");
////    Serial.print(timers[i].onHour);
////    Serial.print(", onMinute: ");
////    Serial.print(timers[i].onMinute);
////    Serial.print(", offHour: ");
////    Serial.print(timers[i].offHour);
////    Serial.print(", offMinute: ");
////    Serial.println(timers[i].offMinute);
//  }
     EEPROM.put(44, speed);  // Start reading from address 2
  // EEPROM.put(16, brightness);
  // Serial.println("Saved brightness: " + String(brightness));
  
  // EEPROM.put(20, powerState);
  // Serial.println("Saved powerState: " + String(powerState ? "true" : "false"));
  
  EEPROM.commit(); // Commit the changes to EEPROM
  Serial.println("State saved to EEPROM successfully.");
}

void loadState() {
  Serial.println("Loading state from EEPROM...");
  
  // Read values from EEPROM with debug messages
  EEPROM.get(0, LED_PIN);
  Serial.println("Loaded LED_PIN: " + String(LED_PIN));
  
  EEPROM.get(4, LED_COUNT);
  Serial.println("Loaded LED_COUNT: " + String(LED_COUNT));
  
  EEPROM.get(8, animationNUM);
  Serial.println("Loaded animationNUM: " + String(animationNUM));
  
  // Uncomment these if you want to load color, brightness, and power state
   EEPROM.get(12, currentColor);
  // Serial.println("Loaded currentColor: " + String(currentColor, HEX));
  
  EEPROM.get(40, timers);
  
  // Debugging: Print the loaded timer values
   // Debugging: Print raw timer data after reading from EEPROM
//  for (int i = 0; i < 3; i++) {
//    Serial.print("Raw Timer ");
//    Serial.print(i + 1);
//    Serial.print(": onHour = ");
//    Serial.print(timers[i].onHour);
//    Serial.print(", onMinute = ");
//    Serial.print(timers[i].onMinute);
//    Serial.print(", offHour = ");
//    Serial.print(timers[i].offHour);
//    Serial.print(", offMinute = ");
//    Serial.println(timers[i].offMinute);
//  }
    EEPROM.get(44, speed);  // Start reading from address 2
  // EEPROM.get(16, brightness);
  // Serial.println("Loaded brightness: " + String(brightness));
  
  // EEPROM.get(20, powerState);
  // Serial.println("Loaded powerState: " + String(powerState ? "true" : "false"));
  
  Serial.println("State loaded from EEPROM successfully.");
}


void checkTimers() {
  unsigned long currentMillis = millis(); // Get current time

  // Only check the timers at intervals defined by checkInterval
  if (currentMillis - lastCheckTime >= checkInterval) {
    lastCheckTime = currentMillis; // Update the last check time

    int currentHour = hour();    // Get the current hour
    int currentMinute = minute(); // Get the current minute

    // Loop through all timers to see if any should trigger an action
    for (int i = 0; i < sizeof(timers) / sizeof(timers[0]); i++) {
      Timer &t = timers[i]; // Reference the current timer to modify its state

      // Ignore timers with both on and off times set to 00:00
      if (t.onHour == 0 && t.onMinute == 0 && t.offHour == 0 && t.offMinute == 0) {
        continue;
      }

      // Check if the current time matches the ON time of a timer
      if (currentHour == t.onHour && currentMinute == t.onMinute && !t.lastActionOn) {
        ws2812fx.start(); // Turn on the LED strip
        t.lastActionOn = true; // Mark "on" action as triggered
        Serial.println("Timer ON: LED strip turned ON");
      }

      // Check if the current time matches the OFF time of a timer
      if (currentHour == t.offHour && currentMinute == t.offMinute && t.lastActionOn) {
        ws2812fx.stop(); // Turn off the LED strip
        t.lastActionOn = false; // Mark "off" action as triggered
        Serial.println("Timer OFF: LED strip turned OFF");
      }
    }
  }
}


  




void handleSetPin() {
  Serial.println("Handling set pin request...");
  
  if (server.hasArg("pin")) {
    LED_PIN = server.arg("pin").toInt(); // Set the pin
    Serial.println("Received pin: " + String(LED_PIN));
    
    ws2812fx.setPin(LED_PIN); // Reinitialize the pin
    ws2812fx.init(); // Reinitialize the LED strip with the new pin
    ws2812fx.start(); // Start the LED strip
    saveState(); // Save the new state

    Serial.println("Pin successfully set. Reinitialized LED strip.");
    server.send(200, "text/plain", "GPIO Pin set to " + String(LED_PIN));
  } else {
    Serial.println("Bad Request: Missing 'pin' argument.");
    server.send(400, "text/plain", "Bad Request: Missing 'pin' argument");
  }
}

void handleSetLedCount() {
  Serial.println("Handling set LED count request...");
  
  if (server.hasArg("ledCount")) {
    int receivedCount = server.arg("ledCount").toInt(); // Convert to integer
    Serial.println("Received LED count (raw): " + server.arg("ledCount"));
    Serial.println("Parsed LED count: " + String(receivedCount));
    
    if (receivedCount > 0) { // Ensure the count is valid
      LED_COUNT = receivedCount;
      ws2812fx.setLength(LED_COUNT); // Adjust the length of the LED strip
      ws2812fx.init(); // Reinitialize the LED strip with the new count
      ws2812fx.start(); // Start the LED strip
      saveState(); // Save the new state

      Serial.println("LED count successfully set to " + String(LED_COUNT));
      server.send(200, "text/plain", "LED Count set to " + String(LED_COUNT));
    } else {
      Serial.println("Invalid LED count: " + String(receivedCount));
      server.send(400, "text/plain", "Invalid LED count: must be greater than 0");
    }
  } else {
    Serial.println("Bad Request: Missing 'ledCount' argument.");
    server.send(400, "text/plain", "Bad Request: Missing 'ledCount' argument");
  }
}

//
//
uint32_t hexToColor(String hex) {
  if (hex.length() == 6) {
    return strtol(("0x" + hex).c_str(), NULL, 16);
  } else {
    return 0xFF0000; // Default to red if there's an issue
  }
}



void handleSetAnimation() {
  if (server.hasArg("num")) {
    animationNUM = server.arg("num").toInt(); // Assign the correct value to animationNUM
    Serial.print("Received animation number: ");
    Serial.println(animationNUM);
    ws2812fx.setSpeed(speed);       // Set animation speed in milliseconds
    ws2812fx.setMode(animationNUM);
    ws2812fx.setColor(currentColor);
    ws2812fx.start();
    
    saveState(); // Save the new state
    server.send(200, "text/plain", "Animation set to mode " + String(animationNUM));
  } else {
    server.send(400, "text/plain", "Bad Request: Missing 'num' argument");
  }
}





// Gamma correction function
uint8_t gammaCorrection(uint8_t value, float gamma) {
  return (uint8_t)(pow((float)value / 255.0, gamma) * 255.0);
}

// Function to apply basic color correction (adjusts RGB channels)
uint32_t colorCorrection(uint32_t color, float redAdjust, float greenAdjust, float blueAdjust) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;

  // Apply basic color correction by adjusting RGB channels
  r = constrain(r * redAdjust, 0, 255);
  g = constrain(g * greenAdjust, 0, 255);
  b = constrain(b * blueAdjust, 0, 255);

  // Apply gamma correction
  r = gammaCorrection(r, 2.2); // Applying gamma correction (2.2 is common)
  g = gammaCorrection(g, 2.2);
  b = gammaCorrection(b, 2.2);

  return (r << 16) | (g << 8) | b;
}

void handleSetColor() {
  if (server.hasArg("color")) {
    String colorHex = server.arg("color");
    currentColor = hexToColor(colorHex);
    Serial.println(colorHex);
    
    server.send(200, "text/plain", "Color set to #" + colorHex);
  } else {
    server.send(400, "text/plain", "Bad Request: Missing 'color' argument");
  }
    // Example color correction: Increase red, decrease green and blue (just an example)
    float redAdjust = 1.2;  // Increase red channel by 20%
    float greenAdjust = 0.8; // Decrease green channel by 20%
    float blueAdjust = 0.9;  // Decrease blue channel by 10%

    currentColor = colorCorrection(currentColor, redAdjust, greenAdjust, blueAdjust);
    
    ws2812fx.setColor(currentColor);
    Serial.print("Set color to: #");
    saveState();
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    brightness = server.arg("brightness").toInt(); // Assign the correct value to brightness
    ws2812fx.setBrightness(brightness);
    Serial.print("Set brightness to: ");
    Serial.println(brightness);
    
//    saveState(); // Save the new brightness
    server.send(200, "text/plain", "Brightness set to " + String(brightness));
  } else {
    server.send(400, "text/plain", "Bad Request: Missing 'brightness' argument");
  }
}

void handleTogglePower() {
  if (server.hasArg("state")) {
    powerState = server.arg("state") == "on"; // Convert to boolean (true for "on", false for "off")
    
    if (powerState) {
      ws2812fx.start(); // Turn on the LED strip
    } else {
      ws2812fx.stop(); // Turn off the LED strip
    }
    
    Serial.print("Power state set to: ");
    Serial.println(powerState ? "on" : "off");
    
//    saveState(); // Save the new power state
    server.send(200, "text/plain", "Power " + String(powerState ? "on" : "off"));
  } else {
    server.send(400, "text/plain", "Bad Request: Missing 'state' argument");
  }
}

void handleGetState() {
  // Set CORS header to allow all origins (or specify a particular domain if needed)
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  String state = "{";
  state += "\"LED_PIN\":" + String(LED_PIN) + ",";         // Add LED pin
  state += "\"LED_COUNT\":" + String(LED_COUNT) + ",";     // Add LED count
  state += "\"animationNUM\":" + String(animationNUM) + ","; 
  state += "\"currentColor\":\"" + String(currentColor, HEX) + "\","; 
  state += "\"brightness\":" + String(brightness) + ","; 
  state += "\"powerState\":" + String(powerState ? "true" : "false") + ","; // Boolean value, no quotes
  // Include timers in the JSON state
  state += "\"timers\":[";
  for (int i = 0; i < 3; i++) {
    state += "{";
    state += "\"onHour\":" + String(timers[i].onHour) + ",";
    state += "\"onMinute\":" + String(timers[i].onMinute) + ",";
    state += "\"offHour\":" + String(timers[i].offHour) + ",";
    state += "\"offMinute\":" + String(timers[i].offMinute);
    state += "}";
    if (i < 2) state += ","; // Add a comma between timer objects
  }
  state += "]";
  
  state += "}"; // Close the main JSON object

  server.send(200, "application/json", state);
}
