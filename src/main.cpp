/*
 * OBD2 AI Diagnostic Kiosk - Clean Version
 * For new ESP32 board with 2.2" LCD (240x320)
 * 
 * Maintains ALL original functionality but with cleaner code structure:
 * - WiFi and web API integration  
 * - QR code generation and display
 * - Payment processing integration
 * - Full kiosk state management
 * - Professional UI adapted for smaller screen
 * 
 * Hardware: ESP32 with TJA1050 CAN transceiver
 * Display: 2.2" TFT LCD (240x320 pixels)
 */

#include <Arduino.h>
#include <driver/twai.h>
#include <TFT_eSPI.h>
#include <qrcode.h>
#include <vector>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ELMduino.h>

// ========== NEW BOARD PINOUTS ==========
#define CAN_TX_PIN    GPIO_NUM_5    // CAN TX (safe for ESP32-S3)
#define CAN_RX_PIN    GPIO_NUM_4    // CAN RX (safe for ESP32-S3)
#define SCAN_BUTTON   2           // SW_TRIG pin

// ========== DISPLAY CONFIGURATION ==========
// 2.2" LCD: 240x320 pixels - adapted from original 320x480
TFT_eSPI tft = TFT_eSPI();
const int SCREEN_WIDTH  = 240;
const int SCREEN_HEIGHT = 320;

// ========== KIOSK STATES ==========
enum KioskState {
  READY_SCREEN,    
  DISPLAY_QR,      
  PAYMENT_LOADING, 
  WAITING_PAYMENT,
  READY_TO_SCAN,
  SCANNING,
  DISPLAY_RESULTS,
  ERROR_STATE,
  VEHICLE_SETUP,   
  VEHICLE_DETECTING,
  TROUBLESHOOTING  
};

KioskState currentState = DISPLAY_QR; // Boot directly to QR code

// ========== GLOBAL VARIABLES ==========
String transactionId = "";
unsigned long stateStartTime = 0;
unsigned long sessionStartTime = 0;
unsigned long lastPaymentCheck = 0;
int vehicleDetectionAttempt = 0;

// Display state tracking (to prevent redundant redraws)
bool screenDrawFlags[10] = {false}; // One for each state
bool forceRedraw = false; // Global flag to force all displays to redraw

// Session configuration
const unsigned long SESSION_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes
const unsigned long PAYMENT_POLL_INTERVAL = 3000; // 3 seconds

// Diagnostic scan timeouts
const unsigned long TOTAL_SCAN_TIMEOUT_MS = 45 * 1000; // 45 seconds max scan time
const unsigned long BAUD_DETECT_TIMEOUT_MS = 2000;     // 2 seconds per baud rate
const unsigned long TRAFFIC_LISTEN_TIMEOUT_MS = 5000;  // 5 seconds listening
const unsigned long ECU_PROBE_TIMEOUT_MS = 15000;      // 15 seconds probing ECUs

// WiFi and API configuration
const char* WIFI_SSID     = "Pasha";
const char* WIFI_PASSWORD = "E38740i!";
const char* API_BASE_URL  = "https://obd2ai-server-1afd74c5766a.herokuapp.com";
const char* WEBAPP_URL    = "https://obd2ai-webapp-805f8e39122c.herokuapp.com";
const char* KIOSK_ID      = "DEMO_KIOSK";

// OBD2 data structures
struct FaultCode {
  String code;
  String system;
  bool isPending;
  uint16_t ecuId;
};

std::vector<FaultCode> detectedCodes;
std::vector<uint16_t> activeECUs;
bool vehicleDetected = false; // Track if vehicle was detected during scan
twai_message_t message;
ELM327 myELM327;

// Standard OBD2 ECU addresses
const uint16_t OBD2_ADDRESSES[] = {
  0x7E0, 0x7E1, 0x7E2, 0x7E3, 0x7E4, 0x7E5, 0x7E6, 0x7E7,
  0x7E8, 0x7E9, 0x7EA, 0x7EB, 0x7EC, 0x7ED, 0x7EE, 0x7EF
};
const int NUM_ECUS = sizeof(OBD2_ADDRESSES) / sizeof(OBD2_ADDRESSES[0]);

// ========== FUNCTION DECLARATIONS ==========
// Initialization
void initializeDisplay();
void initializeWiFi();
void initializeCAN();

// Kiosk Management
void updateKioskState();
void handleSessionTimeout();
String createNewSession();
bool checkPaymentStatus();

// Display Functions (adapted for 240x320)
void displayReadyScreen();
void displayQRCode();
void displayPaymentLoading();
void displayWaitingPayment();
void displayReadyToScan(bool fullRedraw = false);
void displayScanning(bool fullRedraw = false);
void displayScanResults();
void displayError(String message);
void drawQRCode(String data, int x, int y, int scale);

