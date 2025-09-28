#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi and MQTT settings
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "192.168.1.201";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT topics
const char* subscribe_topic = "rpi/to/esp3";  // Topic to receive commands from RPi
const char* publish_topic = "esp3/to/rpi";    // Topic to send data to RPi

// Joystick pins
#define JOY_X_PIN 36
#define JOY_Y_PIN 33
#define JOY_BUTTON_PIN 25

// Game settings
#define MAZE_WIDTH 16
#define MAZE_HEIGHT 8
#define CELL_SIZE 8

// Mid circle position
int midX = 4;
int midY = 3;

// Mid circle 2 position
int midX2 = 9;
int midY2 = 3;

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
bool gameActive = true;  // Can be controlled via MQTT
unsigned long lastMoveTime = 0;
const unsigned long moveDelay = 200; // 200ms delay between moves

void setup() {
  Serial.begin(115200);
  
  // Initialize WiFi
  setupWiFi();
  
  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(onMqttMessage);
  
  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  // Initialize joystick pins
  pinMode(JOY_BUTTON_PIN, INPUT_PULLUP);
  
  // Clear display
  display.clearDisplay();
  display.display();
  
  Serial.println("Maze Game Started!");
}

void loop() {
  // Maintain MQTT connection
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  if (!gameWon && gameActive) {
    handleInput();
    drawGame();
  } else if (gameWon) {
    displayWinMessage();
  } else {
    // Game is inactive - show waiting message
    displayWaitingMessage();
  }

  int xValue = analogRead(JOY_X_PIN);
  int yValue = analogRead(JOY_Y_PIN);

  Serial.println(xValue);
  Serial.println(yValue);

  delay(50); // Small delay for stability
}

void handleInput() {
  if (millis() - lastMoveTime < moveDelay) {
    return; // Too soon for next move
  }
  
  // Read joystick values
  int xValue = analogRead(JOY_X_PIN);
  int yValue = analogRead(JOY_Y_PIN);
  
  int newX = playerX;
  int newY = playerY;

  int xOffset = abs(xValue - 2048); // 2048 is roughly center for 12-bit ADC
  int yOffset = abs(yValue - 2048);
  
  // Only move in the direction with the largest offset (prevents diagonals)
  if (xOffset > yOffset && xOffset > 1000) {
    // X-axis has priority
    if (xValue < 1000) { // Left
      newX++;
    } else if (xValue > 3000) { // Right
      newX--;
    }
  } else if (yOffset > 1000) {
    // Y-axis movement
    if (yValue < 1000) { // Up
      newY++;
    } else if (yValue > 3000) { // Down
      newY--;
    }
  }
  
  // Check if movement occurred
  if (newX != playerX || newY != playerY) {
    // Check bounds
    if (newX >= 0 && newX < MAZE_WIDTH && newY >= 0 && newY < MAZE_HEIGHT) {
      // Check if new position is a wall
      if (maze[newY][newX] == 1) {
        // Hit a wall - reset to start position
        playerX = 1;
        playerY = 1;
        Serial.println("Hit wall! Returning to start.");
        
        // Send wall hit notification to RPi
        sendMqttMessage("WALL_HIT", "Player hit wall and returned to start");
        
      } else {
        // Valid move
        playerX = newX;
        playerY = newY;
        
        // Check if reached the end
        if (playerX == endX && playerY == endY) {
          gameWon = true;
          Serial.println("You Win!");
          
          // Send win notification to RPi
          StaticJsonDocument<200> doc;
          doc["type"] = "MAZE_COMPLETED";
          doc["message"] = "Player completed the maze!";
          doc["time"] = millis();
          
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
      // Restart game
      gameWon = false;
      playerX = 1;
      playerY = 1;
      sendMqttMessage("GAME_RESTART", "Maze game restarted");
      delay(300); // Debounce
    }
  }
}

void drawGame() {
  display.clearDisplay();

  // Draw subtle dotted grid lines for visual reference (doesn't affect gameplay)
  // Draw vertical dotted lines
  for (int x = 1; x < MAZE_WIDTH; x++) { // Skip outer borders
    for (int y = 0; y < MAZE_HEIGHT * CELL_SIZE; y += 2) { // Draw every 2nd pixel
      display.drawPixel(x * CELL_SIZE, y, SSD1306_WHITE);
    }
  }
  // Draw horizontal dotted lines
  for (int y = 1; y < MAZE_HEIGHT; y++) { // Skip outer borders
    for (int x = 0; x < MAZE_WIDTH * CELL_SIZE; x += 2) { // Draw every 2nd pixel
      display.drawPixel(x, y * CELL_SIZE, SSD1306_WHITE);
    }
  }

  // Draw mid point (circle)
  int middleX = midX * CELL_SIZE + CELL_SIZE/2;
  int middleY = midY * CELL_SIZE + CELL_SIZE/2;
  display.drawCircle(middleX, middleY, 3, SSD1306_WHITE);

  // Draw mid point 2 (circle)
  int middleX2 = midX2 * CELL_SIZE + CELL_SIZE/2;
  int middleY2 = midY2 * CELL_SIZE + CELL_SIZE/2;
  display.drawCircle(middleX2, middleY2, 3, SSD1306_WHITE);
  
  // // Draw end point (circle) - commented out as in your original
  // int endPixelX = endX * CELL_SIZE + CELL_SIZE/2;
  // int endPixelY = endY * CELL_SIZE + CELL_SIZE/2;
  // display.drawCircle(endPixelX, endPixelY, 3, SSD1306_WHITE);
  
  // Draw player (filled dot)
  int playerPixelX = playerX * CELL_SIZE + CELL_SIZE/2;
  int playerPixelY = playerY * CELL_SIZE + CELL_SIZE/2;
  display.fillCircle(playerPixelX, playerPixelY, 2, SSD1306_WHITE);
  
  display.display();
}

void displayWinMessage() {
  display.clearDisplay();
  
  // Draw "You Win!" message
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20);
  display.println("You Win!");
  
  display.setTextSize(1);
  display.setCursor(10, 45);
  display.println("Press button to restart");
  
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

// WiFi setup function
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

// MQTT reconnection function
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (client.connect("ESP32_Maze_Game")) {
      Serial.println("connected");
      client.subscribe(subscribe_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// MQTT message callback
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.println("Received MQTT message: " + message);
  
  // Parse JSON message
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    String command = doc["command"];
    
    if (command == "START_GAME") {
      gameActive = true;
      gameWon = false;
      playerX = 1;
      playerY = 1;
      Serial.println("Game started via MQTT");
      
    } else if (command == "STOP_GAME") {
      gameActive = false;
      Serial.println("Game stopped via MQTT");
      
    } else if (command == "RESET_GAME") {
      gameWon = false;
      playerX = 1;
      playerY = 1;
      Serial.println("Game reset via MQTT");
      
    } else if (command == "X") {
      gameActive = false;
      Serial.println("Game deactivated via X command");
    }
  }
}

// Helper function to send MQTT messages
void sendMqttMessage(String type, String message) {
  StaticJsonDocument<200> doc;
  doc["type"] = type;
  doc["message"] = message;
  doc["time"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(publish_topic, jsonString.c_str());
}