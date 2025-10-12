#include <FastLED.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define NUM_LEDS    20
const int buttonPin = 23;
const int ledPin = 32;

// WiFi and MQTT settings
const char* ssid = "advaspire_2.4G";
const char* password = "0172037375";
const char* mqtt_server = "192.168.1.201";
const char* subscribe_topic = "rpi/to/esp4";
const char* publish_topic = "esp4/to/rpi";

WiFiClient espClient;
PubSubClient client(espClient);

// Game variables
int buttonState = 0;
int lastButtonState = 0; 
int currentLed = 0;
unsigned long lastLedTime = 0;
const int ledDelay = 500;

unsigned long buttonPressTime = 0;
unsigned long pressDuration = 0;

// Button configuration (loaded from Raspberry Pi)
String currentButtonId = "button_1";
int targetTime = 2000;
int bufferTime = 500;
bool buttonEnabled = true;

// Game state
bool gameActive = true;
bool gameWon = false;
bool gamePaused = false;
int waiting = 0;

CRGB leds[NUM_LEDS];

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 3000;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Parse JSON message
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  String command = doc["command"];
  String type = doc["type"];
  
  if (command == "X" || type == "X") {
    Serial.println("Received 'X' signal - Penalty applied!");
    
    // Flash red LEDs to indicate penalty
    for (int i = 0; i < 3; i++) {
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      delay(300);
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      delay(300);
    }
    
    Serial.println("Game continues after penalty!");
  }
  else if (command == "START_GAME" || type == "START_GAME") {
    Serial.println("Starting new game!");
    gameActive = true;
    gameWon = false;
    gamePaused = false;
    currentLed = 0;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
  else if (command == "RESET_GAME" || type == "RESET_GAME") {
    Serial.println("Resetting game!");
    gameActive = true;
    gameWon = false;
    gamePaused = false;
    currentLed = 0;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
  else if (command == "PAUSE_TIMER" || type == "PAUSE_TIMER") {
    gamePaused = true;
    Serial.println("⏸️ Button game paused");
    
    // Visual feedback - orange/yellow LEDs
    fill_solid(leds, NUM_LEDS, CRGB(255, 165, 0));
    FastLED.show();
    delay(300);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    
    DynamicJsonDocument doc(256);
    doc["type"] = "BUTTON_GAME_PAUSED";
    doc["message"] = "Button game paused";
    doc["device"] = "ESP32_Button";
    doc["timestamp"] = millis();
    
    String output;
    serializeJson(doc, output);
    client.publish(publish_topic, output.c_str());
  }
  else if (command == "RESUME_TIMER" || type == "RESUME_TIMER") {
    if (!gameWon) {
      gamePaused = false;
      Serial.println("▶️ Button game resumed");
      
      // Visual feedback - green flash
      fill_solid(leds, NUM_LEDS, CRGB::Green);
      FastLED.show();
      delay(300);
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      
      DynamicJsonDocument doc(256);
      doc["type"] = "BUTTON_GAME_RESUMED";
      doc["message"] = "Button game resumed";
      doc["device"] = "ESP32_Button";
      doc["timestamp"] = millis();
      
      String output;
      serializeJson(doc, output);
      client.publish(publish_topic, output.c_str());
    }
  }
  else if (command == "UPDATE_BUTTON_CONFIG" || type == "UPDATE_BUTTON_CONFIG") {
    // Update button configuration from Raspberry Pi
    if (doc.containsKey("button_id")) {
      currentButtonId = doc["button_id"].as<String>();
    }
    if (doc.containsKey("target_time")) {
      targetTime = doc["target_time"];
    }
    if (doc.containsKey("buffer")) {
      bufferTime = doc["buffer"];
    }
    if (doc.containsKey("enabled")) {
      buttonEnabled = doc["enabled"];
    }
    
    Serial.println("Button configuration updated:");
    Serial.printf("  Button ID: %s\n", currentButtonId.c_str());
    Serial.printf("  Target time: %d ms\n", targetTime);
    Serial.printf("  Buffer: %d ms\n", bufferTime);
    Serial.printf("  Enabled: %s\n", buttonEnabled ? "Yes" : "No");
    
    // Send confirmation
    sendButtonConfigUpdate();
  }
  else if (command == "ACTIVATE" || type == "ACTIVATE") {
    // Activate module - all modules are connected
    waiting = 1;
    Serial.println("🚀 Button module activated! All modules connected.");
    
    // Request button configuration from Raspberry Pi
    requestButtonConfig();
    
    sendConnectionStatus();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32ButtonGame")) {
      Serial.println("connected");
      client.subscribe(subscribe_topic);
      
      // Send connection status
      sendConnectionStatus();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

void sendConnectionStatus() {
  DynamicJsonDocument doc(512);
  doc["type"] = "BUTTON_MODULE_CONNECTED";
  doc["message"] = "Button timing module ready";
  doc["device"] = "ESP32_Button";
  doc["button_id"] = currentButtonId;
  doc["target_time"] = targetTime;
  doc["buffer"] = bufferTime;
  doc["enabled"] = buttonEnabled;
  doc["timestamp"] = millis();
  
  String output;
  serializeJson(doc, output);
  client.publish(publish_topic, output.c_str());
  Serial.println("Sent connection status to Raspberry Pi");
}

void requestButtonConfig() {
  DynamicJsonDocument doc(256);
  doc["type"] = "REQUEST_BUTTON_CONFIG";
  doc["message"] = "Requesting button configuration";
  doc["device"] = "ESP32_Button";
  doc["timestamp"] = millis();
  
  String output;
  serializeJson(doc, output);
  client.publish(publish_topic, output.c_str());
  Serial.println("Requested button configuration from Raspberry Pi");
}

void sendButtonConfigUpdate() {
  DynamicJsonDocument doc(512);
  doc["type"] = "BUTTON_CONFIG_UPDATED";
  doc["message"] = "Button configuration received and applied";
  doc["device"] = "ESP32_Button";
  doc["button_id"] = currentButtonId;
  doc["target_time"] = targetTime;
  doc["buffer"] = bufferTime;
  doc["enabled"] = buttonEnabled;
  doc["timestamp"] = millis();
  
  String output;
  serializeJson(doc, output);
  client.publish(publish_topic, output.c_str());
}

void sendGameResult(bool won, unsigned long duration) {
  DynamicJsonDocument doc(1024);
  
  if (won) {
    doc["type"] = "BUTTON_GAME_WON";
    doc["message"] = "Player won the button timing game!";
  } else {
    doc["type"] = "BUTTON_GAME_LOST";
    doc["message"] = "Player lost the button timing game!";
  }
  
  doc["button_id"] = currentButtonId;
  doc["press_duration"] = duration;
  doc["target_time"] = targetTime;
  doc["buffer"] = bufferTime;
  doc["difference"] = abs((int)duration - targetTime);
  doc["timestamp"] = millis();
  doc["device"] = "ESP32_Button";
  
  String output;
  serializeJson(doc, output);
  
  client.publish(publish_topic, output.c_str());
  Serial.println("Sent game result to Raspberry Pi");
}

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  FastLED.setBrightness(50);
  Serial.begin(115200);
  FastLED.addLeds<WS2812, ledPin, GRB>(leds, NUM_LEDS);
  
  // Initialize all LEDs to off
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  Serial.println("ESP32 Button Game Started!");
  Serial.println("Hold button to light up LEDs one by one");
  Serial.print("Current Button ID: ");
  Serial.println(currentButtonId);
  Serial.print("Target time: ");
  Serial.print(targetTime);
  Serial.print(" ms (+/- ");
  Serial.print(bufferTime);
  Serial.println(" ms)");
}

void sendHeartbeat() {
  if (client.connected() && waiting == 1) {
    DynamicJsonDocument doc(256);
    doc["type"] = "HEARTBEAT";
    doc["device"] = "ESP32_Button";
    doc["timestamp"] = millis();
    doc["game_active"] = gameActive;
    doc["game_won"] = gameWon;
    doc["game_paused"] = gamePaused;
    doc["button_id"] = currentButtonId;
    doc["target_time"] = targetTime;
    
    String output;
    serializeJson(doc, output);
    client.publish(publish_topic, output.c_str());
    
    // Optional debug output
    // Serial.println("💓 Heartbeat sent");
  }
}

void loop() {
  // Check WiFi connection first
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected!");
    }
  }
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Send heartbeat every 3 seconds - MOVED BEFORE the return statements
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  // Check waiting status
  if (waiting == 0) {
    // Module is waiting for all modules to connect
    // Only handle MQTT, no game logic
    return;
  }

  // Only process game logic if game is active, not paused, not won, and button is enabled
  if (!gameActive || gamePaused || gameWon || !buttonEnabled) {
    // Still send heartbeats but don't process button input
    return;
  }

  buttonState = digitalRead(buttonPin);
  
  // Check for button press (transition from HIGH to LOW)
  if (buttonState == LOW && lastButtonState == HIGH) {
    buttonPressTime = millis();
  }
  
  // Check for button release (transition from LOW to HIGH)
  if (buttonState == HIGH && lastButtonState == LOW) {
    pressDuration = millis() - buttonPressTime;
    Serial.print("Press duration: ");
    Serial.print(pressDuration);
    Serial.println(" ms");
    
    if (buttonPressTime != 0) {
      // Check if within target range (target ± buffer)
      int minTime = targetTime - bufferTime;
      int maxTime = targetTime + bufferTime;
      
      if (pressDuration >= minTime && pressDuration <= maxTime) {
        Serial.println("You Win!");
        gameWon = true;
        gameActive = false;
        
        // Light up LEDs in green for win
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        
        // Send win message to Raspberry Pi
        sendGameResult(true, pressDuration);
        
        delay(3000);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
      }
      else {
        Serial.println("Wrong timing! Try again!");
        
        // Light up LEDs in red briefly for wrong timing
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        
        // Send wrong timing message to Raspberry Pi (triggers X penalty)
        sendGameResult(false, pressDuration);
        
        delay(1000);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        
        // Game continues - don't set gameActive = false
        Serial.println("Game continues - try again!");
      }
      
      int difference = pressDuration - targetTime;
      Serial.print("You were ");
      Serial.print(abs(difference));
      Serial.println(" ms off target");
      
      // Show performance feedback
      if (abs(difference) < 100) {
        Serial.println("Performance: Excellent! (< 100ms off)");
      } else if (abs(difference) < 300) {
        Serial.println("Performance: Good! (< 300ms off)");
      } else if (abs(difference) < bufferTime) {
        Serial.println("Performance: Close! (within buffer)");
      }
    }
  }
  
  // Button is currently pressed (LOW due to INPUT_PULLUP)
  if (buttonState == LOW && gameActive) {
    if (millis() - lastLedTime >= ledDelay) {
      if (currentLed < NUM_LEDS) {
        leds[currentLed] = CRGB(0, 0, 255);
        FastLED.show();
        currentLed++;
        lastLedTime = millis();
      }
    }
  }
  // Button is released (HIGH due to INPUT_PULLUP)
  else if (gameActive) {
    if (currentLed > 0) {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      currentLed = 0;
    }
  }
  
  lastButtonState = buttonState;
  delay(10);
}