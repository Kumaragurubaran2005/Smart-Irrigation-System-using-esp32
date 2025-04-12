#include <WiFi.h>
#include <WebSocketsServer.h>
#include <DHT.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "kumar";
const char* password = "200513111";

// WebSocket server on port 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Pins
#define BUILTIN_LED 2          // Built-in LED (GPIO2)
#define SOIL_MOISTURE_PIN 34   // Analog input
#define WATER_LEVEL_PIN 35     // Analog input
#define DHT_PIN 4              // Digital input
#define DHT_TYPE DHT11

// Sensor calibration (adjust these based on your sensor readings)
#define SOIL_DRY 4095   // Value when completely dry
#define SOIL_WET 750    // Value when in water
#define WATER_EMPTY 0 // Value when empty
#define WATER_FULL 2500  // Value when full

// Motor control
const unsigned long MAX_RUN_TIME = 300000; // 5 minutes max (safety limit)
bool ledActive = false;
unsigned long ledStartTime = 0;
unsigned long requestedDuration = 0;  // Stores duration in milliseconds

DHT dht(DHT_PIN, DHT_TYPE);

// Function to convert raw ADC to percentage (0-100)
int toPercentage(int raw, int dry, int wet) {
  raw = constrain(raw, wet, dry); // Ensure value is within range
  return map(raw, dry, wet, 0, 100); // Convert to percentage
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %s\n", num, ip.toString().c_str());
      break;
    }
    case WStype_TEXT:
      handleWebSocketMessage(num, (char*)payload);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize built-in LED
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);
  Serial.println("Initialized built-in LED (GPIO2) to LOW");
  
  // Initialize sensors
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  dht.begin();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("\nSystem Ready. Built-in LED will simulate motor:");
  Serial.println("- LED ON = Motor running");
  Serial.println("- LED OFF = Motor stopped");
  Serial.println("- Duration should be specified in minutes");
}

void loop() {
  webSocket.loop();
  
  // Check if we need to automatically turn off
  if (ledActive && requestedDuration > 0) {
    unsigned long elapsedTime = millis() - ledStartTime;
    
    // First check safety limit
    if (elapsedTime >= MAX_RUN_TIME) {
      emergencyStop("Maximum run time reached");
    }
    // Then check if requested duration has elapsed
    else if (elapsedTime >= requestedDuration) {
      stopMotor();
      Serial.println("Stopped after requested duration");
    }
  }
  
  // Send sensor data every 2 seconds
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 2000) {
    sendSensorData();
    lastUpdate = millis();
  }
}

void handleWebSocketMessage(uint8_t num, char* payload) {
  Serial.printf("Received: %s\n", payload);
  
  DynamicJsonDocument doc(200);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  if (doc["type"] == "irrigation_command") {
    if (doc["command"] == "start") {
      // Convert minutes to milliseconds
      requestedDuration = doc["duration"].as<unsigned long>() * 60000UL;
      startMotor();
    } 
    else if (doc["command"] == "stop") {
      stopMotor();
    }
    else if (doc["command"] == "pulse") {
      pulseMotor();
    }
  }
}

void startMotor() {
  if (ledActive) {
    Serial.println("Motor (LED) already running");
    return;
  }
  
  Serial.println("\n=== ACTIVATING MOTOR (LED ON) ===");
  digitalWrite(BUILTIN_LED, HIGH);
  ledActive = true;
  ledStartTime = millis();
  
  if (requestedDuration > 0) {
    Serial.printf("Will run for exactly %lu minutes\n", requestedDuration/60000UL);
  } else {
    Serial.println("Will run until stopped manually");
  }
  
  Serial.printf("LED state: %s\n", digitalRead(BUILTIN_LED) ? "ON" : "OFF");
}

void stopMotor() {
  Serial.println("\n=== DEACTIVATING MOTOR (LED OFF) ===");
  digitalWrite(BUILTIN_LED, LOW);
  ledActive = false;
  requestedDuration = 0; // Reset duration
  
  Serial.printf("LED state: %s\n", digitalRead(BUILTIN_LED) ? "ON" : "OFF");
}

void pulseMotor() {
  // Temporarily override any duration setting
  unsigned long savedDuration = requestedDuration;
  requestedDuration = 0;
  
  Serial.println("\nTesting motor (LED) with 3 pulses:");
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUILTIN_LED, HIGH);
    Serial.println("LED ON (Motor simulated ON)");
    delay(1000);
    digitalWrite(BUILTIN_LED, LOW);
    Serial.println("LED OFF (Motor simulated OFF)");
    delay(1000);
  }
  
  requestedDuration = savedDuration;
}

void emergencyStop(const char* reason) {
  Serial.printf("EMERGENCY STOP: %s\n", reason);
  digitalWrite(BUILTIN_LED, LOW);
  ledActive = false;
  requestedDuration = 0;
  
  // Notify clients
  String alert = "{\"type\":\"alert\",\"message\":\"";
  alert += reason;
  alert += "\"}";
  webSocket.broadcastTXT(alert);
}

int rssiToPercentage(int rssi) {
  if (rssi >= -50) return 100; // Excellent signal
  if (rssi <= -100) return 0;  // No signal
  return 2 * (rssi + 100); // Linear approximation
}

void sendSensorData() {
  DynamicJsonDocument doc(256);
  doc["motorPin"] = BUILTIN_LED;
  doc["motorState"] = digitalRead(BUILTIN_LED) ? "ON" : "OFF";
  
  // Calculate remaining time in minutes
  if (ledActive && requestedDuration > 0) {
    unsigned long elapsed = millis() - ledStartTime;
    unsigned long remaining = (elapsed < requestedDuration) ? (requestedDuration - elapsed) : 0;
    doc["remainingTime"] = remaining / 60000UL; // Convert to minutes
  } else {
    doc["remainingTime"] = 0;
  }
  
  // Sensor data as percentages
  doc["humidity"] = dht.readHumidity();
  doc["temperature"] = dht.readTemperature();
  
  // Convert soil moisture to percentage (higher value = more dry)
  int soilRaw = analogRead(SOIL_MOISTURE_PIN);
  doc["soilMoisture"] = toPercentage(soilRaw, SOIL_DRY, SOIL_WET);
  
  // Convert water level to percentage
  int waterRaw = analogRead(WATER_LEVEL_PIN);
  doc["waterLevel"] = (waterRaw*100)/ 2500;
  doc["signalStrength"] = rssiToPercentage(WiFi.RSSI());
  String payload;
  serializeJson(doc, payload);
  webSocket.broadcastTXT(payload);

}