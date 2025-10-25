#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 3000;

const char* ssid = "advaspire_2.4G";
const char* password = "0172037375";
const char* mqtt_server = "192.168.1.201";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

const char* subscribe_topic = "rpi/to/esp3";
const char* publish_topic = "esp3/to/rpi";

#define JOY_X_PIN 36
#define JOY_Y_PIN 33
#define JOY_BUTTON_PIN 25

#define MAZE_WIDTH 16
#define MAZE_HEIGHT 8
#define CELL_SIZE 8

// Configurable maze settings (loaded from JSON)
String currentMazeId = "maze_1";
String mazeName = "Default Maze";
int startX = 1;
int startY = 1;
int endX = 14;
int endY = 6;
int checkpoint1X = 4;
int checkpoint1Y = 3;
int checkpoint2X = 9;
int checkpoint2Y = 3;

// Player position
int playerX = 1;
int playerY = 1;

// Default maze layout (will be overwritten by config)
int maze[MAZE_HEIGHT][MAZE_WIDTH] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1},
  {1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,1},
  {1,0,1,0,0,0,0,0,0,0,1,0,0,0,1,1},
  {1,0,1,1,1,1,1,0,1,0,1,1,1,0,0,1},
  {1,0,0,0,0,0,0,0,1,0,0,0,1,0,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// Game state
bool gameWon = false;
bool gameActive = true;
bool gamePaused = false;
unsigned long lastMoveTime = 0;
const unsigned long moveDelay = 200;
int waiting = 0;
bool configReceived = false;

void setup() {
  Serial.begin(115200);
  
  // Increase MQTT buffer size to handle large maze configurations
  client.setBufferSize(2048);
  
  setupWiFi();
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(onMqttMessage);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  pinMode(JOY_BUTTON_PIN, INPUT_PULLUP);
  
  display.clearDisplay();
  display.display();
  
  Serial.println("=== Maze Game Started ===");
  Serial.println("Waiting for configuration from Raspberry Pi...");
}

void sendHeartbeat() {
  if (client.connected() && waiting == 1) {
    StaticJsonDocument<256> doc;
    doc["type"] = "HEARTBEAT";
    doc["device"] = "ESP32_Maze";
    doc["timestamp"] = millis();
    doc["game_active"] = gameActive;
    doc["game_won"] = gameWon;
    doc["game_paused"] = gamePaused;
    doc["maze_id"] = currentMazeId;
    doc["config_received"] = configReceived;
    
    String json_string;
    serializeJson(doc, json_string);
    client.publish(publish_topic, json_string.c_str());
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
    reconnectMQTT();
  }
  client.loop();
  
  // Send heartbeat
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  
  if (waiting == 0) {
    displayWaitingMessage();
  }
  else if (waiting == 1) {
    if (!gameWon && gameActive && !gamePaused) {
      handleInput();
      drawGame();
    } else if (gameWon) {
      displayWinMessage();
    } else if (gamePaused) {
      displayPausedMessage();
    } else {
      displayWaitingMessage();
    }
  }

  delay(50);
}

void handleInput() {
  if (millis() - lastMoveTime < moveDelay) {
    return;
  }
  
  int xValue = analogRead(JOY_X_PIN);
  int yValue = analogRead(JOY_Y_PIN);
  
  int newX = playerX;
  int newY = playerY;

  int xOffset = abs(xValue - 2048);
  int yOffset = abs(yValue - 2048);
  
  if (xOffset > yOffset && xOffset > 1000) {
    if (xValue < 1000) {
      newX++;
    } else if (xValue > 3000) {
      newX--;
    }
  } else if (yOffset > 1000) {
    if (yValue < 1000) {
      newY++;
    } else if (yValue > 3000) {
      newY--;
    }
  }
  
  if (newX != playerX || newY != playerY) {
    if (newX >= 0 && newX < MAZE_WIDTH && newY >= 0 && newY < MAZE_HEIGHT) {
      if (maze[newY][newX] == 1) {
        // Hit a wall - reset to start position
        playerX = startX;
        playerY = startY;
        Serial.println("💥 Hit wall! Returning to start.");
        
        StaticJsonDocument<200> doc;
        doc["type"] = "WALL_HIT";
        doc["message"] = "Player hit wall and returned to start";
        doc["device"] = "ESP32_Maze";
        doc["maze_id"] = currentMazeId;
        doc["position_x"] = newX;
        doc["position_y"] = newY;
        doc["timestamp"] = millis();
        
        String jsonString;
        serializeJson(doc, jsonString);
        client.publish(publish_topic, jsonString.c_str());
        
      } else {
        // Valid move
        playerX = newX;
        playerY = newY;
        
        // Check if reached the end
        if (playerX == endX && playerY == endY) {
          gameWon = true;
          Serial.println("🎉 You Win!");
          
          StaticJsonDocument<256> doc;
          doc["type"] = "MAZE_COMPLETED";
          doc["message"] = "Player completed the maze!";
          doc["maze_id"] = currentMazeId;
          doc["maze_name"] = mazeName;
          doc["time"] = millis();
          doc["device"] = "ESP32_Maze";
          doc["timestamp"] = millis();
          
          String jsonString;
          serializeJson(doc, jsonString);
          client.publish(publish_topic, jsonString.c_str());
        }
      }
      
      lastMoveTime = millis();
    }
  }
  
  // Check button press to restart game
  if (digitalRead(JOY_BUTTON_PIN) == LOW) {
    if (gameWon) {
      gameWon = false;
      playerX = startX;
      playerY = startY;
      
      StaticJsonDocument<200> doc;
      doc["type"] = "GAME_RESTART";
      doc["message"] = "Maze game restarted";
      doc["device"] = "ESP32_Maze";
      doc["maze_id"] = currentMazeId;
      doc["timestamp"] = millis();
      
      String jsonString;
      serializeJson(doc, jsonString);
      client.publish(publish_topic, jsonString.c_str());
      delay(300);
    }
  }
}

void drawGame() {
  display.clearDisplay();

  // Draw subtle dotted grid lines
  for (int x = 1; x < MAZE_WIDTH; x++) {
    for (int y = 0; y < MAZE_HEIGHT * CELL_SIZE; y += 2) {
      display.drawPixel(x * CELL_SIZE, y, SSD1306_WHITE);
    }
  }
  for (int y = 1; y < MAZE_HEIGHT; y++) {
    for (int x = 0; x < MAZE_WIDTH * CELL_SIZE; x += 2) {
      display.drawPixel(x, y * CELL_SIZE, SSD1306_WHITE);
    }
  }

  // Draw checkpoint 1
  int cp1X = checkpoint1X * CELL_SIZE + CELL_SIZE/2;
  int cp1Y = checkpoint1Y * CELL_SIZE + CELL_SIZE/2;
  display.drawCircle(cp1X, cp1Y, 3, SSD1306_WHITE);

  // Draw checkpoint 2
  int cp2X = checkpoint2X * CELL_SIZE + CELL_SIZE/2;
  int cp2Y = checkpoint2Y * CELL_SIZE + CELL_SIZE/2;
  display.drawCircle(cp2X, cp2Y, 3, SSD1306_WHITE);
  
  // Draw player (filled dot)
  int playerPixelX = playerX * CELL_SIZE + CELL_SIZE/2;
  int playerPixelY = playerY * CELL_SIZE + CELL_SIZE/2;
  display.fillCircle(playerPixelX, playerPixelY, 2, SSD1306_WHITE);
  
  display.display();
}

void displayWinMessage() {
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 15);
  display.println("You Win!");
  
  display.setTextSize(1);
  display.setCursor(5, 40);
  display.println(mazeName.c_str());
  display.setCursor(5, 52);
  display.println("Press button to restart");
  
  display.display();
}

