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

// Checkpoint positions (must be reached in order)
struct Checkpoint {
  int x;
  int y;
  bool reached;
  String name;
};

Checkpoint checkpoints[] = {
  {4, 3, false, "Checkpoint 1"},
  {9, 3, false, "Checkpoint 2"}
};
int numCheckpoints = 2;
int currentCheckpoint = 0;

// Player position
int playerX = 1;
int playerY = 1;

// End point position
int endX = 14;
int endY = 6;

// Maze layout (1 = wall, 0 = path)
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
unsigned long lastMoveTime = 0;
const unsigned long moveDelay = 200;
int waiting = 0;

void setup() {
  Serial.begin(115200);
  
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
  
  Serial.println("Maze Game Started!");
  Serial.println("Navigate through checkpoints to reach the end!");
}

void sendHeartbeat() {
  if (client.connected() && waiting == 1) {
    StaticJsonDocument<256> doc;
    doc["type"] = "HEARTBEAT";
    doc["device"] = "ESP32_Maze";
    doc["timestamp"] = millis();
    doc["game_active"] = gameActive;
    doc["game_won"] = gameWon;
    doc["current_checkpoint"] = currentCheckpoint;
    doc["total_checkpoints"] = numCheckpoints;
    
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
  
  if (waiting == 0) {
    displayWaitingMessage();
  }
  else if (waiting == 1) {
    if (!gameWon && gameActive) {
      handleInput();
      drawGame();
    } else if (gameWon) {
      displayWinMessage();
    } else {
      displayWaitingMessage();
    }
  }

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
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
        // Hit a wall - reset to start
        playerX = 1;
        playerY = 1;
        
        // Reset all checkpoints
        for (int i = 0; i < numCheckpoints; i++) {
          checkpoints[i].reached = false;
        }
        currentCheckpoint = 0;
        
        Serial.println("Hit wall! Returning to start. Checkpoints reset.");
        
        StaticJsonDocument<256> doc;
        doc["type"] = "WALL_HIT";
        doc["message"] = "Player hit wall, returned to start, checkpoints reset";
        doc["device"] = "ESP32_Maze";
        doc["timestamp"] = millis();
        
        String jsonString;
        serializeJson(doc, jsonString);
        client.publish(publish_topic, jsonString.c_str());
        
      } else {
        // Valid move
        playerX = newX;
        playerY = newY;
        
        // Check if reached current checkpoint
        if (currentCheckpoint < numCheckpoints) {
          Checkpoint* cp = &checkpoints[currentCheckpoint];
          if (playerX == cp->x && playerY == cp->y && !cp->reached) {
            cp->reached = true;
            currentCheckpoint++;
            
            Serial.printf("✓ Reached %s! (%d/%d)\n", 
                         cp->name.c_str(), 
                         currentCheckpoint, 
                         numCheckpoints);
            
            StaticJsonDocument<256> doc;
            doc["type"] = "CHECKPOINT_REACHED";
            doc["message"] = cp->name;
            doc["checkpoint_number"] = currentCheckpoint;
            doc["total_checkpoints"] = numCheckpoints;
            doc["device"] = "ESP32_Maze";
            doc["timestamp"] = millis();
            
            String jsonString;
            serializeJson(doc, jsonString);
            client.publish(publish_topic, jsonString.c_str());
            
            // Visual feedback
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(10, 25);
            display.println(cp->name);
            display.setCursor(30, 40);
            display.printf("%d/%d", currentCheckpoint, numCheckpoints);
            display.display();
            delay(1000);
          }
        }
        
        // Check if reached the end (only after all checkpoints)
        if (playerX == endX && playerY == endY) {
          if (currentCheckpoint >= numCheckpoints) {
            // All checkpoints reached - WIN!
            gameWon = true;
            Serial.println("🎉 YOU WIN! All checkpoints completed!");
            
            StaticJsonDocument<256> doc;
            doc["type"] = "MAZE_COMPLETED";
            doc["message"] = "Player completed maze with all checkpoints!";
            doc["time"] = millis();
            doc["device"] = "ESP32_Maze";
            doc["checkpoints_completed"] = currentCheckpoint;
            doc["timestamp"] = millis();
            
            String jsonString;
            serializeJson(doc, jsonString);
            client.publish(publish_topic, jsonString.c_str());
          } else {
            // Reached end without all checkpoints
            Serial.printf("❌ Need to reach checkpoints first! (%d/%d completed)\n", 
                         currentCheckpoint, numCheckpoints);
            
            StaticJsonDocument<256> doc;
            doc["type"] = "INCOMPLETE_MAZE";
            doc["message"] = "Reached end without all checkpoints";
            doc["checkpoints_completed"] = currentCheckpoint;
            doc["checkpoints_required"] = numCheckpoints;
            doc["device"] = "ESP32_Maze";
            doc["timestamp"] = millis();
            
            String jsonString;
            serializeJson(doc, jsonString);
            client.publish(publish_topic, jsonString.c_str());
          }
        }
      }
      
      lastMoveTime = millis();
    }
  }
  
  // Button press to restart
  if (digitalRead(JOY_BUTTON_PIN) == LOW) {
    if (gameWon) {
      gameWon = false;
      playerX = 1;
      playerY = 1;
      
      // Reset checkpoints
      for (int i = 0; i < numCheckpoints; i++) {
        checkpoints[i].reached = false;
      }
      currentCheckpoint = 0;
      
      StaticJsonDocument<256> doc;
      doc["type"] = "GAME_RESTART";
      doc["message"] = "Maze game restarted";
      doc["device"] = "ESP32_Maze";
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

  // Draw subtle dotted grid
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

  // Draw checkpoints
  for (int i = 0; i < numCheckpoints; i++) {
    int cpX = checkpoints[i].x * CELL_SIZE + CELL_SIZE/2;
    int cpY = checkpoints[i].y * CELL_SIZE + CELL_SIZE/2;
    
    if (checkpoints[i].reached) {
      // Filled circle for reached checkpoint
      display.fillCircle(cpX, cpY, 3, SSD1306_WHITE);
    } else if (i == currentCheckpoint) {
      // Hollow circle for current target checkpoint
      display.drawCircle(cpX, cpY, 3, SSD1306_WHITE);
    } else {
      // Small dot for future checkpoints
      display.drawPixel(cpX, cpY, SSD1306_WHITE);
    }
  }
  
  // Draw player
  int playerPixelX = playerX * CELL_SIZE + CELL_SIZE/2;
  int playerPixelY = playerY * CELL_SIZE + CELL_SIZE/2;
  display.fillCircle(playerPixelX, playerPixelY, 2, SSD1306_WHITE);
  
  // Draw checkpoint counter at top
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("CP: %d/%d", currentCheckpoint, numCheckpoints);
  
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
  display.printf("All %d checkpoints!", numCheckpoints);
  display.setCursor(10, 52);
  display.println("Press to restart");
  
  display.display();
}

void displayWaitingMessage() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20);
  display.println("Game Inactive");
  display.setCursor(10, 35);
  display.println("Waiting for RPi...");
  
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
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (client.connect("ESP32_Maze_Game")) {
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
  StaticJsonDocument<256> doc;
  doc["type"] = "MAZE_MODULE_CONNECTED";
  doc["message"] = "Maze navigation module ready";
  doc["device"] = "ESP32_Maze";
  doc["total_checkpoints"] = numCheckpoints;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(publish_topic, jsonString.c_str());
  Serial.println("Sent connection status to Raspberry Pi");
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.println("Received MQTT message: " + message);
  
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    String command = doc["command"];
    
    if (command == "START_GAME") {
      gameActive = true;
      gameWon = false;
      playerX = 1;
      playerY = 1;
      
      // Reset checkpoints
      for (int i = 0; i < numCheckpoints; i++) {
        checkpoints[i].reached = false;
      }
      currentCheckpoint = 0;
      
      Serial.println("Game started via MQTT");
      
    } else if (command == "STOP_GAME") {
      gameActive = false;
      Serial.println("Game stopped via MQTT");
      
    } else if (command == "RESET_GAME") {
      gameWon = false;
      playerX = 1;
      playerY = 1;
      
      // Reset checkpoints
      for (int i = 0; i < numCheckpoints; i++) {
        checkpoints[i].reached = false;
      }
      currentCheckpoint = 0;
      
      Serial.println("Game reset via MQTT");
      
    } else if (command == "PAUSE_TIMER") {
      gameActive = false;
      Serial.println("⏸️ Maze game paused");
      
      StaticJsonDocument<200> doc;
      doc["type"] = "MAZE_PAUSED";
      doc["message"] = "Maze game paused";
      doc["device"] = "ESP32_Maze";
      doc["timestamp"] = millis();
      
      String jsonString;
      serializeJson(doc, jsonString);
      client.publish(publish_topic, jsonString.c_str());
    }
    else if (command == "RESUME_TIMER") {
      if (!gameWon) {
        gameActive = true;
        Serial.println("▶️ Maze game resumed");
        
        StaticJsonDocument<200> doc;
        doc["type"] = "MAZE_RESUMED";
        doc["message"] = "Maze game resumed";
        doc["device"] = "ESP32_Maze";
        doc["timestamp"] = millis();
        
        String jsonString;
        serializeJson(doc, jsonString);
        client.publish(publish_topic, jsonString.c_str());
      }
    }
    else if (command == "X") {
      gameActive = false;
      Serial.println("Game deactivated via X command");
    } 
    else if (command == "ACTIVATE") {
      waiting = 1;
      Serial.println("🚀 Maze module activated! All modules connected.");
      sendConnectionStatus();
    }
    else if (command == "UPDATE_MAZE_CONFIG") {
      // Future: Load maze configuration from RPi
      Serial.println("Received maze configuration update");
    }
  }
}