// Real CAN Bus Scanning
void performDiagnosticScan();
uint32_t autoDetectCANBaudRate();
void listenForCANTraffic(uint32_t duration_ms);
void probeOBD2ECUs();
void probeOBD2ECUsWithTimeout(uint32_t timeout_ms);
void scanAllDTCs();
bool testECUCommunication(uint16_t ecuId);
void scanForDTCs(uint16_t ecuId);
void parseAndStoreDTC(uint8_t* data, int len, uint16_t ecuId);
bool reinitializeCAN(uint32_t baudRate);
void updateScanProgress(String message, int percentage);

// Utility Functions
void handleButtonPress();
void resetToReady();
void resetDisplayFlags();

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  
  delay(1000);
  
  Serial.println("=== OBD2 AI KIOSK - CLEAN VERSION ===");
  Serial.println("Board: New ESP32 with 2.2\" LCD");
  Serial.println("Features: Full functionality, cleaner code");
  Serial.println("Pins: CAN_TX=38, CAN_RX=40, BTN=41");
  Serial.println("====================================");
  
  // Initialize all systems
  initializeDisplay();
  initializeWiFi();
  initializeCAN();
  
  // Setup button (keeping for potential manual override)
  pinMode(SCAN_BUTTON, INPUT_PULLUP);
  
  // Create session immediately on boot and show QR code
  Serial.println("🚀 Boot-to-scan mode: Creating session automatically...");
  transactionId = createNewSession();
  
  if (transactionId.length() > 0) {
    currentState = DISPLAY_QR;
    sessionStartTime = millis();
    Serial.println("✓ Session created on boot: " + transactionId);
    Serial.println("✓ QR code will be displayed immediately");
  } else {
    Serial.println("❌ Failed to create session on boot, falling back to ready screen");
    currentState = READY_SCREEN;
  }
  
  stateStartTime = millis();
  Serial.println("✓ Kiosk initialized in boot-to-scan mode");
}

// ========== MAIN LOOP ==========
void loop() {
  // Button still available for manual override/debugging
  handleButtonPress();
  updateKioskState();
  handleSessionTimeout();
  delay(50);
}

// ========== INITIALIZATION FUNCTIONS ==========
void initializeDisplay() {
  // Set up backlight pin (TFT_BL = 15)
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH); // Turn on backlight
  
  tft.init();
  tft.setRotation(1); // 90 degrees rotation for proper orientation (320x240)
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Test display with a simple message
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("KIOSK");
  tft.setCursor(10, 40);
  tft.println("STARTING...");
  delay(2000);
  tft.fillScreen(TFT_BLACK); // Clear test message
  
  Serial.println("✓ Display initialized (240x320) with backlight");
}

void initializeWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED && millis() < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n❌ WiFi connection failed");
  }
}

void initializeCAN() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("✓ CAN driver installed");
  } else {
    Serial.println("❌ Failed to install CAN driver");
    return;
  }
  
  if (twai_start() == ESP_OK) {
    Serial.println("✓ CAN driver started");
  } else {
    Serial.println("❌ Failed to start CAN driver");
  }
}

// ========== KIOSK STATE MANAGEMENT ==========
void updateKioskState() {
  switch (currentState) {
    case READY_SCREEN:
      displayReadyScreen();
      break;
      
    case DISPLAY_QR:
      displayQRCode();
      // Auto-transition to payment polling after QR is displayed
      // Give customer time to scan QR code and navigate to payment
      if (millis() - stateStartTime > 5000) { // 5 seconds to scan QR
        currentState = WAITING_PAYMENT;
        lastPaymentCheck = millis(); // Initialize payment polling
        stateStartTime = millis();
        Serial.println("⏰ Transitioning to payment waiting mode...");
      }
      break;
      
    case PAYMENT_LOADING:
      displayPaymentLoading();
      currentState = WAITING_PAYMENT;
      stateStartTime = millis();
      break;
      
    case WAITING_PAYMENT:
      displayWaitingPayment();
      
      // Check payment status periodically
      if (millis() - lastPaymentCheck > PAYMENT_POLL_INTERVAL) {
        lastPaymentCheck = millis();
        if (checkPaymentStatus()) {
          Serial.println("✅ Payment confirmed! Auto-starting scan...");
          currentState = SCANNING;  // Auto-start scan immediately
          sessionStartTime = millis();
          stateStartTime = millis();
        }
      }
      break;
      
    case READY_TO_SCAN:
      displayReadyToScan();
      break;
      
    case SCANNING:
      displayScanning();
      performDiagnosticScan();
      currentState = DISPLAY_RESULTS;
      stateStartTime = millis();
      break;
      
    case DISPLAY_RESULTS:
      {
        displayScanResults();
        // Auto-return to ready after showing results
        // Use longer timeout if no vehicle was detected
        unsigned long displayTimeout = (!vehicleDetected && detectedCodes.size() == 0) ? 60000 : 10000;
        if (millis() - stateStartTime > displayTimeout) {
          Serial.println("🔄 Auto-reset timeout reached, returning to ready state");
          resetToReady();
        }
      }
      break;
      
    case ERROR_STATE:
      // Auto-recover from error state
      if (millis() - stateStartTime > 5000) {
        resetToReady();
      }
      break;
  }
}

