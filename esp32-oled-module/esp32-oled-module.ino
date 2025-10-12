#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi settings
const char* ssid = "advaspire_2.4G";
const char* password = "0172037375";

// MQTT Broker settings
const char* mqtt_broker = "192.168.1.201";
const int mqtt_port = 1883;
String clientId = "ESP32_Display_" + String(random(0xffff), HEX);
const char* topic_from_rpi = "rpi/to/esp2";
const char* topic_to_rpi = "esp2/to/rpi";

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT_TIMER 64
#define SCREEN_HEIGHT_COUNTER 32
#define OLED_RESET    -1

// I2C Bus 1 pins (Timer Display)
#define SDA1 21
#define SCL1 22

// I2C Bus 2 pins (X Counter Display)
#define SDA2 25
#define SCL2 26

#define DISPLAY_ADDRESS 0x3C

TwoWire I2C_1 = TwoWire(0);
TwoWire I2C_2 = TwoWire(1);

Adafruit_SSD1306 display1(SCREEN_WIDTH, SCREEN_HEIGHT_TIMER, &I2C_1, OLED_RESET);
Adafruit_SSD1306 display2(SCREEN_WIDTH, SCREEN_HEIGHT_COUNTER, &I2C_2, OLED_RESET);

// Global variables
int countdownTime = 0;
int initialCountdownTime = 0;
int pausedTimeRemaining = 0;  // Store remaining time when paused
bool countdownActive = false;
bool countdownPaused = false;  // Track if timer is paused
unsigned long lastUpdate = 0;
int xCount = 0;
int maxXCount = 3;
int waiting = 0;
unsigned long timerUpdateInterval = 1000;  // Base update interval (1 second)
unsigned long lastHeartbeat = 0;  // Last time we sent a heartbeat

void setup() {
  Serial.begin(115200);
  
  Serial.println("=== ESP32 Dual OLED Display with MQTT ===");
  Serial.println("Timer Display - I2C Bus 1 (SDA=21, SCL=22) - 128x64");
  Serial.println("X Counter Display - I2C Bus 2 (SDA=25, SCL=26) - 128x32");
  
  setupWiFi();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttCallback);
  
  I2C_1.begin(SDA1, SCL1);
  if(!display1.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS)) {
    Serial.println("Display 1 (Timer) allocation failed");
    for(;;);
  }
  Serial.println("✓ Display 1 (Timer 128x64) initialized successfully");
  
  I2C_2.begin(SDA2, SCL2);
  if(!display2.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS)) {
    Serial.println("Display 2 (X Counter) allocation failed");
    for(;;);
  }
  display2.setRotation(2);
  Serial.println("✓ Display 2 (X Counter 128x32) initialized successfully");
  
  display1.clearDisplay();
  display2.clearDisplay();
  
  showInitialMessage();
  updateXDisplay();
  
  Serial.println("\nCommands:");
  Serial.println("  'x' - Add X mark");
  Serial.println("  'reset' - Reset X counter");
  Serial.println("  'start 30' - Start 30-second timer");
  Serial.println("  'stop' - Stop timer");
  
  delay(3000);
  showWaitingMessage();
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
    reconnectMQTT();
  }
  client.loop();
  
  // Update countdown with dynamic interval based on X count
  if (countdownActive && millis() - lastUpdate >= timerUpdateInterval) {
    updateCountdown();
    lastUpdate = millis();
  }
  
  if (waiting == 0) {
    handleSerialInput();
  }
  else if (waiting == 1) {
    handleSerialInput();
  }
}

void handleSerialInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "x" || input == "X") {
      addXMark("MANUAL");
    }
    else if (input.startsWith("start")) {
      int duration = input.substring(6).toInt();
      if (duration > 0) {
        startCountdown(duration);
      } else {
        startCountdown(10);
      }
    }
    else if (input == "reset") {
      resetXCounter();
    }
    else if (input == "stop") {
      stopCountdown();
    }
    else if (input == "test") {
      testDisplays();
    }
    else if (input.startsWith("max")) {
      int newMax = input.substring(4).toInt();
      if (newMax > 0 && newMax <= 10) {
        maxXCount = newMax;
        Serial.printf("Max X count set to: %d\n", maxXCount);
        updateXDisplay();
      }
    }
  }
}

