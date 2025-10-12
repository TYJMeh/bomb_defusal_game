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

// Button configuration (loaded from Raspberry Pi JSON)
String currentButtonId = "button_1";  // Current active button
int targetTime = 2000;
int bufferTime = 500;
bool buttonEnabled = true;

// Available button configurations (loaded from RPi)
struct ButtonConfig {
  String id;
  int target_time;
  int buffer;
  bool enabled;
};

// Store up to 3 button configs
ButtonConfig buttonConfigs[3];
int numButtonConfigs = 0;
int currentButtonIndex = 0;

// Game state
bool gameActive = true;
bool gameWon = false;
bool gamePaused = false;
int waiting = 0;
bool configReceived = false;

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

  DynamicJsonDocument doc(2048);
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
  else if (type == "BUTTON_CONFIG_RESPONSE") {
    // Receive all button configurations from RPi
    Serial.println("📥 Received button configuration from RPi");
    
    JsonObject button_ID = doc["button_ID"];
    numButtonConfigs = 0;
    
    // Load button_1
    if (button_ID.containsKey("button_1")) {
      JsonObject btn1 = button_ID["button_1"];
      buttonConfigs[numButtonConfigs].id = "button_1";
      buttonConfigs[numButtonConfigs].target_time = btn1["target_time"];
      buttonConfigs[numButtonConfigs].buffer = btn1["buffer"];
      buttonConfigs[numButtonConfigs].enabled = btn1["enabled"];
      numButtonConfigs++;
    }
    
    // Load button_2
    if (button_ID.containsKey("button_2")) {
      JsonObject btn2 = button_ID["button_2"];
      buttonConfigs[numButtonConfigs].id = "button_2";
      buttonConfigs[numButtonConfigs].target_time = btn2["target_time"];
      buttonConfigs[numButtonConfigs].buffer = btn2["buffer"];
      buttonConfigs[numButtonConfigs].enabled = btn2["enabled"];
      numButtonConfigs++;
    }
    
    // Load button_3
    if (button_ID.containsKey("button_3")) {
      JsonObject btn3 = button_ID["button_3"];
      buttonConfigs[numButtonConfigs].id = "button_3";
      buttonConfigs[numButtonConfigs].target_time = btn3["target_time"];
      buttonConfigs[numButtonConfigs].buffer = btn3["buffer"];
      buttonConfigs[numButtonConfigs].enabled = btn3["enabled"];
      numButtonConfigs++;
    }
    
    Serial.printf("Loaded %d button configurations\n", numButtonConfigs);
    
    // Set the first enabled button as active
    setActiveButton(0);
    configReceived = true;
    
    sendButtonConfigUpdate();
  }
  else if (command == "SWITCH_BUTTON" || type == "SWITCH_BUTTON") {
    // Switch to a different button configuration
    String newButtonId = doc["button_id"];
    
    for (int i = 0; i < numButtonConfigs; i++) {
      if (buttonConfigs[i].id == newButtonId) {
        setActiveButton(i);
        Serial.printf("Switched to %s\n", newButtonId.c_str());
        sendButtonConfigUpdate();
        break;
      }
    }
  }
  else if (command == "ACTIVATE" || type == "ACTIVATE") {
    waiting = 1;
    Serial.println("🚀 Button module activated! All modules connected.");
    
    // Request button configuration if not received yet
    if (!configReceived) {
      requestButtonConfig();
    }
    
    sendConnectionStatus();
  }
}

void setActiveButton(int index) {
  if (index >= 0 && index < numButtonConfigs) {
    currentButtonIndex = index;
    currentButtonId = buttonConfigs[index].id;
    targetTime = buttonConfigs[index].target_time;
    bufferTime = buttonConfigs[index].buffer;
    buttonEnabled = buttonConfigs[index].enabled;
    
    Serial.println("=== Active Button Configuration ===");
    Serial.printf("  ID: %s\n", currentButtonId.c_str());
    Serial.printf("  Target: %d ms\n", targetTime);
    Serial.printf("  Buffer: ±%d ms\n", bufferTime);
    Serial.printf("  Enabled: %s\n", buttonEnabled ? "Yes" : "No");
    Serial.println("===================================");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32ButtonGame")) {
      Serial.println("connected");
      client.subscribe(subscribe_topic);
      
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
  doc["config_received"] = configReceived;
  doc["timestamp"] = millis();
  
  String output;
  serializeJson(doc, output);
  client.publish(publish_topic, output.c_str());
  Serial.println("Sent connection status to Raspberry Pi");
}

void requestButtonConfig() {
  DynamicJsonDocument doc(256);
  doc["type"] = "REQUEST_BUTTON_CONFIG";
  doc["message"] = "Requesting button configuration from database";
  doc["device"] = "ESP32_Button";
  doc["timestamp"] = millis();
  
  String output;
  serializeJson(doc, output);
  client.publish(publish_topic, output.c_str());
  Serial.println("📤 Requested button configuration from Raspberry Pi");
}

void sendButtonConfigUpdate() {
  DynamicJsonDocument doc(512);
  doc["type"] = "BUTTON_CONFIG_UPDATED";
  doc["message"] = "Active button configuration";
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
  
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  Serial.println("ESP32 Button Game Started!");
  Serial.println("Waiting for configuration from Raspberry Pi...");
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
    doc["config_received"] = configReceived;
    
    String output;
    serializeJson(doc, output);
    client.publish(publish_topic, output.c_str());
  }
}

void loop() {
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

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  if (waiting == 0) {
    return;
  }

  // Request config if not received after activation
  if (waiting == 1 && !configReceived) {
    static unsigned long lastConfigRequest = 0;
    if (millis() - lastConfigRequest > 5000) {
      requestButtonConfig();
      lastConfigRequest = millis();
    }
  }

  if (!gameActive || gamePaused || gameWon || !buttonEnabled || !configReceived) {
    return;
  }

  buttonState = digitalRead(buttonPin);
  
  if (buttonState == LOW && lastButtonState == HIGH) {
    buttonPressTime = millis();
  }
  
  if (buttonState == HIGH && lastButtonState == LOW) {
    pressDuration = millis() - buttonPressTime;
    Serial.print("Press duration: ");
    Serial.print(pressDuration);
    Serial.println(" ms");
    
    if (buttonPressTime != 0) {
      int minTime = targetTime - bufferTime;
      int maxTime = targetTime + bufferTime;
      
      if (pressDuration >= minTime && pressDuration <= maxTime) {
        Serial.println("You Win!");
        gameWon = true;
        gameActive = false;
        
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        
        sendGameResult(true, pressDuration);
        
        delay(3000);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
      }
      else {
        Serial.println("Wrong timing! Try again!");
        
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        
        sendGameResult(false, pressDuration);
        
        delay(1000);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        
        Serial.println("Game continues - try again!");
      }
      
      int difference = pressDuration - targetTime;
      Serial.print("You were ");
      Serial.print(abs(difference));
      Serial.println(" ms off target");
      
      if (abs(difference) < 100) {
        Serial.println("Performance: Excellent! (< 100ms off)");
      } else if (abs(difference) < 300) {
        Serial.println("Performance: Good! (< 300ms off)");
      } else if (abs(difference) < bufferTime) {
        Serial.println("Performance: Close! (within buffer)");
      }
    }
  }
  
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