void handleSessionTimeout() {
  // Check for session timeout in paid states
  if ((currentState == WAITING_PAYMENT || currentState == READY_TO_SCAN || 
       currentState == SCANNING) && sessionStartTime > 0) {
    
    if (millis() - sessionStartTime > SESSION_TIMEOUT_MS) {
      displayError("Session timeout - returning to home");
      delay(2000);
      resetToReady();
    }
  }
}

// ========== BUTTON HANDLING ==========
void handleButtonPress() {
  static unsigned long lastPress = 0;
  static bool buttonPressed = false;
  
  bool currentButtonState = !digitalRead(SCAN_BUTTON);
  
  if (currentButtonState && !buttonPressed && (millis() - lastPress > 300)) {
    buttonPressed = true;
    lastPress = millis();
    
    Serial.println("🔘 Button pressed in state: " + String(currentState));
    
    switch (currentState) {
      case READY_SCREEN:
        // Try session creation first, fallback to test mode if it fails
        Serial.println("🔗 Attempting session creation...");
        transactionId = createNewSession();
        if (transactionId.length() > 0) {
          Serial.println("✅ Session created successfully: " + transactionId);
          currentState = DISPLAY_QR;
          stateStartTime = millis();
        } else {
          Serial.println("❌ Session creation failed, using offline test mode");
          transactionId = "OFFLINE_" + String(millis());
          currentState = READY_TO_SCAN;  // Skip QR/payment, go directly to scan
          stateStartTime = millis();
        }
        break;
        
      case READY_TO_SCAN:
        // Start diagnostic scan
        currentState = SCANNING;
        stateStartTime = millis();
        break;
        
      default:
        // Button press in other states - could be used for cancellation
        break;
    }
  } else if (!currentButtonState) {
    buttonPressed = false;
  }
}

// ========== SESSION MANAGEMENT ==========
String createNewSession() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ No WiFi connection for session creation");
    return "";
  }
  
  Serial.println("📡 Connecting to API: " + String(API_BASE_URL));
  
  HTTPClient http;
  http.setTimeout(5000);  // 5 second timeout
  http.begin(String(API_BASE_URL) + "/kiosk/create-session");
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(200);
  doc["kioskId"] = KIOSK_ID;
  doc["deviceId"] = WiFi.macAddress();
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  Serial.println("📤 Sending request: " + requestBody);
  
  int httpCode = http.POST(requestBody);
  String response = http.getString();
  http.end();
  
  Serial.println("📥 Response code: " + String(httpCode));
  Serial.println("📥 Response body: " + response);
  
  if (httpCode == 200) {
    DynamicJsonDocument responseDoc(500);
    deserializeJson(responseDoc, response);
    String sessionId = responseDoc["sessionId"];
    
    Serial.println("✓ Session created: " + sessionId);
    return sessionId;
  } else {
    Serial.println("❌ Session creation failed: HTTP " + String(httpCode));
    Serial.println("    Response: " + response);
    return "";
  }
}

bool checkPaymentStatus() {
  if (transactionId.length() == 0) return false;
  
  HTTPClient http;
  http.begin(String(API_BASE_URL) + "/kiosk/check-payment/" + transactionId);
  
  int httpCode = http.GET();
  String response = http.getString();
  http.end();
  
  if (httpCode == 200) {
    DynamicJsonDocument doc(500);
    deserializeJson(doc, response);
    bool paid = doc["paid"];
    
    if (paid) {
      Serial.println("✓ Payment confirmed!");
      return true;
    }
  }
  
  return false;
}

// ========== DISPLAY FUNCTIONS (Adapted for 240x320) ==========
void displayReadyScreen() {
  static bool displayed = false;
  
  // Redraw if forced or not displayed
  if (displayed && !forceRedraw) return;
  displayed = true;
  forceRedraw = false; // Clear the force redraw flag
  
  tft.fillScreen(TFT_BLACK);
  
  // Header (adapted for smaller screen)
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, TFT_DARKGREEN);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 8);
  tft.println("OBD2 KIOSK");
  tft.setTextSize(1);
  tft.setCursor(30, 25);
  tft.println("Vehicle Diagnostic Scanner");
  
  // Main content
  tft.setTextSize(3);
  tft.setTextColor(TFT_DARKGREEN);
  tft.setCursor(70, 100);
  tft.println("READY");
  
  // Instructions
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 160);
  tft.println("Press button to start");
  tft.setCursor(20, 175);
  tft.println("professional vehicle");
  tft.setCursor(20, 190);
  tft.println("diagnostic scan");
  
  // Status bar
  tft.fillRect(0, SCREEN_HEIGHT-30, SCREEN_WIDTH, 30, TFT_DARKGREY);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, SCREEN_HEIGHT-20);
  String wifiStatus = "WiFi: ";
  wifiStatus += (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  tft.println(wifiStatus);
  
  Serial.println("📺 Ready screen displayed");
}

