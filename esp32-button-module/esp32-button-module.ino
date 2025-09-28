#include <FastLED.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define NUM_LEDS    20
const int buttonPin = 23;
const int ledPin = 32;

// WiFi and MQTT settings
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
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
int targetTime = 2000;

// Game state
bool gameActive = true;
bool gameWon = false;
bool gameLost = false;

CRGB leds[NUM_LEDS];

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
  
  if (command == "X") {
    Serial.println("Received 'X' signal - Game Over!");
    gameActive = false;
    gameLost = true;
    
    // Flash red LEDs to indicate game over
    for (int i = 0; i < 3; i++) {
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      delay(300);
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      delay(300);
    }
  }
  else if (command == "START_GAME") {
    Serial.println("Starting new game!");
    gameActive = true;
    gameWon = false;
    gameLost = false;
    currentLed = 0;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
  else if (command == "RESET_GAME") {
    Serial.println("Resetting game!");
    gameActive = true;
    gameWon = false;
    gameLost = false;
    currentLed = 0;
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32ButtonGame")) {
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

void sendGameResult(bool won, unsigned long duration) {
  DynamicJsonDocument doc(1024);
  
  if (won) {
    doc["type"] = "BUTTON_GAME_WON";
    doc["message"] = "Player won the button timing game!";
  } else {
    doc["type"] = "BUTTON_GAME_LOST";
    doc["message"] = "Player lost the button timing game!";
  }
  
  doc["press_duration"] = duration;
  doc["target_time"] = targetTime;
  doc["tolerance"] = 500;
  doc["difference"] = abs((int)duration - targetTime);
  
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
  Serial.print("Target time: ");
  Serial.print(targetTime);
  Serial.println(" ms (+/- 500ms)");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Only process game logic if game is active
  if (!gameActive) {
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
      if (pressDuration >= (targetTime - 500) && pressDuration <= (targetTime + 500)) {
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
        Serial.println("You Lose!");
        gameLost = true;
        gameActive = false;
        
        // Light up LEDs in red for lose
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        
        // Send lose message to Raspberry Pi
        sendGameResult(false, pressDuration);
        
        delay(3000);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
      }
      
      int difference = pressDuration - targetTime;
      Serial.print("You were ");
      Serial.print(abs(difference));
      Serial.println(" ms off target");
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