// ===== WIFI AND MQTT FUNCTIONS =====

void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📨 MQTT Message received [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  
  processMQTTMessage(message);
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      client.subscribe(topic_from_rpi);
      Serial.printf("Subscribed to: %s\n", topic_from_rpi);
      
      sendToRaspberryPi("DISPLAY_CONNECTED", "Display system ready");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void processMQTTMessage(String message) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    Serial.println("🔍 JSON Parsed successfully:");
    
    String type = doc["type"];
    String content = doc["message"];
    String command = doc["command"];
    
    Serial.printf("  - type: '%s'\n", type.c_str());
    Serial.printf("  - command: '%s'\n", command.c_str());
    Serial.printf("  - message: '%s'\n", content.c_str());
    Serial.printf("  - Current waiting status: %d\n", waiting);
    
    if (type == "X" || type == "WRONG_WIRE" || type == "FAILURE" || command == "X") {
      Serial.printf("📨 Received X command via MQTT: %s\n", type.c_str());
      addXMark(type.length() > 0 ? type : "MQTT_COMMAND");
    }
    else if (type == "RESET_X" || type == "NEW_GAME" || command == "RESET_X") {
      resetXCounter();
    }
    else if (type == "START_TIMER" || command == "START_TIMER") {
      int duration = doc["duration"] | 300;
      Serial.printf("🚀 Starting timer for %d seconds\n", duration);
      startCountdown(duration);
    }
    else if (type == "PAUSE_TIMER" || command == "PAUSE_TIMER") {
      Serial.println("⏹️ Stopping timer via MQTT");
      stopCountdown();
    }
    else if (type == "TEST" || command == "TEST") {
      testDisplays();
    }
    else if (type == "ACTIVATE" || command == "ACTIVATE") {
      waiting = 1;
      Serial.println("🚀 Display module activated! All modules connected.");
      sendToRaspberryPi("DISPLAY_ACTIVATED", "Display module activated and ready");
      int duration = doc["duration"] | 300;
      Serial.printf("🚀 Starting timer for %d seconds\n", duration);
      startCountdown(duration);
      
      showActivatedMessage();
    }
    else if (type == "GAME_OVER" || command == "GAME_OVER") {
      Serial.println("💥 Game Over received!");
      pausedTimeRemaining = 0; 
      countdownPaused = false;  
      stopCountdown();
      showGameOver();
    }
    else if (type == "VICTORY" || command == "VICTORY") {
      Serial.println("🎉 Victory received!");
      pausedTimeRemaining = 0;
      countdownPaused = false;
      stopCountdown();
      showVictory();
    }
    else {
      Serial.printf("❓ Unknown JSON message - type: '%s', command: '%s'\n", type.c_str(), command.c_str());
    }
  }
  else {
    Serial.printf("❌ JSON Parse failed: %s\n", error.c_str());
    Serial.printf("Raw message: '%s'\n", message.c_str());
    
    message.toUpperCase();
    
    if (message == "X") {
      Serial.println("📨 Received X command via simple MQTT message");
      addXMark("RPI_SIMPLE");
    }
    else if (message == "RESET") {
      resetXCounter();
    }
    else if (message.startsWith("TIMER")) {
      int duration = message.substring(5).toInt();
      if (duration > 0) {
        startCountdown(duration);
      }
    }
    else if (message == "ACTIVATE") {
      waiting = 1;
      Serial.println("🚀 Display module activated via simple message!");
      sendToRaspberryPi("DISPLAY_ACTIVATED", "Display module activated and ready");
      showActivatedMessage();
    }
    else {
      Serial.printf("❓ Unknown simple message: %s\n", message.c_str());
    }
  }
}