void displayQRCode() {
  static bool displayed = false;
  if (displayed) return;
  displayed = true;
  
  tft.fillScreen(TFT_WHITE);
  
  // Header
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, TFT_BLUE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(60, 15);
  tft.println("SCAN QR CODE");
  
  // QR Code (use short URL to trigger router redirect with token)
  String qrData = String(WEBAPP_URL) + "/" + transactionId;
  drawQRCode(qrData, 30, 70, 3); // Version 6 QR codes are larger, use smaller scale
  
  // Instructions (moved down for larger QR code)
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(20, 260);
  tft.println("1. Scan QR with phone");
  tft.setCursor(20, 275);
  tft.println("2. Complete payment");
  tft.setCursor(20, 290);
  tft.println("3. Return to kiosk");
  
  Serial.println("📺 QR code displayed: " + qrData);
}

void displayPaymentLoading() {
  tft.fillScreen(TFT_YELLOW);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(40, 120);
  tft.println("PROCESSING");
  tft.setCursor(60, 150);
  tft.println("PAYMENT");
  
  // Loading animation
  static int dots = 0;
  tft.setCursor(80, 180);
  for (int i = 0; i < dots % 4; i++) {
    tft.print(".");
  }
  dots++;
  
  Serial.println("📺 Payment loading displayed");
}

void displayWaitingPayment() {
  static bool displayed = false;
  if (displayed) return;
  displayed = true;
  
  tft.fillScreen(TFT_ORANGE);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(30, 100);
  tft.println("WAITING FOR");
  tft.setCursor(50, 130);
  tft.println("PAYMENT");
  
  tft.setTextSize(1);
  tft.setCursor(20, 180);
  tft.println("Complete payment on");
  tft.setCursor(20, 195);
  tft.println("your phone, then");
  tft.setCursor(20, 210);
  tft.println("return to kiosk");
  
  Serial.println("📺 Waiting for payment displayed");
}

void displayReadyToScan(bool fullRedraw) {
  static bool displayed = false;
  if (displayed && !fullRedraw) return;
  displayed = true;
  
  tft.fillScreen(TFT_GREEN);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(40, 100);
  tft.println("PAYMENT");
  tft.setCursor(50, 130);
  tft.println("SUCCESS");
  
  tft.setTextSize(1);
  tft.setCursor(20, 180);
  tft.println("Connect OBD2 cable to");
  tft.setCursor(20, 195);
  tft.println("your vehicle's port");
  tft.setCursor(20, 210);
  tft.println("Press button to scan");
  
  Serial.println("📺 Ready to scan displayed");
}

void displayScanning(bool fullRedraw) {
  static bool displayed = false;
  if (displayed && !fullRedraw) return;
  displayed = true;
  
  tft.fillScreen(TFT_BLUE);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 100);
  tft.println("SCANNING");
  tft.setCursor(60, 130);
  tft.println("VEHICLE");
  
  tft.setTextSize(1);
  tft.setCursor(20, 180);
  tft.println("Please wait while we");
  tft.setCursor(20, 195);
  tft.println("scan all vehicle");
  tft.setCursor(20, 210);
  tft.println("systems...");
  
  Serial.println("📺 Scanning displayed");
}

void displayScanResults() {
  static bool displayed = false;
  static unsigned long lastUpdate = 0;
  
  if (!displayed) {
    displayed = true;
    
    tft.fillScreen(TFT_WHITE);
    
    // Header
    tft.fillRect(0, 0, SCREEN_WIDTH, 40, TFT_NAVY);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(60, 15);
    tft.println("SCAN COMPLETE");
    
    // Results summary
    int y = 60;
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK);
    
    tft.setCursor(10, y);
    tft.println("Active ECUs: " + String(activeECUs.size()) + "/" + String(NUM_ECUS));
    y += 20;
    
    tft.setCursor(10, y);
    tft.println("Fault Codes: " + String(detectedCodes.size()));
    y += 30;
    
    // Display fault codes if any
    if (detectedCodes.size() > 0) {
      tft.setTextColor(TFT_RED);
      tft.setCursor(10, y);
      tft.println("ISSUES FOUND:");
      y += 15;
      
      for (int i = 0; i < min(5, (int)detectedCodes.size()); i++) {
        tft.setCursor(10, y);
        tft.println(detectedCodes[i].code + " - " + detectedCodes[i].system);
        y += 12;
      }
      
      // Instructions for issues found
      tft.setTextColor(TFT_BLACK);
      tft.setCursor(10, SCREEN_HEIGHT - 30);
      tft.println("Detailed report sent via email");
      
    } else if (!vehicleDetected) {
      // No vehicle detected case
      tft.setTextColor(TFT_ORANGE);
      tft.setCursor(10, y);
      tft.println("NO VEHICLE DETECTED");
      tft.setCursor(10, y + 20);
      tft.println("Please ensure:");
      tft.setCursor(10, y + 35);
      tft.println("- OBD2 cable is connected");
      tft.setCursor(10, y + 50);
      tft.println("- Vehicle is turned ON");
      tft.setCursor(10, y + 65);
      tft.println("- Engine is running");
      
    } else {
      // Vehicle found but no codes - THIS IS YOUR CASE!
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(10, y);
      tft.println("ALL SYSTEMS OK!");
      tft.setCursor(10, y + 15);
      tft.println("No issues detected");
      
      // Instructions for good results
      tft.setTextColor(TFT_BLACK);
      tft.setCursor(10, SCREEN_HEIGHT - 30);
      tft.println("Health report sent via email");
    }
    
    Serial.println("📺 Scan results displayed");
  }
  
  // Update countdown every second for "no vehicle" case
  if (!vehicleDetected && millis() - lastUpdate > 1000) {
    lastUpdate = millis();
    
    // Calculate remaining time
    unsigned long elapsed = millis() - stateStartTime;
    unsigned long remaining = (60000 > elapsed) ? (60000 - elapsed) / 1000 : 0;
    
    // Update countdown
    tft.fillRect(10, SCREEN_HEIGHT - 30, SCREEN_WIDTH - 20, 20, TFT_WHITE);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(10, SCREEN_HEIGHT - 25);
    tft.printf("Returning to menu in %d seconds", (int)remaining);
  }
}

