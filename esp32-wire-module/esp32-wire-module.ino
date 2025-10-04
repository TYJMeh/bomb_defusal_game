#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 3000;  // Send heartbeat every 3 seconds

// WiFi settings
const char* ssid = "advaspire_2.4G";      // Replace with your WiFi SSID
const char* password = "0172037375"; // Replace with your WiFi password

// MQTT Broker settings
const char* mqtt_broker = "192.168.1.201"; // Replace with Raspberry Pi's IP
const int mqtt_port = 1883;
String clientId = "ESP32_Wire_" + String(random(0xffff), HEX); // Unique client ID
const char* topic_from_rpi = "rpi/to/esp";
const char* topic_to_rpi = "esp/to/rpi";

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Configuration
const int NUM_WIRES = 4;  // Number of wires to track
const int DEBOUNCE_DELAY = 50;  // Debounce delay in milliseconds
const int STATUS_LED = 2;  // Built-in LED pin

// Wire pin assignments (adjust based on your wiring)
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

// ===== PUZZLE CONFIGURATION SECTION =====
// Customize your puzzles here - easy to modify!

struct PuzzleStep {
  String instruction;      // What to display to the player
  int correct_wire;       // Which wire index (0-3) is correct
  String success_message; // Message when correct wire is cut
};

// Define your puzzle sequence here
PuzzleStep puzzle_sequence[] = {
  {
    "STEP 1: Cut the RED wire to begin",
    0,  // RED wire (index 0)
    "RED wire cut correctly! Next step..."
  }
};

const int TOTAL_STEPS = sizeof(puzzle_sequence) / sizeof(puzzle_sequence[0]);

// ===== END PUZZLE CONFIGURATION =====

// Game state variables
bool game_active = false;
bool game_completed = false;
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
  
  // Initialize WiFi and MQTT
  setupWiFi();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttCallback);
  
  // Initialize status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  
  // Initialize wire pins as INPUT_PULLUP
  // Wires should be connected between pin and GND
  // When wire is intact: LOW (connected to GND)
  // When wire is cut: HIGH (disconnected, pulled up by internal resistor)
  // Detection: LOW to HIGH transition = WIRE CUT
  for (int i = 0; i < NUM_WIRES; i++) {
    pinMode(WIRE_PINS[i], INPUT_PULLUP);
    
    // Initialize wire status
    wires[i].current_state = digitalRead(WIRE_PINS[i]);
    wires[i].previous_state = wires[i].current_state;
    wires[i].is_cut = wires[i].current_state;  // If HIGH at start, consider it cut
    wires[i].last_change_time = 0;
    wires[i].color = wire_colors[i];
  }
  
  start_time = millis();
  
  Serial.println("Wire Cutting Puzzle Ready!");
  Serial.printf("This puzzle has %d steps to complete.\n", TOTAL_STEPS);
  
  // Check and sync wire states with hardware
  checkWireStates();
  
  Serial.println("Commands: START, STATUS, JSON, STATS, RESET, CHECK, HELP");
  
  // Send initial status to MQTT
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
    
    String json_string;
    serializeJson(doc, json_string);
    client.publish(topic_to_rpi, json_string.c_str());
  }
}

void loop() {
  // Maintain MQTT connection
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  if (waiting == 0) {
    // Module is waiting for all modules to connect
    // Only handle MQTT and serial commands, no game logic
    handleSerialCommands();
  }
  else if (waiting == 1) {
    // All modules connected, normal operation
    updateWireStatus();
    updateStatusLED();
    handleSerialCommands();
  }

  // Send heartbeat every 3 seconds
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  delay(10);  // Small delay for stability
}