void displayWaitingMessage() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 20);
  display.println("Game Inactive");
  display.setCursor(10, 35);
  
  if (configReceived) {
    display.println("Waiting for start...");
  } else {
    display.println("Loading config...");
  }
  
  display.display();
}

void displayPausedMessage() {
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 15);
  display.println("PAUSED");
  
  display.setTextSize(1);
  display.setCursor(10, 40);
  display.println("Module disconnected");
  
  // Blinking border
  if ((millis() / 500) % 2) {
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  }
  
  display.display();
}

void setupWiFi() {
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
  Serial.println("✓ WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (client.connect("ESP32_Maze_Game")) {
      Serial.println("connected");
      client.subscribe(subscribe_topic);
      Serial.printf("Subscribed to: %s\n", subscribe_topic);
      
      // Send connection status
      sendConnectionStatus();
      
      // Wait a moment for connection to stabilize
      delay(500);
      
      // Request maze configuration
      requestMazeConfig();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

void sendConnectionStatus() {
  StaticJsonDocument<256> doc;
  doc["type"] = "MAZE_MODULE_CONNECTED";
  doc["message"] = "Maze navigation module ready";
  doc["device"] = "ESP32_Maze";
  doc["maze_id"] = currentMazeId;
  doc["config_received"] = configReceived;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(publish_topic, jsonString.c_str());
  Serial.println("✓ Sent connection status to Raspberry Pi");
}

void requestMazeConfig() {
  Serial.println("\n📨 Requesting maze configuration from Raspberry Pi...");
  
  StaticJsonDocument<128> doc;
  doc["type"] = "REQUEST_MAZE_CONFIG";
  doc["message"] = "Requesting maze configuration";
  doc["device"] = "ESP32_Maze";
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(publish_topic, jsonString.c_str());
  
  Serial.println("✓ Configuration request sent");
}

void applyMazeConfig(JsonDocument& doc) {
  Serial.println("\n==================================================");
  Serial.println("🔧 APPLYING MAZE CONFIGURATION");
  Serial.println("==================================================");
  
  // Extract configuration values
  currentMazeId = doc["maze_id"] | "maze_1";
  mazeName = doc["name"] | "Default Maze";
  startX = doc["start_x"] | 1;
  startY = doc["start_y"] | 1;
  endX = doc["end_x"] | 14;
  endY = doc["end_y"] | 6;
  checkpoint1X = doc["checkpoint_1_x"] | 4;
  checkpoint1Y = doc["checkpoint_1_y"] | 3;
  checkpoint2X = doc["checkpoint_2_x"] | 9;
  checkpoint2Y = doc["checkpoint_2_y"] | 3;
  
  Serial.printf("  Maze ID: %s\n", currentMazeId.c_str());
  Serial.printf("  Name: %s\n", mazeName.c_str());
  Serial.printf("  Start Position: (%d, %d)\n", startX, startY);
  Serial.printf("  End Position: (%d, %d)\n", endX, endY);
  Serial.printf("  Checkpoint 1: (%d, %d)\n", checkpoint1X, checkpoint1Y);
  Serial.printf("  Checkpoint 2: (%d, %d)\n", checkpoint2X, checkpoint2Y);
  
  // Load maze layout if provided
  if (doc.containsKey("maze_layout")) {
    JsonArray layout = doc["maze_layout"];
    
    if (layout.size() > 0) {
      Serial.printf("\n📍 Loading maze layout: %d rows\n", layout.size());
      
      int row = 0;
      for (JsonArray rowData : layout) {
        if (row < MAZE_HEIGHT) {
          int col = 0;
          for (int cell : rowData) {
            if (col < MAZE_WIDTH) {
              maze[row][col] = cell;
              col++;
            }
          }
          row++;
        }
      }
      Serial.printf("✅ Maze layout loaded successfully: %d x %d\n", row, MAZE_WIDTH);
    } else {
      Serial.println("⚠️  Maze layout array is empty, keeping default maze");
    }
  } else {
    Serial.println("⚠️  No maze_layout in config, keeping default maze");
  }
  
  // Reset player to new start position
  playerX = startX;
  playerY = startY;
  gameWon = false;
  gamePaused = false;
  
  configReceived = true;
  
  Serial.println("==================================================");
  Serial.println("✅ MAZE CONFIGURATION APPLIED SUCCESSFULLY");
  Serial.printf("   Player position reset to: (%d, %d)\n", playerX, playerY);
  Serial.println("==================================================\n");
  
  // Send confirmation
  sendMazeConfigConfirmation();
}

void sendMazeConfigConfirmation() {
  StaticJsonDocument<256> doc;
  doc["type"] = "MAZE_CONFIG_UPDATED";
  doc["message"] = "Maze configuration received and applied successfully";
  doc["device"] = "ESP32_Maze";
  doc["maze_id"] = currentMazeId;
  doc["maze_name"] = mazeName;
  doc["config_received"] = true;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(publish_topic, jsonString.c_str());
  Serial.println("✓ Sent configuration confirmation to Raspberry Pi\n");
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  Serial.printf("\n📨 MQTT Message [%s]: %d bytes\n", topic, length);
  
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  // Don't print full message if it's too long (maze config)
  if (length > 200) {
    Serial.printf("Message: (large payload - %d bytes)\n", length);
  } else {
    Serial.printf("Message: %s\n", message.c_str());
  }
  
  // Use larger buffer for maze configuration
  DynamicJsonDocument doc(4096);  // Increased to 4KB for maze layout
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("❌ JSON Parse Error: ");
    Serial.println(error.c_str());
    Serial.printf("Message length: %d bytes\n", length);
    return;
  }
  
  String command = doc["command"] | "";
  String type = doc["type"] | "";
  
  Serial.printf("Parsed - Type: '%s', Command: '%s'\n", type.c_str(), command.c_str());
  
  if (command == "UPDATE_MAZE_CONFIG" || type == "UPDATE_MAZE_CONFIG") {
    Serial.println("✅ Received maze configuration update!");
    applyMazeConfig(doc);
  }
  else if (command == "START_GAME" || type == "START_GAME") {
    gameActive = true;
    gameWon = false;
    gamePaused = false;
    playerX = startX;
    playerY = startY;
    Serial.println("🎮 Game started via MQTT");
    
  } else if (command == "STOP_GAME" || type == "STOP_GAME") {
    gameActive = false;
    Serial.println("⏹️  Game stopped via MQTT");
    
  } else if (command == "RESET_GAME" || type == "RESET_GAME") {
    gameWon = false;
    gamePaused = false;
    playerX = startX;
    playerY = startY;
    Serial.println("🔄 Game reset via MQTT");
    
  } else if (command == "PAUSE_TIMER" || type == "PAUSE_TIMER") {
    gamePaused = true;
    Serial.println("⏸️  Maze game paused");
    
    StaticJsonDocument<200> doc;
    doc["type"] = "MAZE_PAUSED";
    doc["message"] = "Maze game paused";
    doc["device"] = "ESP32_Maze";
    doc["maze_id"] = currentMazeId;
    doc["timestamp"] = millis();
    
    String jsonString;
    serializeJson(doc, jsonString);
    client.publish(publish_topic, jsonString.c_str());
  }
  else if (command == "RESUME_TIMER" || type == "RESUME_TIMER") {
    if (!gameWon) {
      gamePaused = false;
      Serial.println("▶️  Maze game resumed");
      
      StaticJsonDocument<200> doc;
      doc["type"] = "MAZE_RESUMED";
      doc["message"] = "Maze game resumed";
      doc["device"] = "ESP32_Maze";
      doc["maze_id"] = currentMazeId;
      doc["timestamp"] = millis();
      
      String jsonString;
      serializeJson(doc, jsonString);
      client.publish(publish_topic, jsonString.c_str());
    }
  }
  else if (command == "X" || type == "X") {
    gameActive = false;
    Serial.println("❌ Game deactivated via X command");
  } 
  else if (command == "ACTIVATE" || type == "ACTIVATE") {
    waiting = 1;
    Serial.println("🚀 Maze module activated! All modules connected.");
    sendConnectionStatus();
    
    // Request config again after activation to ensure we have it
    if (!configReceived) {
      delay(500);
      requestMazeConfig();
    }
  }
}