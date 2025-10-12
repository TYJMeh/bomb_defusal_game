#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 3000;

// WiFi settings
const char* ssid = "advaspire_2.4G";
const char* password = "0172037375";

// MQTT Broker settings
const char* mqtt_broker = "192.168.1.201";
const int mqtt_port = 1883;
String clientId = "ESP32_Wire_" + String(random(0xffff), HEX);
const char* topic_from_rpi = "rpi/to/esp";
const char* topic_to_rpi = "esp/to/rpi";

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Configuration
const int NUM_WIRES = 4;
const int DEBOUNCE_DELAY = 50;
const int STATUS_LED = 2;

// Wire pin assignments
const int WIRE_PINS[NUM_WIRES] = {5, 18, 19, 23};

// Wire status tracking
struct WireStatus {
  bool current_state;
  bool previous_state;
  bool is_cut;
  unsigned long last_change_time;
  String color;
};

WireStatus wires[NUM_WIRES];

// Wire colors for identification
String wire_colors[NUM_WIRES] = {
  "RED", "BLUE", "GREEN", "YELLOW"
};

// Puzzle configuration
struct PuzzleStep {
  String instruction;
  int correct_wire;
  String success_message;
};

PuzzleStep puzzle_sequence[] = {
  {
    "STEP 1: Cut the RED wire to begin",
    0,  // RED wire (index 0)
    "RED wire cut correctly! Next step..."
  }
};

const int TOTAL_STEPS = sizeof(puzzle_sequence) / sizeof(puzzle_sequence[0]);

// Game state variables
bool game_active = false;
bool game_completed = false;
bool game_paused = false;
int current_step = 0;
unsigned long game_start_time = 0;

// Statistics
int total_cuts = 0;
int total_reconnections = 0;
int games_completed = 0;
int wrong_cuts = 0;
unsigned long start_time = 0;
int waiting = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Wire Cutting Puzzle Game - MQTT Enabled");
  
  setupWiFi();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttCallback);
  
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  
  for (int i = 0; i < NUM_WIRES; i++) {
    pinMode(WIRE_PINS[i], INPUT_PULLUP);
    
    wires[i].current_state = digitalRead(WIRE_PINS[i]);
    wires[i].previous_state = wires[i].current_state;
    wires[i].is_cut = wires[i].current_state;
    wires[i].last_change_time = 0;
    wires[i].color = wire_colors[i];
  }
  
  start_time = millis();
  
  Serial.println("Wire Cutting Puzzle Ready!");
  Serial.printf("This puzzle has %d steps to complete.\n", TOTAL_STEPS);
  
  checkWireStates();
  
  Serial.println("Commands: START, STATUS, JSON, STATS, RESET, CHECK, HELP");
  
  sendGameStatusJSON();
}

void sendHeartbeat() {
  if (client.connected() && waiting == 1) {
    StaticJsonDocument<128> doc;
    doc["type"] = "HEARTBEAT";
    doc["device"] = "ESP32_Wire";
    doc["timestamp"] = millis();
    doc["game_active"] = game_active;
    doc["game_completed"] = game_completed;
    doc["game_paused"] = game_paused;
    
    String json_string;
    serializeJson(doc, json_string);
    client.publish(topic_to_rpi, json_string.c_str());
  }
}

void loop() {
  // Check WiFi connection
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
  
  // Maintain MQTT connection
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Send heartbeat every 3 seconds
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  
  if (waiting == 0) {
    // Module is waiting for activation
    handleSerialCommands();
  }
  else if (waiting == 1) {
    // All modules connected, normal operation
    // Only process wire status if game is active and not paused
    if (game_active && !game_paused) {
      updateWireStatus();
    }
    updateStatusLED();
    handleSerialCommands();
  }

  delay(10);
}