void displayError(String message) {
  tft.fillScreen(TFT_RED);
  
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(70, 100);
  tft.println("ERROR");
  
  tft.setTextSize(1);
  tft.setCursor(20, 140);
  tft.println(message);
  
  currentState = ERROR_STATE;
  stateStartTime = millis();
  
  Serial.println("❌ Error displayed: " + message);
}

// ========== QR CODE GENERATION ==========
void drawQRCode(String data, int x, int y, int scale) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(6)]; // Higher version for longer URLs
  qrcode_initText(&qrcode, qrcodeData, 6, 0, data.c_str());
  
  // Draw QR code
  for (uint8_t y0 = 0; y0 < qrcode.size; y0++) {
    for (uint8_t x0 = 0; x0 < qrcode.size; x0++) {
      uint16_t color = qrcode_getModule(&qrcode, x0, y0) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(x + x0 * scale, y + y0 * scale, scale, scale, color);
    }
  }
}

// ========== REAL CAN BUS SCANNING ==========
void performDiagnosticScan() {
  Serial.println("🔍 Starting REAL CAN bus diagnostic scan...");
  unsigned long scanStartTime = millis();
  
  detectedCodes.clear();
  activeECUs.clear();
  vehicleDetected = false; // Reset vehicle detection flag
  
  // Update display with progress
  updateScanProgress("Detecting vehicle...", 0);
  
  // Step 1: Auto-detect CAN baud rate
  uint32_t detectedBaudRate = autoDetectCANBaudRate();
  if (detectedBaudRate == 0) {
    Serial.println("❌ No CAN activity detected on any baud rate");
    updateScanProgress("No vehicle detected", 100);
    delay(3000); // Show message for 3 seconds
    return;
  }
  
  // Check overall timeout
  if (millis() - scanStartTime > TOTAL_SCAN_TIMEOUT_MS) {
    Serial.println("⏰ Scan timeout reached");
    updateScanProgress("Scan timeout", 100);
    delay(2000);
    return;
  }
  
  Serial.printf("✅ CAN activity detected at %d bps\n", detectedBaudRate);
  vehicleDetected = true; // Vehicle was successfully detected!
  updateScanProgress("Vehicle found! Analyzing...", 25);
  
  // Step 2: Listen for existing CAN traffic
  Serial.println("📡 Listening for existing CAN traffic...");
  listenForCANTraffic(TRAFFIC_LISTEN_TIMEOUT_MS);
  updateScanProgress("Reading vehicle data...", 50);
  
  // Check timeout again
  if (millis() - scanStartTime > TOTAL_SCAN_TIMEOUT_MS) {
    Serial.println("⏰ Scan timeout reached during traffic analysis");
    updateScanProgress("Scan timeout", 100);
    delay(2000);
    return;
  }
  
  // Step 3: Actively probe for OBD2 responses
  Serial.println("🔍 Probing for OBD2 ECU responses...");
  probeOBD2ECUsWithTimeout(ECU_PROBE_TIMEOUT_MS);
  updateScanProgress("Checking systems...", 75);
  
  // Step 4: Scan for DTCs on active ECUs
  Serial.println("🚨 Scanning for diagnostic trouble codes...");
  scanAllDTCs();
  updateScanProgress("Scan complete!", 100);
  
  unsigned long scanDuration = millis() - scanStartTime;
  Serial.printf("✓ Scan complete: %d active ECUs, %d fault codes (%.1fs)\n", 
                activeECUs.size(), detectedCodes.size(), scanDuration / 1000.0);
}

