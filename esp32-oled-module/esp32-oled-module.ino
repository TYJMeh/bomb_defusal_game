#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi settings
const char* ssid = "advaspire_2.4G";      // Replace with your WiFi SSID
const char* password = "0172037375"; // Replace with your WiFi password

// MQTT Broker settings
const char* mqtt_broker = "192.168.1.201"; // Replace with Raspberry Pi's IP
const int mqtt_port = 1883;
String clientId = "ESP32_Display_" + String(random(0xffff), HEX); // Unique client ID
const char* topic_from_rpi = "rpi/to/display";
const char* topic_to_rpi = "display/to/rpi";

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT_TIMER 64    // Timer display (128x64)
#define SCREEN_HEIGHT_COUNTER 32  // X counter display (128x32)
#define OLED_RESET    -1

// I2C Bus 1 pins (Timer Display)
#define SDA1 21  // Default SDA
#define SCL1 22  // Default SCL

// I2C Bus 2 pins (X Counter Display) - YOU CAN CHANGE THESE
#define SDA2 25  // Alternative SDA pin
#define SCL2 26  // Alternative SCL pin

// Both displays can use the same address since they're on different buses
#define DISPLAY_ADDRESS 0x3C

// Create separate Wire instances
TwoWire I2C_1 = TwoWire(0);  // First I2C bus
TwoWire I2C_2 = TwoWire(1);  // Second I2C bus

// Initialize displays with their respective I2C buses and screen sizes
Adafruit_SSD1306 display1(SCREEN_WIDTH, SCREEN_HEIGHT_TIMER, &I2C_1, OLED_RESET);    // Timer display (128x64)
Adafruit_SSD1306 display2(SCREEN_WIDTH, SCREEN_HEIGHT_COUNTER, &I2C_2, OLED_RESET);  // X counter display (128x32)

// Global variables
int countdownTime = 0;
int initialCountdownTime = 0;
bool countdownActive = false;
unsigned long lastUpdate = 0;
int xCount = 0;
int maxXCount = 3;  // Maximum X marks allowed

void setup() {
  Serial.begin(115200);
  
  Serial.println("=== ESP32 Dual OLED Display with MQTT ===");
  Serial.println("Timer Display - I2C Bus 1 (SDA=21, SCL=22) - 128x64");
  Serial.println("X Counter Display - I2C Bus 2 (SDA=25, SCL=26) - 128x32");
  
  // Initialize WiFi and MQTT
  setupWiFi();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttCallback);
  
  // Initialize first I2C bus for timer display
  I2C_1.begin(SDA1, SCL1);
  if(!display1.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS)) {
    Serial.println("Display 1 (Timer) allocation failed");
    Serial.println("Check wiring: SDA1=21, SCL1=22");
    for(;;);
  }
  Serial.println("✓ Display 1 (Timer 128x64) initialized successfully");
  
  // Initialize second I2C bus for X counter display
  I2C_2.begin(SDA2, SCL2);
  if(!display2.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS)) {
    Serial.println("Display 2 (X Counter) allocation failed");
    Serial.println("Check wiring: SDA2=25, SCL2=26");
    for(;;);
  }
  Serial.println("✓ Display 2 (X Counter 128x32) initialized successfully");
  
  // Clear both displays
  display1.clearDisplay();
  display2.clearDisplay();
  
  // Show initial messages
  showInitialMessage();
  updateXDisplay();
  
  Serial.println("\nWiring Guide:");
  Serial.println("Display 1 (Timer 128x64):");
  Serial.println("  VCC → 3.3V, GND → GND, SDA → GPIO21, SCL → GPIO22");
  Serial.println("Display 2 (X Counter 128x32):");
  Serial.println("  VCC → 3.3V, GND → GND, SDA → GPIO25, SCL → GPIO26");
  
  Serial.println("\nCommands:");
  Serial.println("  'x' - Add X mark");
  Serial.println("  'reset' - Reset X counter");
  Serial.println("  'start 30' - Start 30-second timer");
  Serial.println("  'stop' - Stop timer");
  Serial.println("  'mqtt' - Show MQTT status");
  
  // Auto-start countdown after 3 seconds
  delay(3000);
  showWaitingMessage();
}