void updateWireStatus() {
  unsigned long current_time = millis();
  
  for (int i = 0; i < NUM_WIRES; i++) {
    bool current_reading = digitalRead(WIRE_PINS[i]);
    
    if (current_reading != wires[i].previous_state && 
        (current_time - wires[i].last_change_time) > DEBOUNCE_DELAY) {
      
      wires[i].previous_state = wires[i].current_state;
      wires[i].current_state = current_reading;
      wires[i].last_change_time = current_time;
      
      // Detect wire cutting (LOW to HIGH transition)
      if (wires[i].previous_state == LOW && wires[i].current_state == HIGH) {
        if (!wires[i].is_cut) {
          wires[i].is_cut = true;
          total_cuts++;
          Serial.printf("🔪 WIRE CUT DETECTED: %s wire disconnected!\n", wires[i].color.c_str());
          onWireCut(i);
        }
      }
      // Detect wire reconnection (HIGH to LOW transition)
      else if (wires[i].previous_state == HIGH && wires[i].current_state == LOW) {
        if (wires[i].is_cut) {
          wires[i].is_cut = false;
          total_reconnections++;
          Serial.printf("🔌 WIRE RECONNECTED: %s wire reconnected!\n", wires[i].color.c_str());
          onWireReconnected(i);
        }
      }
    } else {
      wires[i].previous_state = current_reading;
    }
  }
}

void onWireCut(int wire_index) {
  unsigned long timestamp = millis();
  Serial.printf("[%lu ms] WIRE CUT: %s (Pin %d)\n", 
                timestamp,
                wires[wire_index].color.c_str(), 
                WIRE_PINS[wire_index]);
  
  if (game_active && !game_completed && !game_paused) {
    checkPuzzleStep(wire_index);
  }
}

void onWireReconnected(int wire_index) {
  unsigned long timestamp = millis();
  Serial.printf("[%lu ms] WIRE RECONNECTED: %s (Pin %d)\n", 
                timestamp,
                wires[wire_index].color.c_str(), 
                WIRE_PINS[wire_index]);
}

void checkPuzzleStep(int cut_wire_index) {
  int expected_wire = puzzle_sequence[current_step].correct_wire;
  
  if (cut_wire_index == expected_wire) {
    Serial.println("\n✅ CORRECT!");
    Serial.println(puzzle_sequence[current_step].success_message);
    
    current_step++;
    
    if (current_step >= TOTAL_STEPS) {
      game_completed = true;
      game_active = false;
      games_completed++;
      
      Serial.println("\n🎉 PUZZLE FULLY COMPLETED! 🎉");
      Serial.println("All steps finished successfully!");
      
      sendToRaspberryPi("PUZZLE_COMPLETED", "Wire cutting puzzle completed successfully!");
      
    } else {
      Serial.println("\n📋 NEXT STEP:");
      Serial.printf("Step %d/%d: %s\n", 
                    current_step + 1, 
                    TOTAL_STEPS, 
                    puzzle_sequence[current_step].instruction.c_str());
    }
    
  } else {
    wrong_cuts++;
    Serial.println("\n❌ WRONG WIRE!");
    Serial.printf("You cut the %s wire, but step %d requires: %s\n",
                  wires[cut_wire_index].color.c_str(),
                  current_step + 1,
                  wires[expected_wire].color.c_str());
    Serial.println("Try again - the puzzle continues...\n");
    
    StaticJsonDocument<256> doc;
    doc["type"] = "WRONG_CUT_ALERT";
    doc["wrong_wire_cut"] = wires[cut_wire_index].color;
    doc["expected_wire"] = wires[expected_wire].color;
    doc["current_step"] = current_step + 1;
    doc["total_steps"] = TOTAL_STEPS;
    doc["timestamp"] = millis();
    
    String json_string;
    serializeJson(doc, json_string);
    client.publish(topic_to_rpi, json_string.c_str());
    
    Serial.printf("📡 SENT WRONG CUT ALERT TO RPI\n");
    
    Serial.printf("Current Step %d/%d: %s\n", 
                  current_step + 1, 
                  TOTAL_STEPS, 
                  puzzle_sequence[current_step].instruction.c_str());
  }
  
  sendGameStatusJSON();
}