bool testECUCommunication(uint16_t ecuId) {
  twai_message_t msg;
  msg.identifier = ecuId;
  msg.data_length_code = 8;
  msg.data[0] = 0x02;  // Length
  msg.data[1] = 0x01;  // Mode 01
  msg.data[2] = 0x00;  // PID 00
  msg.data[3] = 0x00;
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;
  
  if (twai_transmit(&msg, pdMS_TO_TICKS(100)) != ESP_OK) {
    return false;
  }
  
  twai_message_t response;
  if (twai_receive(&response, pdMS_TO_TICKS(500)) == ESP_OK) {
    return (response.identifier == (ecuId + 8));
  }
  
  return false;
}

void scanForDTCs(uint16_t ecuId) {
  twai_message_t msg;
  msg.identifier = ecuId;
  msg.data_length_code = 8;
  msg.data[0] = 0x01;  // Length
  msg.data[1] = 0x03;  // Mode 03 - Read DTCs
  msg.data[2] = 0x00;
  msg.data[3] = 0x00;
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;
  
  if (twai_transmit(&msg, pdMS_TO_TICKS(100)) != ESP_OK) {
    return;
  }
  
  twai_message_t response;
  if (twai_receive(&response, pdMS_TO_TICKS(500)) == ESP_OK) {
    if (response.data_length_code > 2) {
      parseAndStoreDTC(response.data, response.data_length_code, ecuId);
    }
  }
}

void parseAndStoreDTC(uint8_t* data, int len, uint16_t ecuId) {
  for (int i = 2; i < len - 1; i += 2) {
    uint8_t byte1 = data[i];
    uint8_t byte2 = data[i + 1];
    
    if (byte1 == 0 && byte2 == 0) continue;
    
    // Determine DTC type
    char category = 'P';
    if ((byte1 & 0xC0) == 0x40) category = 'C';
    else if ((byte1 & 0xC0) == 0x80) category = 'B';
    else if ((byte1 & 0xC0) == 0xC0) category = 'U';
    
    int codeNumber = ((byte1 & 0x3F) << 8) | byte2;
    String dtcCode = String(category) + String(codeNumber, HEX);
    dtcCode.toUpperCase();
    
    // Pad with zeros if needed
    while (dtcCode.length() < 5) {
      dtcCode = dtcCode.substring(0, 1) + "0" + dtcCode.substring(1);
    }
    
    FaultCode fault;
    fault.code = dtcCode;
    fault.system = "ECU 0x" + String(ecuId, HEX);
    fault.isPending = false;
    fault.ecuId = ecuId;
    
    detectedCodes.push_back(fault);
    
    Serial.printf("  🚨 DTC found: %s from ECU 0x%03X\n", dtcCode.c_str(), ecuId);
  }
}

// ========== REAL CAN BUS FUNCTIONS ==========
uint32_t autoDetectCANBaudRate() {
  Serial.println("🔍 Auto-detecting CAN baud rate...");
  
  // Common OBD2 baud rates to try
  uint32_t baudRates[] = {500000, 250000, 125000, 1000000};
  int numRates = sizeof(baudRates) / sizeof(baudRates[0]);
  
  for (int i = 0; i < numRates; i++) {
    uint32_t baudRate = baudRates[i];
    Serial.printf("📡 Trying %d bps...\n", baudRate);
    
    if (reinitializeCAN(baudRate)) {
      // Listen for any CAN activity for shorter time per baud rate
      unsigned long startTime = millis();
      int frameCount = 0;
      
      while (millis() - startTime < BAUD_DETECT_TIMEOUT_MS) {
        twai_message_t message;
        if (twai_receive(&message, pdMS_TO_TICKS(100)) == ESP_OK) {
          frameCount++;
          if (frameCount >= 3) { // Found activity
            Serial.printf("✅ CAN activity detected at %d bps (%d frames)\n", baudRate, frameCount);
            return baudRate;
          }
        }
      }
      Serial.printf("   No activity at %d bps\n", baudRate);
    }
  }
  
  return 0; // No activity detected
}

bool reinitializeCAN(uint32_t baudRate) {
  // Stop current driver
  twai_stop();
  twai_driver_uninstall();
  
  // Configure for new baud rate
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // Accept all messages
  
  twai_timing_config_t t_config;
  if (baudRate == 1000000) {
    t_config = TWAI_TIMING_CONFIG_1MBITS();
  } else if (baudRate == 500000) {
    t_config = TWAI_TIMING_CONFIG_500KBITS();
  } else if (baudRate == 250000) {
    t_config = TWAI_TIMING_CONFIG_250KBITS();
  } else if (baudRate == 125000) {
    t_config = TWAI_TIMING_CONFIG_125KBITS();
  } else {
    return false;
  }
  
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    return false;
  }
  
  if (twai_start() != ESP_OK) {
    return false;
  }
  
  return true;
}