void loop() {
  // Maintain MQTT connection
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Check if countdown is active and update every second
  if (countdownActive && millis() - lastUpdate >= 1000) {
    updateCountdown();
    lastUpdate = millis();
  }
  
  // Check for serial input
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
    else if (input == "mqtt") {
      printMQTTStatus();
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
  
  // Process messages from Raspberry Pi
  processMQTTMessage(message);
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      // Subscribe to messages from Raspberry Pi
      client.subscribe(topic_from_rpi);
      Serial.printf("Subscribed to: %s\n", topic_from_rpi);
      
      // Send connection status
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
  // Try to parse as JSON first
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    // JSON message format
    String type = doc["type"];
    String content = doc["message"];
    
    if (type == "X" || type == "WRONG_WIRE" || type == "FAILURE") {
      addXMark(type);
    }
    else if (type == "RESET_X" || type == "NEW_GAME") {
      resetXCounter();
    }
    else if (type == "START_TIMER") {
      int duration = doc["duration"] | 60;  // Default 60 seconds
      startCountdown(duration);
    }
    else if (type == "STOP_TIMER") {
      stopCountdown();
    }
    else if (type == "TEST") {
      testDisplays();
    }
    else {
      Serial.printf("Unknown JSON message type: %s\n", type.c_str());
    }
  }
  else {
    // Simple text message format
    message.toUpperCase();
    
    if (message == "X") {
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
    else {
      Serial.printf("Unknown simple message: %s\n", message.c_str());
    }
  }
}

void sendToRaspberryPi(String message_type, String message_content) {
  if (client.connected()) {
    // Create JSON message
    StaticJsonDocument<256> doc;
    doc["type"] = message_type;
    doc["message"] = message_content;
    doc["timestamp"] = millis();
    doc["device"] = "ESP32_Display";
    doc["x_count"] = xCount;
    doc["max_x_count"] = maxXCount;
    
    String json_string;
    serializeJson(doc, json_string);
    
    // Publish to MQTT
    client.publish(topic_to_rpi, json_string.c_str());
    
    Serial.printf("📡 SENT TO RPI: %s - %s\n", message_type.c_str(), message_content.c_str());
  } else {
    Serial.println("❌ MQTT not connected - message not sent");
  }
}

void printMQTTStatus() {
  Serial.println("\n=== MQTT STATUS ===");
  Serial.printf("WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.printf("MQTT Broker: %s:%d\n", mqtt_broker, mqtt_port);
  Serial.printf("MQTT Status: %s\n", client.connected() ? "Connected" : "Disconnected");
  Serial.printf("Client ID: %s\n", clientId.c_str());
  Serial.printf("Subscribe Topic: %s\n", topic_from_rpi);
  Serial.printf("Publish Topic: %s\n", topic_to_rpi);
  Serial.println("===================\n");
}

// ===== DISPLAY FUNCTIONS =====

void addXMark(String source) {
  if (xCount < maxXCount) {
    xCount++;
    updateXDisplay();
    Serial.printf("❌ X added from %s! Total count: %d/%d\n", source.c_str(), xCount, maxXCount);
    
    // Send acknowledgment back to RPI
    sendToRaspberryPi("X_ADDED", "X mark added, total: " + String(xCount) + "/" + String(maxXCount));
    
    // Check if maximum reached
    if (xCount >= maxXCount) {
      Serial.println("🚨 MAXIMUM X COUNT REACHED!");
      showMaxXReached();
      sendToRaspberryPi("MAX_X_REACHED", "Maximum X count reached: " + String(maxXCount));
    }
  } else {
    Serial.printf("Maximum X count reached (%d)\n", maxXCount);
    sendToRaspberryPi("X_LIMIT_REACHED", "Cannot add X - limit reached: " + String(maxXCount));
  }
}

void resetXCounter() {
  xCount = 0;
  updateXDisplay();
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
  
  // Add warning border
  display2.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT_COUNTER, SSD1306_WHITE);
  display2.drawRect(1, 1, SCREEN_WIDTH-2, SCREEN_HEIGHT_COUNTER-2, SSD1306_WHITE);
  
  display2.display();
  
  // Blink effect
  for (int i = 0; i < 4; i++) {
    delay(500);
    display2.invertDisplay(i % 2);
  }
  
  // Return to normal X display after warning
  delay(1000);
  updateXDisplay();
}

void testDisplays() {
  Serial.println("Testing both displays...");
  
  // Test Display 1 (128x64)
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(20, 20);
  display1.println("TIMER");
  display1.setCursor(15, 40);
  display1.println("128x64");
  display1.display();
  
  // Test Display 2 (128x32)
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

void startCountdown(int seconds) {
  if (seconds <= 0) return;
  
  countdownTime = seconds;
  initialCountdownTime = seconds;
  countdownActive = true;
  lastUpdate = millis();
  
  displayCountdown();
  
  Serial.printf("⏰ Countdown started: %d seconds\n", seconds);
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
  
  // Title
  display1.setTextSize(1);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(35, 5);
  display1.println("COUNTDOWN");
  
  // Calculate minutes and seconds
  int minutes = countdownTime / 60;
  int seconds = countdownTime % 60;
  
  // Display large time
  display1.setTextSize(3);
  display1.setCursor(15, 25);
  
  if (minutes < 10) display1.print("0");
  display1.print(minutes);
  display1.print(":");
  if (seconds < 10) display1.print("0");
  display1.print(seconds);
  
  // Progress bar
  int barWidth = map(countdownTime, 0, initialCountdownTime, 0, 120);
  display1.drawRect(4, 55, 120, 6, SSD1306_WHITE);
  display1.fillRect(4, 55, barWidth, 6, SSD1306_WHITE);
  
  // Warning for last 10 seconds
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
  
  // Blinking effect
  for (int i = 0; i < 6; i++) {
    delay(300);
    display1.invertDisplay(i % 2);
  }
  
  // Return to waiting state
  showWaitingMessage();
}

void stopCountdown() {
  countdownActive = false;
  display1.clearDisplay();
  display1.setTextSize(2);
  display1.setTextColor(SSD1306_WHITE);
  display1.setCursor(15, 20);
  display1.println("STOPPED");
  display1.setTextSize(1);
  display1.setCursor(35, 45);
  display1.println("Timer paused");
  display1.display();
  Serial.println("⏰ Countdown stopped");
  sendToRaspberryPi("TIMER_STOPPED", "Countdown stopped by command");
}

void updateXDisplay() {
  display2.clearDisplay();
  
  // For 128x32 display, we need to use smaller text and different positioning
  display2.setTextSize(3);  // Reduced from 4 to fit in 32 pixels height
  display2.setTextColor(SSD1306_WHITE);
  
  // Calculate spacing based on max count
  int availableWidth = 110;  // Leave some margin
  int xSpacing = min(35, availableWidth / max(1, maxXCount));
  int startX = (SCREEN_WIDTH - (min(maxXCount, xCount) * xSpacing)) / 2;
  int startY = 2;    // Much higher up for 32-pixel height
  
  // Display X marks horizontally
  for (int i = 0; i < xCount && i < maxXCount; i++) {
    int xPos = startX + (i * xSpacing);
    display2.setCursor(xPos, startY);
    display2.print("X");
  }
  
  // Show count at bottom (adjusted for 32-pixel height)
  display2.setTextSize(1);
  display2.setCursor(50, 24);  // Position near bottom of 32-pixel display
  display2.print(xCount);
  display2.print("/");
  display2.print(maxXCount);
  
  // Add warning if approaching limit
  if (xCount >= maxXCount - 1 && xCount < maxXCount) {
    display2.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT_COUNTER, SSD1306_WHITE);
  }
  
  display2.display();
}