void startNewPuzzle() {
  game_active = true;
  game_completed = false;
  game_paused = false;
  current_step = 0;
  game_start_time = millis();
  
  Serial.println("\n🚨 NEW PUZZLE STARTED! 🚨");
  Serial.printf("This puzzle has %d steps to complete in sequence.\n", TOTAL_STEPS);
  Serial.println("\nWire Layout:");
  for (int i = 0; i < NUM_WIRES; i++) {
    Serial.printf("  Wire %d: %s (Pin %d)\n", i + 1, wire_colors[i].c_str(), WIRE_PINS[i]);
  }
  
  Serial.println("\n📋 STARTING WITH STEP 1:");
  Serial.printf("Step 1/%d: %s\n", TOTAL_STEPS, puzzle_sequence[0].instruction.c_str());
  
  sendToRaspberryPi("GAME_STARTED", "Wire cutting puzzle started");
  sendGameStatusJSON();
}

void sendGameStatusJSON() {
  StaticJsonDocument<512> doc;
  doc["type"] = "GAME_STATUS";
  doc["game_active"] = game_active;
  doc["game_completed"] = game_completed;
  doc["game_paused"] = game_paused;
  doc["current_step"] = current_step + 1;
  doc["total_steps"] = TOTAL_STEPS;
  doc["games_completed"] = games_completed;
  doc["wrong_cuts"] = wrong_cuts;
  
  if (game_active && current_step < TOTAL_STEPS) {
    doc["current_instruction"] = puzzle_sequence[current_step].instruction;
    doc["required_wire_color"] = wire_colors[puzzle_sequence[current_step].correct_wire];
  }
  
  doc["timestamp"] = millis();
  
  Serial.print("GAME_STATUS: ");
  serializeJson(doc, Serial);
  Serial.println();
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toUpperCase();
    
    if (command == "START") {
      if (!game_active) {
        startNewPuzzle();
      } else {
        Serial.println("Game already active!");
        printCurrentStep();
      }
    }
    else if (command == "STATUS") {
      printWireStatus();
      if (game_active) {
        printCurrentStep();
      }
    }
    else if (command == "JSON") {
      sendWireStatusJSON();
    }
    else if (command == "STATS") {
      printStatistics();
    }
    else if (command == "RESET") {
      resetStatistics();
    }
    else if (command == "HELP") {
      printHelp();
    }
    else if (command == "STEPS") {
      printAllSteps();
    }
    else if (command == "CHECK") {
      checkWireStates();
    }
    else if (command == "PAUSE") {
      game_paused = true;
      Serial.println("⏸️ Wire game paused");
    }
    else if (command == "RESUME") {
      game_paused = false;
      Serial.println("▶️ Wire game resumed");
    }
    else {
      Serial.println("Unknown command. Type HELP for available commands.");
    }
  }
}

void printCurrentStep() {
  if (game_active && current_step < TOTAL_STEPS) {
    Serial.printf("\n📋 CURRENT STEP %d/%d:\n", current_step + 1, TOTAL_STEPS);
    Serial.println(puzzle_sequence[current_step].instruction);
    Serial.printf("Required wire: %s\n", wire_colors[puzzle_sequence[current_step].correct_wire].c_str());
  } else if (game_completed) {
    Serial.println("\n🎉 Puzzle completed!");
  } else {
    Serial.println("\nNo active game. Type START to begin.");
  }
}

void printAllSteps() {
  Serial.println("\n=== PUZZLE SEQUENCE ===");
  for (int i = 0; i < TOTAL_STEPS; i++) {
    Serial.printf("Step %d: %s\n", i + 1, puzzle_sequence[i].instruction.c_str());
    Serial.printf("        (Requires %s wire)\n", wire_colors[puzzle_sequence[i].correct_wire].c_str());
  }
  Serial.println("=======================\n");
}

void printWireStatus() {
  Serial.println("\n=== CURRENT WIRE STATUS ===");
  for (int i = 0; i < NUM_WIRES; i++) {
    Serial.printf("Wire %d (%s, Pin %d): %s\n",
                  i + 1,
                  wires[i].color.c_str(),
                  WIRE_PINS[i],
                  wires[i].is_cut ? "CUT" : "INTACT");
  }
  
  int cut_count = 0;
  for (int i = 0; i < NUM_WIRES; i++) {
    if (wires[i].is_cut) cut_count++;
  }
  
  Serial.printf("Summary: %d/%d wires cut\n", cut_count, NUM_WIRES);
  Serial.println("===========================\n");
}