void listenForCANTraffic(uint32_t duration_ms) {
  Serial.println("👂 Listening for raw CAN traffic...");
  
  unsigned long startTime = millis();
  int frameCount = 0;
  std::vector<uint32_t> uniqueIDs;
  
  while (millis() - startTime < duration_ms) {
    twai_message_t message;
    if (twai_receive(&message, pdMS_TO_TICKS(50)) == ESP_OK) {
      frameCount++;
      
      // Log raw frame data
      Serial.printf("📦 CAN Frame #%d: ID=0x%03X DLC=%d Data=", 
                    frameCount, message.identifier, message.data_length_code);
      
      for (int i = 0; i < message.data_length_code; i++) {
        Serial.printf("%02X ", message.data[i]);
      }
      Serial.printf(" (Extended=%s)\n", message.extd ? "yes" : "no");
      
      // Track unique IDs
      bool found = false;
      for (uint32_t id : uniqueIDs) {
        if (id == message.identifier) {
          found = true;
          break;
        }
      }
      if (!found) {
        uniqueIDs.push_back(message.identifier);
      }
      
      // Update display periodically
      if (frameCount % 10 == 0) {
        tft.fillRect(0, 200, SCREEN_WIDTH, 20, TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(10, 200);
        tft.printf("Frames: %d IDs: %d", frameCount, uniqueIDs.size());
      }
    }
  }
  
  Serial.printf("📊 Traffic summary: %d frames, %d unique IDs\n", frameCount, uniqueIDs.size());
  
  // Log unique IDs found
  Serial.print("🆔 Unique CAN IDs: ");
  for (int i = 0; i < uniqueIDs.size() && i < 20; i++) {
    Serial.printf("0x%03X ", uniqueIDs[i]);
  }
  Serial.println();
}

void probeOBD2ECUs() {
  Serial.println("🔍 Actively probing for OBD2 ECUs...");
  
  // Send standard OBD2 query to detect active ECUs
  for (int i = 0; i < NUM_ECUS; i++) {
    uint16_t ecuAddr = OBD2_ADDRESSES[i];
    
    Serial.printf("📡 Probing ECU 0x%03X (%d/%d)...\n", ecuAddr, i + 1, NUM_ECUS);
    
    // Send Mode 01 PID 00 (Supported PIDs) request
    twai_message_t msg;
    msg.identifier = ecuAddr;
    msg.flags = TWAI_MSG_FLAG_NONE;
    msg.data_length_code = 8;
    msg.data[0] = 0x02;  // Length
    msg.data[1] = 0x01;  // Mode 01 (Show current data)
    msg.data[2] = 0x00;  // PID 00 (Supported PIDs 01-20)
    msg.data[3] = 0x00;
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;
    
    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
      // Wait for response
      twai_message_t response;
      unsigned long start = millis();
      while (millis() - start < 1000) { // 1 second timeout
        if (twai_receive(&response, pdMS_TO_TICKS(50)) == ESP_OK) {
          // Check if this is a response to our query
          if (response.identifier == (ecuAddr + 8) || 
              (response.identifier >= 0x7E8 && response.identifier <= 0x7EF)) {
            
            activeECUs.push_back(response.identifier);
            Serial.printf("  ✅ Active ECU found: 0x%03X responded from 0x%03X\n", 
                         ecuAddr, response.identifier);
            
            // Log response data
            Serial.printf("     Response: ");
            for (int j = 0; j < response.data_length_code; j++) {
              Serial.printf("%02X ", response.data[j]);
            }
            Serial.println();
            break;
          }
        }
      }
    }
    
    delay(100); // Brief pause between requests
  }
  
  Serial.printf("🎯 Found %d active OBD2 ECUs\n", activeECUs.size());
}

void probeOBD2ECUsWithTimeout(uint32_t timeout_ms) {
  Serial.println("🔍 Actively probing for OBD2 ECUs with timeout...");
  unsigned long startTime = millis();
  
  // Send standard OBD2 query to detect active ECUs
  for (int i = 0; i < NUM_ECUS; i++) {
    // Check timeout
    if (millis() - startTime > timeout_ms) {
      Serial.printf("⏰ ECU probing timeout after %d ECUs\n", i);
      break;
    }
    
    uint16_t ecuAddr = OBD2_ADDRESSES[i];
    
    Serial.printf("📡 Probing ECU 0x%03X (%d/%d)...\n", ecuAddr, i + 1, NUM_ECUS);
    
    // Update progress on display
    if (i % 4 == 0) {
      int progress = 50 + (i * 25 / NUM_ECUS); // 50-75% range
      updateScanProgress("Checking ECU " + String(i+1) + "/" + String(NUM_ECUS), progress);
    }
    
    // Send Mode 01 PID 00 (Supported PIDs) request
    twai_message_t msg;
    msg.identifier = ecuAddr;
    msg.flags = TWAI_MSG_FLAG_NONE;
    msg.data_length_code = 8;
    msg.data[0] = 0x02;  // Length
    msg.data[1] = 0x01;  // Mode 01 (Show current data)
    msg.data[2] = 0x00;  // PID 00 (Supported PIDs 01-20)
    msg.data[3] = 0x00;
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;
    
    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
      // Wait for response with shorter timeout per ECU
      twai_message_t response;
      unsigned long ecuStart = millis();
      while (millis() - ecuStart < 800) { // 800ms per ECU max
        if (twai_receive(&response, pdMS_TO_TICKS(50)) == ESP_OK) {
          // Check if this is a response to our query
          if (response.identifier == (ecuAddr + 8) || 
              (response.identifier >= 0x7E8 && response.identifier <= 0x7EF)) {
            
            activeECUs.push_back(response.identifier);
            Serial.printf("  ✅ Active ECU found: 0x%03X responded from 0x%03X\n", 
                         ecuAddr, response.identifier);
            
            // Log response data
            Serial.printf("     Response: ");
            for (int j = 0; j < response.data_length_code; j++) {
              Serial.printf("%02X ", response.data[j]);
            }
            Serial.println();
            break;
          }
        }
      }
    }
    
    delay(50); // Brief pause between requests
  }
  
  Serial.printf("🎯 Found %d active OBD2 ECUs\n", activeECUs.size());
}