void sendToRaspberryPi(String message_type, String message_content) {
  if (client.connected()) {
    StaticJsonDocument<256> doc;
    doc["type"] = message_type;
    doc["message"] = message_content;
    doc["timestamp"] = millis();
    doc["device"] = "ESP32_Display";
    doc["x_count"] = xCount;
    doc["max_x_count"] = maxXCount;
    
    String json_string;
    serializeJson(doc, json_string);
    
    client.publish(topic_to_rpi, json_string.c_str());
    
    Serial.printf("📡 SENT TO RPI: %s - %s\n", message_type.c_str(), message_content.c_str());
  } else {
    Serial.println("❌ MQTT not connected - message not sent");
  }
}

// ===== DISPLAY FUNCTIONS =====

void updateTimerSpeed() {
  // Update timer speed based on X count
  // 0 X's = normal speed (1000ms = 1 second)
  // 1 X = 25% faster (750ms = 1 second, so timer runs 1.33x speed)
  // 2 X's = 50% faster (500ms = 1 second, so timer runs 2x speed)
  
  if (xCount == 0) {
    timerUpdateInterval = 1000;  // Normal speed
  } else if (xCount == 1) {
    timerUpdateInterval = 750;   // 25% faster
  } else if (xCount >= 2) {
    timerUpdateInterval = 500;   // 50% faster
  }
  
  Serial.printf("Timer speed updated: %d X's = %dms interval\n", xCount, timerUpdateInterval);
}

void addXMark(String source) {
  Serial.printf("🔍 addXMark called from: %s, current count: %d/%d\n", source.c_str(), xCount, maxXCount);
  
  if (xCount < maxXCount) {
    xCount++;
    Serial.printf("✅ X count increased to: %d/%d\n", xCount, maxXCount);
    updateXDisplay();
    updateTimerSpeed();  // Update timer speed when X is added
    Serial.printf("❌ X added from %s! Total count: %d/%d\n", source.c_str(), xCount, maxXCount);
    
    sendToRaspberryPi("X_ADDED", "X mark added, total: " + String(xCount) + "/" + String(maxXCount));
    
    if (xCount >= maxXCount) {
      Serial.println("🚨 MAXIMUM X COUNT REACHED!");
      sendToRaspberryPi("MAX_X_REACHED", "Maximum X count reached: " + String(maxXCount));
    }
  } else {
    Serial.printf("Maximum X count reached (%d)\n", maxXCount);
    sendToRaspberryPi("X_LIMIT_REACHED", "Cannot add X - limit reached: " + String(maxXCount));
  }
}

void resetXCounter() {
  Serial.printf("🔄 Resetting X counter from %d to 0\n", xCount);
  xCount = 0;
  updateXDisplay();
  updateTimerSpeed();  // Reset timer speed to normal
  Serial.println("🔄 X counter reset to 0");
  sendToRaspberryPi("X_RESET", "X counter reset to 0");
}

void showMaxXReached() {
  display2.clearDisplay();
  display2.setTextSize(2);
  display2.setTextColor(SSD1306_WHITE);
  display2.setCursor(35, 5);
  display2.println("MAX");
  display2.setCursor(15, 20);
  display2.println("LIMIT!");
  
  display2.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT_COUNTER, SSD1306_WHITE);
  display2.drawRect(1, 1, SCREEN_WIDTH-2, SCREEN_HEIGHT_COUNTER-2, SSD1306_WHITE);
  
  display2.display();
  
  for (int i = 0; i < 4; i++) {
    delay(500);
    display2.invertDisplay(i % 2);
  }
  
  delay(1000);
  updateXDisplay();
}

void testDisplays() {
  Serial.println("Testing both displays...");
  
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(20, 20);
  display1.println("TIMER");
  display1.setCursor(15, 40);
  display1.println("128x64");
  display1.display();
  
  display2.clearDisplay();
  display2.setTextSize(2);
  display2.setTextColor(SSD1306_WHITE);
  display2.setCursor(10, 5);
  display2.println("X COUNT");
  display2.setCursor(15, 20);
  display2.println("128x32");
  display2.display();
  
  Serial.println("Both displays should show their info");
  
  delay(3000);
  showWaitingMessage();
  updateXDisplay();
}