void sendWireStatusJSON() {
  StaticJsonDocument<512> doc;
  JsonArray wire_array = doc.createNestedArray("wires");
  
  int cut_count = 0;
  for (int i = 0; i < NUM_WIRES; i++) {
    JsonObject wire = wire_array.createNestedObject();
    wire["id"] = i;
    wire["pin"] = WIRE_PINS[i];
    wire["color"] = wires[i].color;
    wire["is_cut"] = wires[i].is_cut;
    wire["connected"] = wires[i].current_state;
    
    if (wires[i].is_cut) cut_count++;
  }
  
  doc["total_wires"] = NUM_WIRES;
  doc["wires_cut"] = cut_count;
  doc["uptime_ms"] = millis() - start_time;
  doc["timestamp"] = millis();
  
  Serial.print("WIRE_STATUS: ");
  serializeJson(doc, Serial);
  Serial.println();
}

void printStatistics() {
  Serial.println("\n=== STATISTICS ===");
  Serial.printf("Uptime: %lu ms (%.1f seconds)\n", 
                millis() - start_time, 
                (millis() - start_time) / 1000.0);
  Serial.printf("Total wire cuts: %d\n", total_cuts);
  Serial.printf("Total reconnections: %d\n", total_reconnections);
  Serial.printf("Puzzles completed: %d\n", games_completed);
  Serial.printf("Wrong cuts: %d\n", wrong_cuts);
  
  if (total_cuts > 0) {
    float accuracy = (float)(total_cuts - wrong_cuts) / total_cuts * 100.0;
    Serial.printf("Cut accuracy: %.1f%%\n", accuracy);
  }
  
  int current_cut_count = 0;
  for (int i = 0; i < NUM_WIRES; i++) {
    if (wires[i].is_cut) current_cut_count++;
  }
  Serial.printf("Currently cut wires: %d/%d\n", current_cut_count, NUM_WIRES);
  Serial.println("==================\n");
}

void resetStatistics() {
  total_cuts = 0;
  total_reconnections = 0;
  games_completed = 0;
  wrong_cuts = 0;
  start_time = millis();
  
  game_active = false;
  game_completed = false;
  game_paused = false;
  current_step = 0;
  
  Serial.println("Statistics and game state reset!");
}

void updateStatusLED() {
  if (game_completed) {
    digitalWrite(STATUS_LED, (millis() / 150) % 2);
  } else if (game_active && !game_paused) {
    unsigned long cycle = millis() % 1500;
    bool led_on = (cycle < 100) || (cycle > 200 && cycle < 300);
    digitalWrite(STATUS_LED, led_on);
  } else if (game_paused) {
    // Slow blink when paused
    digitalWrite(STATUS_LED, (millis() / 1000) % 2);
  } else {
    digitalWrite(STATUS_LED, (millis() / 2000) % 2);
  }
}

// ===== MQTT FUNCTIONS =====

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
      
      sendToRaspberryPi("WIRE_MODULE_CONNECTED", "Wire cutting module ready");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

