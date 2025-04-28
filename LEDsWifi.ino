/*
  Neural Kinetic Sculpture - ESP32 Controller
  Controls WS2812B LEDs in response to WiFi commands
  Based on code by Letzy Mota and Ashley Nguyen and Melina Moore
  Modified: April 28, 2025
*/

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// LED Configuration
#define LED_PIN 18         // Pin controlling LED strip
#define NUM_LEDS 196       // ~195 LEDs per 4ft strip (for 1 panel)
#define MAX_PANELS 9       // Maximum 9 panels (3x3 grid)
#define DEFAULT_BRIGHTNESS 50  // Default brightness (0-255)

// WiFi Configuration
// const char* ssid = "YOUR_WIFI_SSID";       // Replace with your WiFi name
// const char* password = "YOUR_WIFI_PASSWORD"; // Replace with your WiFi password

// WebSocket Server Configuration
const char* websocket_server = "signal-filter.onrender.com";
const uint16_t websocket_port = 80;
const char* websocket_path = "/";

// LED Strips (one per panel)
Adafruit_NeoPixel panels[MAX_PANELS];
int activePanelsCount = 0;
int totalRows = 3;
int totalCols = 3;

// Panel configuration
struct PanelConfig {
  int rows;
  int columns;
  float xPosition;
  float yPosition;
  int brightness;
  uint32_t color;
  uint32_t currentColor;  // To track current color for transitions
  int currentBrightness;  // To track current brightness for transitions
  bool isActive;
};

PanelConfig panelConfigs[MAX_PANELS];
WebSocketsClient webSocket;

// Animation variables
float animationSpeed = 1.0;   // Default speed
int animationDirection = 1;   // 1 = up, 0 = down