void showInitialMessage() {
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(25, 10);
  display1.println("TIMER");
  display1.setTextSize(1);
  display1.setCursor(20, 35);
  display1.println("Connecting...");
  display1.setCursor(15, 50);
  display1.println("Please wait");
  display1.display();
}

void showWaitingMessage() {
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(15, 15);
  display1.println("WAITING");
  display1.setTextSize(1);
  display1.setCursor(25, 40);
  display1.println("For commands");
  display1.setCursor(30, 52);
  display1.println("from RPI");
  display1.display();
}

void showActivatedMessage() {
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(20, 10);
  display1.println("SYSTEM");
  display1.setCursor(15, 30);
  display1.println("ACTIVE");
  display1.setTextSize(1);
  display1.setCursor(15, 50);
  display1.println("Ready for game");
  display1.display();
}

void showGameOver() {
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(25, 15);
  display1.println("GAME");
  display1.setCursor(25, 35);
  display1.println("OVER");
  
  for (int i = 0; i < 3; i++) {
    display1.drawRect(i * 2, i * 2, SCREEN_WIDTH - i * 4, SCREEN_HEIGHT_TIMER - i * 4, SSD1306_WHITE);
  }
  display1.display();
  
  for (int i = 0; i < 6; i++) {
    delay(300);
    display1.invertDisplay(i % 2);
  }
  
  display1.invertDisplay(false);
}

void showVictory() {
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(15, 10);
  display1.println("SUCCESS!");
  display1.setTextSize(1);
  display1.setCursor(20, 35);
  display1.println("All puzzles");
  display1.setCursor(25, 45);
  display1.println("completed!");
  
  display1.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT_TIMER, SSD1306_WHITE);
  display1.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT_TIMER-4, SSD1306_WHITE);
  
  display1.display();
  
  for (int i = 0; i < 8; i++) {
    delay(250);
    if (i % 2 == 0) {
      display1.invertDisplay(true);
    } else {
      display1.invertDisplay(false);
    }
  }
  
  display1.invertDisplay(false);
}

void startCountdown(int seconds) {
  if (seconds <= 0) return;
  
  countdownTime = seconds;
  initialCountdownTime = seconds;
  countdownActive = true;
  lastUpdate = millis();
  updateTimerSpeed();  // Set initial timer speed based on current X count
  
  displayCountdown();
  
  Serial.printf("⏰ Countdown started: %d seconds\n", seconds);
  Serial.printf("Timer speed: %dms interval (%d X's)\n", timerUpdateInterval, xCount);
  sendToRaspberryPi("TIMER_STARTED", "Countdown started: " + String(seconds) + " seconds");
}

void updateCountdown() {
  if (!countdownActive) return;
  
  countdownTime--;
  
  if (countdownTime <= 0) {
    countdownActive = false;
    showTimesUp();
    Serial.println("⏰ Time's up!");
    sendToRaspberryPi("TIMER_FINISHED", "Countdown completed");
  } else {
    displayCountdown();
  }
}

void displayCountdown() {
  display1.clearDisplay();
  
  display1.setTextSize(1);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(35, 5);
  display1.println("COUNTDOWN");
  
  int minutes = countdownTime / 60;
  int seconds = countdownTime % 60;
  
  display1.setTextSize(3);
  display1.setCursor(15, 25);
  
  if (minutes < 10) display1.print("0");
  display1.print(minutes);
  display1.print(":");
  if (seconds < 10) display1.print("0");
  display1.print(seconds);
  
  int barWidth = map(countdownTime, 0, initialCountdownTime, 0, 120);
  display1.drawRect(4, 55, 120, 6, SSD1306_WHITE);
  display1.fillRect(4, 55, barWidth, 6, SSD1306_WHITE);
  
  // Show speed indicator based on X count
  if (xCount == 1) {
    // Show 1.25x speed indicator
    display1.setTextSize(1);
    display1.setCursor(95, 45);
    display1.print("1.3x");
  } else if (xCount >= 2) {
    // Show 2x speed indicator
    display1.setTextSize(1);
    display1.setCursor(100, 45);
    display1.print("2x");
  }
  
  if (countdownTime <= 10 && countdownTime > 0) {
    if ((millis() / 500) % 2) {
      display1.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT_TIMER, SSD1306_WHITE);
    }
  }
  
  display1.display();
}