void processMQTTMessage(String message) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    String type = doc["type"];
    String command = doc["command"];
    
    Serial.printf("Received command: type='%s', command='%s'\n", type.c_str(), command.c_str());
    
    if (command == "START_GAME" || type == "START_GAME") {
      if (doc["reset"]) {
        resetStatistics();
      }
      startNewPuzzle();
    }
    else if (command == "STOP_GAME" || type == "STOP_GAME") {
      game_active = false;
      Serial.println("🛑 Game stopped by MQTT command");
      sendToRaspberryPi("GAME_STOPPED", "Wire game stopped by command");
    }
    else if (command == "PAUSE_TIMER" || type == "PAUSE_TIMER") {
      game_paused = true;
      Serial.println("⏸️ Wire game paused by MQTT");
      sendToRaspberryPi("WIRE_GAME_PAUSED", "Wire game paused");
    }
    else if (command == "RESUME_TIMER" || type == "RESUME_TIMER") {
      game_paused = false;
      Serial.println("▶️ Wire game resumed by MQTT");
      sendToRaspberryPi("WIRE_GAME_RESUMED", "Wire game resumed");
    }
    else if (command == "RESET_GAME" || type == "RESET_GAME") {
      resetStatistics();
      Serial.println("🔄 Game reset by MQTT command");
      sendToRaspberryPi("GAME_RESET", "Wire game reset by command");
    }
    else if (command == "ACTIVATE" || type == "ACTIVATE") {
      waiting = 1;
      Serial.println("🚀 Wire module activated! All modules connected.");
      sendToRaspberryPi("MODULE_ACTIVATED", "Wire module activated and ready");
      // Auto-start the game when activated
      startNewPuzzle();
    }
    else {
      Serial.printf("Unknown JSON command: type='%s', command='%s'\n", type.c_str(), command.c_str());
    }
  }
  else {
    message.toUpperCase();
    
    if (message == "START") {
      startNewPuzzle();
    }
    else if (message == "STOP") {
      game_active = false;
      Serial.println("🛑 Game stopped by MQTT command");
    }
    else if (message == "RESET") {
      resetStatistics();
      Serial.println("🔄 Game reset by MQTT command");
    }
    else {
      Serial.printf("Unknown simple message: %s\n", message.c_str());
    }
  }
}

void sendToRaspberryPi(String message_type, String message_content) {
  if (client.connected()) {
    StaticJsonDocument<512> doc;
    doc["type"] = message_type;
    doc["message"] = message_content;
    doc["timestamp"] = millis();
    doc["device"] = "ESP32_Wire";
    doc["game_active"] = game_active;
    doc["game_completed"] = game_completed;
    doc["game_paused"] = game_paused;
    doc["current_step"] = current_step + 1;
    doc["total_steps"] = TOTAL_STEPS;
    doc["wrong_cuts"] = wrong_cuts;
    
    String json_string;
    serializeJson(doc, json_string);
    
    client.publish(topic_to_rpi, json_string.c_str());
    
    Serial.printf("📡 SENT TO RPI: %s - %s\n", message_type.c_str(), message_content.c_str());
  } else {
    Serial.println("❌ MQTT not connected - message not sent");
  }
}

void checkWireStates() {
  Serial.println("\n🔍 Checking current wire states...");
  
  for (int i = 0; i < NUM_WIRES; i++) {
    bool current_reading = digitalRead(WIRE_PINS[i]);
    
    wires[i].current_state = current_reading;
    wires[i].previous_state = current_reading;
    wires[i].is_cut = current_reading;
    wires[i].last_change_time = millis();
    
    Serial.printf("Wire %d (%s, Pin %d): %s %s\n",
                  i + 1,
                  wires[i].color.c_str(),
                  WIRE_PINS[i],
                  current_reading ? "CUT" : "INTACT",
                  current_reading ? "(HIGH)" : "(LOW)");
  }
  
  int cut_count = 0;
  for (int i = 0; i < NUM_WIRES; i++) {
    if (wires[i].is_cut) cut_count++;
  }
  
  Serial.printf("Summary: %d/%d wires currently cut\n", cut_count, NUM_WIRES);
  Serial.println("Wire states synchronized with hardware!\n");
}

void printHelp() {
  Serial.println("\n=== AVAILABLE COMMANDS ===");
  Serial.println("START      - Begin the puzzle sequence");
  Serial.println("STATUS     - Show current wire status and game progress");
  Serial.println("JSON       - Output wire status as JSON");
  Serial.println("STATS      - Show game statistics");
  Serial.println("STEPS      - Show the complete puzzle sequence");
  Serial.println("RESET      - Reset statistics and game state");
  Serial.println("CHECK      - Check and sync wire states with hardware");
  Serial.println("PAUSE      - Pause the game");
  Serial.println("RESUME     - Resume the game");
  Serial.println("HELP       - Show this help message");
  Serial.println("\n=== GAME FLOW ===");
  Serial.println("1. Type START to begin the puzzle");
  Serial.println("2. Follow the steps in sequence (no random order)");
  Serial.println("3. Cut wrong wire? No problem - game continues!");
  Serial.println("4. Complete all steps to finish the puzzle");
  Serial.println("5. Modify puzzle_sequence[] in code to customize");
  Serial.println("===========================\n");
}