void setup() {
  Serial.begin(115200);
  Serial.println("\nNeural Kinetic Sculpture - ESP32 Controller");
  
  // Initialize LED panels
  initializePanels();
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initialize WebSocket connection
  webSocket.begin(websocket_server, websocket_port, websocket_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();
  
  // Update panels based on their configurations
  updatePanels();
  
  delay(10);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void initializePanels() {
  // Default configuration: 3x3 grid of panels
  totalRows = 3;
  totalCols = 3;
  
  // Initialize each panel
  for (int i = 0; i < MAX_PANELS; i++) {
    panels[i] = Adafruit_NeoPixel(NUM_LEDS, LED_PIN + i, NEO_GRB + NEO_KHZ800);
    panels[i].begin();
    panels[i].setBrightness(DEFAULT_BRIGHTNESS);
    panels[i].clear();
    panels[i].show();
    
    // Initialize panel configuration
    panelConfigs[i].rows = totalRows;
    panelConfigs[i].columns = totalCols;
    panelConfigs[i].xPosition = (i % totalCols) * (100.0 / totalCols);
    panelConfigs[i].yPosition = (i / totalCols) * (100.0 / totalRows);
    panelConfigs[i].brightness = DEFAULT_BRIGHTNESS;
    panelConfigs[i].color = panels[i].Color(0, 0, 255); // Default blue
    panelConfigs[i].currentColor = panels[i].Color(0, 0, 255); // Default blue
    panelConfigs[i].currentBrightness = DEFAULT_BRIGHTNESS;
    panelConfigs[i].isActive = false;
  }
  
  Serial.println("LED panels initialized");
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
      
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      // Send a message to the server to confirm connection
      webSocket.sendTXT("ESP32 Neural Kinetic Sculpture Controller connected");
      break;
      
    case WStype_TEXT:
      Serial.printf("Received: %s\n", payload);
      handleIncomingData((char*)payload);
      break;
  }
}

void handleIncomingData(char* data) {
  // Check for command prefix
  if (strncmp(data, "ALLPANELS ", 10) == 0) {
    // Parse panel data
    parsePanelData(data + 10); // Skip "ALLPANELS " prefix
  } 
  else if (strncmp(data, "CONFIG ", 7) == 0) {
    // Handle CONFIG command (speed, direction)
    parseConfigData(data + 7); // Skip "CONFIG " prefix
  }
  else if (strcmp(data, "START") == 0) {
    // Start animation/control
    Serial.println("Received START command");
    // Implementation for START
  }
  else if (strcmp(data, "STOP") == 0) {
    // Stop animation/control
    Serial.println("Received STOP command");
    // Deactivate all panels
    for (int i = 0; i < MAX_PANELS; i++) {
      panelConfigs[i].isActive = false;
    }
    activePanelsCount = 0;
  }
  else if (strncmp(data, "EEG ", 4) == 0) {
    // Handle EEG data (optional)
    parseEEGData(data + 4); // Skip "EEG " prefix
  }
}

void parsePanelData(char* jsonData) {
  // Parse JSON array of panel configurations
  // Format: [[rows, column, xposition, yposition, brightness, color], [...], ...]
  
  // Use ArduinoJson to parse the data
  DynamicJsonDocument doc(4096); // Adjust size based on your needs
  DeserializationError error = deserializeJson(doc, jsonData);
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Reset active panels
  for (int i = 0; i < MAX_PANELS; i++) {
    panelConfigs[i].isActive = false;
  }
  
  // Process each panel configuration
  JsonArray panelArray = doc.as<JsonArray>();
  activePanelsCount = 0;
  
  for (JsonArray panel : panelArray) {
    if (activePanelsCount >= MAX_PANELS) break;
    
    if (panel.size() >= 6) {
      int rows = panel[0];
      int columns = panel[1];
      float xPosition = panel[2];
      float yPosition = panel[3];
      int brightness = panel[4];
      const char* colorHex = panel[5];
      
      // Find which panel this corresponds to based on position
      int panelIndex = findPanelByPosition(xPosition, yPosition, rows, columns);
      
      if (panelIndex >= 0 && panelIndex < MAX_PANELS) {
        panelConfigs[panelIndex].rows = rows;
        panelConfigs[panelIndex].columns = columns;
        panelConfigs[panelIndex].xPosition = xPosition;
        panelConfigs[panelIndex].yPosition = yPosition;
        panelConfigs[panelIndex].brightness = brightness;
        panelConfigs[panelIndex].color = hexToColor(colorHex);
        panelConfigs[panelIndex].isActive = true;
        activePanelsCount++;
        
        Serial.printf("Activated panel %d at position (%.1f, %.1f) with color %s and brightness %d\n", 
                    panelIndex, xPosition, yPosition, colorHex, brightness);
      }
    }
  }
  
  Serial.printf("Processed %d panel configurations\n", activePanelsCount);
}

int findPanelByPosition(float x, float y, int rows, int columns) {
  // Calculate the panel index based on x,y position
  int col = round(x / (100.0 / columns));
  int row = round(y / (100.0 / rows));
  
  // Ensure we're within bounds
  if (col < 0) col = 0;
  if (col >= columns) col = columns - 1;
  if (row < 0) row = 0;
  if (row >= rows) row = rows - 1;
  
  return row * columns + col;
}

void parseConfigData(char* configData) {
  // Parse CONFIG command: speed and direction
  char* token = strtok(configData, " ");
  int tokenCount = 0;
  
  while (token != NULL && tokenCount < 2) {
    if (tokenCount == 0) {
      // First token: speed
      animationSpeed = atof(token);
    } else if (tokenCount == 1) {
      // Second token: direction
      animationDirection = atoi(token);
    }
    token = strtok(NULL, " ");
    tokenCount++;
  }
  
  Serial.printf("Set animation speed: %.2f, direction: %d\n", animationSpeed, animationDirection);
}

void parseEEGData(char* eegData) {
  // Parse EEG data (optional feature)
  // Format: alpha beta theta delta gamma dominant_band
  
  float alpha, beta, theta, delta, gamma;
  int dominantBand;
  
  sscanf(eegData, "%f %f %f %f %f %d", &alpha, &beta, &theta, &delta, &gamma, &dominantBand);
  
  Serial.printf("EEG Data: α=%.2f, β=%.2f, θ=%.2f, δ=%.2f, γ=%.2f, dominant=%d\n", 
              alpha, beta, theta, delta, gamma, dominantBand);
  
  // You can use this data to modify animations or LED behavior
}

uint32_t hexToColor(const char* hexColor) {
  // Convert hex color string to uint32_t color
  // Format: "#RRGGBB" or "RRGGBB"
  
  long number = strtol(hexColor[0] == '#' ? hexColor + 1 : hexColor, NULL, 16);
  
  // Extract RGB components
  uint8_t r = (number >> 16) & 0xFF;
  uint8_t g = (number >> 8) & 0xFF;
  uint8_t b = number & 0xFF;
  
  // Return color in the format needed by NeoPixel
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void updatePanels() {
  // Update each active panel
  for (int i = 0; i < MAX_PANELS; i++) {
    if (panelConfigs[i].isActive) {
      // Transition to new color and brightness if changed
      if (panelConfigs[i].currentColor != panelConfigs[i].color || 
          panelConfigs[i].currentBrightness != panelConfigs[i].brightness) {
        
        transitionLEDs(i, panelConfigs[i].color, panelConfigs[i].brightness);
      }
    } else {
      // If panel is not active, turn off LEDs
      if (panelConfigs[i].currentBrightness > 0) {
        panels[i].clear();
        panels[i].show();
        panelConfigs[i].currentBrightness = 0;
      }
    }
  }
}

void setAllLEDs(int panelIndex, uint32_t color) {
  // Set all LEDs on a panel to a specific color
  for (int i = 0; i < NUM_LEDS; i++) {
    panels[panelIndex].setPixelColor(i, color);
  }
  panels[panelIndex].show();
}

void transitionLEDs(int panelIndex, uint32_t newColor, int newBrightness) {
  // Extract RGB components of current color
  uint32_t currColor = panelConfigs[panelIndex].currentColor;
  int currBrightness = panelConfigs[panelIndex].currentBrightness;
  
  int r1 = (currColor >> 16) & 0xFF;
  int g1 = (currColor >> 8) & 0xFF;
  int b1 = currColor & 0xFF;
  
  // Extract RGB components of target color
  int r2 = (newColor >> 16) & 0xFF;
  int g2 = (newColor >> 8) & 0xFF;
  int b2 = newColor & 0xFF;
  
  // Number of steps for transition
  const int steps = 50; // Reduced for performance
  
  // Perform the transition
  for (int i = 0; i <= steps; i++) {
    float t = i / (float)steps;  // Interpolation factor from 0.0 to 1.0
    
    // Interpolate RGB values
    int r = round(r1 + (r2 - r1) * t);
    int g = round(g1 + (g2 - g1) * t);
    int b = round(b1 + (b2 - b1) * t);
    
    // Interpolate brightness
    int brightness = round(currBrightness + (newBrightness - currBrightness) * t);
    panels[panelIndex].setBrightness(brightness);
    
    // Set the color
    uint32_t interpolatedColor = panels[panelIndex].Color(r, g, b);
    setAllLEDs(panelIndex, interpolatedColor);
    
    delay(10);
  }
  
  // Ensure final state is set
  panels[panelIndex].setBrightness(newBrightness);
  setAllLEDs(panelIndex, newColor);
  
  // Update current values
  panelConfigs[panelIndex].currentColor = newColor;
  panelConfigs[panelIndex].currentBrightness = newBrightness;
}