void showTimesUp() {
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(20, 15);
  display1.println("TIME'S");
  display1.setCursor(35, 35);
  display1.println("OUT!");
  
  for (int i = 0; i < 3; i++) {
    display1.drawRect(i * 2, i * 2, SCREEN_WIDTH - i * 4, SCREEN_HEIGHT_TIMER - i * 4, SSD1306_WHITE);
  }
  display1.display();
  
  for (int i = 0; i < 6; i++) {
    delay(300);
    display1.invertDisplay(i % 2);
  }
  
  showWaitingMessage();
}

void stopCountdown() {
  if (countdownActive) {
    // Store remaining time before stopping
    pausedTimeRemaining = countdownTime;
    countdownActive = false;
    countdownPaused = true;  // Mark as paused, not stopped
    
    display1.clearDisplay();
    display1.setTextSize(2);
    display1.setTextColor(SSD1306_WHITE);
    display1.setCursor(15, 15);
    display1.println("PAUSED");
    
    // Show remaining time
    display1.setTextSize(1);
    display1.setCursor(25, 40);
    int mins = pausedTimeRemaining / 60;
    int secs = pausedTimeRemaining % 60;
    display1.printf("Time: %02d:%02d", mins, secs);
    
    display1.display();
    Serial.printf("⏸️ Countdown paused at %d seconds\n", pausedTimeRemaining);
    sendToRaspberryPi("TIMER_PAUSED", "Countdown paused at " + String(pausedTimeRemaining) + " seconds");
  }
}

void resumeCountdown() {
  if (countdownPaused && pausedTimeRemaining > 0) {
    countdownTime = pausedTimeRemaining;
    countdownActive = true;
    countdownPaused = false;
    lastUpdate = millis();
    updateTimerSpeed();  // Maintain current speed based on X count
    
    displayCountdown();
    
    Serial.printf("▶️ Countdown resumed: %d seconds remaining\n", countdownTime);
    Serial.printf("Timer speed: %dms interval (%d X's)\n", timerUpdateInterval, xCount);
    sendToRaspberryPi("TIMER_RESUMED", "Countdown resumed: " + String(countdownTime) + " seconds remaining");
  } else if (!countdownPaused) {
    Serial.println("⚠️ Timer is not paused - cannot resume");
  } else if (pausedTimeRemaining <= 0) {
    Serial.println("⚠️ No time remaining to resume");
  }
}

void updateXDisplay() {
  display2.clearDisplay();
  
  // Display only X marks, no count text
  display2.setTextSize(3);
  display2.setTextColor(SSD1306_WHITE);
  
  // Calculate spacing for X marks
  int xSpacing = 35;
  int startX = (SCREEN_WIDTH - (min(maxXCount, xCount) * xSpacing)) / 2;
  int yPos = 5;  // Centered vertically for 32-pixel display
  
  // Draw X marks
  for (int i = 0; i < xCount && i < maxXCount; i++) {
    int xPos = startX + (i * xSpacing);
    display2.setCursor(xPos, yPos);
    display2.print("X");
  }
  
  // Add warning border if approaching limit
  if (xCount >= maxXCount - 1 && xCount < maxXCount) {
    display2.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT_COUNTER, SSD1306_WHITE);
  }
  
  display2.display();
  
  Serial.printf("X Display Update: %d/%d X marks shown\n", xCount, maxXCount);
}