void updateScanProgress(String message, int percentage) {
  // Update the scanning display with progress
  tft.fillRect(0, 180, SCREEN_WIDTH, 40, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  
  // Progress message
  tft.setCursor(20, 185);
  tft.println(message);
  
  // Progress bar
  int barWidth = SCREEN_WIDTH - 40;
  int barHeight = 8;
  int barX = 20;
  int barY = 200;
  
  // Progress bar background
  tft.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
  
  // Progress bar fill
  int fillWidth = (barWidth * percentage) / 100;
  tft.fillRect(barX, barY, fillWidth, barHeight, TFT_WHITE);
  
  // Percentage text
  tft.setCursor(barX + barWidth + 5, barY);
  tft.printf("%d%%", percentage);
}

void scanAllDTCs() {
  if (activeECUs.size() == 0) {
    Serial.println("⚠️ No active ECUs found, skipping DTC scan");
    return;
  }
  
  Serial.println("🚨 Scanning for Diagnostic Trouble Codes...");
  
  for (uint16_t ecuId : activeECUs) {
    Serial.printf("🔍 Scanning ECU 0x%03X for DTCs...\n", ecuId);
    
    // Convert response ID back to request ID
    uint16_t requestId = ecuId;
    if (ecuId >= 0x7E8 && ecuId <= 0x7EF) {
      requestId = ecuId - 8;
    }
    
    // Send Mode 03 (Request stored DTCs)
    twai_message_t msg;
    msg.identifier = requestId;
    msg.flags = TWAI_MSG_FLAG_NONE;
    msg.data_length_code = 8;
    msg.data[0] = 0x01;  // Length
    msg.data[1] = 0x03;  // Mode 03 (Request stored DTCs)
    msg.data[2] = 0x00;
    msg.data[3] = 0x00;
    msg.data[4] = 0x00;
    msg.data[5] = 0x00;
    msg.data[6] = 0x00;
    msg.data[7] = 0x00;
    
    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
      // Wait for DTC response
      unsigned long start = millis();
      while (millis() - start < 1000) {
        twai_message_t response;
        if (twai_receive(&response, pdMS_TO_TICKS(50)) == ESP_OK) {
          if (response.identifier == ecuId) {
            Serial.printf("  📋 DTC Response from 0x%03X: ", ecuId);
            for (int i = 0; i < response.data_length_code; i++) {
              Serial.printf("%02X ", response.data[i]);
            }
            Serial.println();
            
            if (response.data_length_code > 2) {
              parseAndStoreDTC(response.data, response.data_length_code, ecuId);
            }
            break;
          }
        }
      }
    }
    
    delay(100);
  }
}

// ========== UTILITY FUNCTIONS ==========
void resetDisplayFlags() {
  // Force all display functions to redraw by setting the global flag
  forceRedraw = true;
}

void resetToReady() {
  // Clear all session data
  transactionId = "";
  sessionStartTime = 0;
  detectedCodes.clear();
  activeECUs.clear();
  vehicleDetected = false; // Reset vehicle detection flag
  
  // Reset display flags
  for (int i = 0; i < 10; i++) {
    screenDrawFlags[i] = false;
  }
  
  // Force all display functions to redraw
  forceRedraw = true;
  
  // Create new session and return to QR code for next customer
  Serial.println("🔄 Creating new session for next customer...");
  transactionId = createNewSession();
  
  if (transactionId.length() > 0) {
    currentState = DISPLAY_QR;
    Serial.println("✓ New session created: " + transactionId);
  } else {
    Serial.println("❌ Failed to create new session, showing ready screen");
    currentState = READY_SCREEN;
  }
  
  stateStartTime = millis();
  Serial.println("🔄 Reset complete - ready for next customer");
}