void updateWireStatus() {
  unsigned long current_time = millis();
  
  for (int i = 0; i < NUM_WIRES; i++) {
    // Read current pin state
    bool current_reading = digitalRead(WIRE_PINS[i]);
    
    // Check if state changed and enough time has passed (debouncing)
    if (current_reading != wires[i].previous_state && 
        (current_time - wires[i].last_change_time) > DEBOUNCE_DELAY) {
      
      wires[i].previous_state = wires[i].current_state;
      wires[i].current_state = current_reading;
      wires[i].last_change_time = current_time;
      
      // Detect wire cutting (LOW to HIGH transition)
      // Wire goes from connected (LOW) to not connected (HIGH) = CUT
      if (wires[i].previous_state == LOW && wires[i].current_state == HIGH) {
        if (!wires[i].is_cut) {
          wires[i].is_cut = true;
          total_cuts++;
          Serial.printf("🔪 WIRE CUT DETECTED: %s wire disconnected!\n", wires[i].color.c_str());
          onWireCut(i);
        }
      }
      
      // Detect wire reconnection (HIGH to LOW transition)
      // Wire goes from not connected (HIGH) to connected (LOW) = RECONNECTED
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
  
  // Check if game is active and this affects the puzzle
  if (game_active && !game_completed) {
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
    // Correct wire cut for this step!
    Serial.println("\n✅ CORRECT!");
    Serial.println(puzzle_sequence[current_step].success_message);
    
    current_step++;
    
    if (current_step >= TOTAL_STEPS) {
      // Puzzle completed!
      game_completed = true;
      game_active = false;
      games_completed++;
      
      Serial.println("\n🎉 PUZZLE FULLY COMPLETED! 🎉");
      Serial.println("All steps finished successfully!");
      
      // Send completion message to MQTT
      sendToRaspberryPi("PUZZLE_COMPLETED", "Wire cutting puzzle completed successfully!");
      
    } else {
      // Move to next step
      Serial.println("\n📋 NEXT STEP:");
      Serial.printf("Step %d/%d: %s\n", 
                    current_step + 1, 
                    TOTAL_STEPS, 
                    puzzle_sequence[current_step].instruction.c_str());
    }
    
  } else {
    // Wrong wire cut - send penalty to MQTT
    wrong_cuts++;
    Serial.println("\n❌ WRONG WIRE!");
    Serial.printf("You cut the %s wire, but step %d requires: %s\n",
                  wires[cut_wire_index].color.c_str(),
                  current_step + 1,
                  wires[expected_wire].color.c_str());
    Serial.println("Try again - the puzzle continues...\n");
    
    // Send wrong cut alert to MQTT
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
    
    // Show current step again
    Serial.printf("Current Step %d/%d: %s\n", 
                  current_step + 1, 
                  TOTAL_STEPS, 
                  puzzle_sequence[current_step].instruction.c_str());
  }
  
  sendGameStatusJSON();
}

void startNewPuzzle() {
  // Reset game state
  game_active = true;
  game_completed = false;
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
  
  // Send game start message to MQTT
  sendToRaspberryPi("GAME_STARTED", "Wire cutting puzzle started");
  
  sendGameStatusJSON();
}

void sendGameStatusJSON() {
  StaticJsonDocument<512> doc;
  doc["type"] = "GAME_STATUS";
  doc["game_active"] = game_active;
  doc["game_completed"] = game_completed;
  doc["current_step"] = current_step + 1;  // Display as 1-based
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
  
  // Reset game state
  game_active = false;
  game_completed = false;
  current_step = 0;
  
  Serial.println("Statistics and game state reset!");
}

void updateStatusLED() {
  if (game_completed) {
    // Completed: Fast celebration flashing
    digitalWrite(STATUS_LED, (millis() / 150) % 2);
  } else if (game_active) {
    // Active game: Double heartbeat pattern
    unsigned long cycle = millis() % 1500;
    bool led_on = (cycle < 100) || (cycle > 200 && cycle < 300);
    digitalWrite(STATUS_LED, led_on);
  } else {
    // Idle: Slow blink
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
      sendToRaspberryPi("WIRE_MODULE_CONNECTED", "Wire cutting module ready");
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
    String command = doc["command"];
    
    if (command == "START_GAME") {
      if (doc["reset"]) {
        resetStatistics();
      }
      startNewPuzzle();
    }
    else if (command == "STOP_GAME") {
      game_active = false;
      Serial.println("🛑 Game stopped by MQTT command");
      sendToRaspberryPi("GAME_STOPPED", "Wire game stopped by command");
    }
    else if (command == "RESET_GAME") {
      resetStatistics();
      Serial.println("🔄 Game reset by MQTT command");
      sendToRaspberryPi("GAME_RESET", "Wire game reset by command");
    }
    else if (command == "X") {
      // Deactivate wire game (penalty)
      game_active = false;
      Serial.println("❌ Wire game deactivated due to penalty");
      sendToRaspberryPi("WIRE_GAME_DEACTIVATED", "Wire game deactivated due to penalty");
    }
    else if (command == "CHECK_WIRES") {
      // Check wire states via MQTT
      checkWireStates();
      sendToRaspberryPi("WIRE_STATES_CHECKED", "Wire states checked and synchronized");
    }
    else if (command == "ACTIVATE") {
      // Activate module - all modules are connected
      waiting = 1;
      Serial.println("🚀 Module activated! All modules connected.");
      sendToRaspberryPi("MODULE_ACTIVATED", "Wire module activated and ready");
    }
    else {
      Serial.printf("Unknown JSON command: %s\n", command.c_str());
    }
  }
  else {
    // Simple text message format
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
    // Create JSON message
    StaticJsonDocument<512> doc;
    doc["type"] = message_type;
    doc["message"] = message_content;
    doc["timestamp"] = millis();
    doc["device"] = "ESP32_Wire";
    doc["game_active"] = game_active;
    doc["game_completed"] = game_completed;
    doc["current_step"] = current_step + 1;
    doc["total_steps"] = TOTAL_STEPS;
    doc["wrong_cuts"] = wrong_cuts;
    
    String json_string;
    serializeJson(doc, json_string);
    
    // Publish to MQTT
    client.publish(topic_to_rpi, json_string.c_str());
    
    Serial.printf("📡 SENT TO RPI: %s - %s\n", message_type.c_str(), message_content.c_str());
  } else {
    Serial.println("❌ MQTT not connected - message not sent");
  }
}

// ===== END MQTT FUNCTIONS =====

void checkWireStates() {
  Serial.println("\n🔍 Checking current wire states...");
  
  for (int i = 0; i < NUM_WIRES; i++) {
    // Read current pin state
    bool current_reading = digitalRead(WIRE_PINS[i]);
    
    // Update internal state to match hardware
    wires[i].current_state = current_reading;
    wires[i].previous_state = current_reading;
    wires[i].is_cut = current_reading;  // HIGH = cut, LOW = intact
    wires[i].last_change_time = millis();
    
    // Display status
    Serial.printf("Wire %d (%s, Pin %d): %s %s\n",
                  i + 1,
                  wires[i].color.c_str(),
                  WIRE_PINS[i],
                  current_reading ? "CUT" : "INTACT",
                  current_reading ? "(HIGH)" : "(LOW)");
  }
  
  // Count current cuts
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
  Serial.println("HELP       - Show this help message");
  Serial.println("\n=== GAME FLOW ===");
  Serial.println("1. Type START to begin the puzzle");
  Serial.println("2. Follow the steps in sequence (no random order)");
  Serial.println("3. Cut wrong wire? No problem - game continues!");
  Serial.println("4. Complete all steps to finish the puzzle");
  Serial.println("5. Modify puzzle_sequence[] in code to customize");
  Serial.println("===========================\n");
}