EXPERIMENTAL DEEP SCAN - QUICK START GUIDE
==========================================

The experimental deep scan files are now added to your project but are NOT compiled by default.
This ensures your existing working ESP32 code remains completely untouched.

TO ENABLE EXPERIMENTAL DEEP SCAN:
=================================

1. Add this line to the top of your main.cpp (after the existing includes):
   
   #define ENABLE_DEEP_SCAN
   #include "experimental_deep_scan.h"

2. Add a global deep scan instance:
   
   ExperimentalDeepScan deepScan;

3. Initialize in your setup():
   
   void setup() {
     // Your existing setup code...
     
     // Initialize deep scan (only if enabled)
     #ifdef ENABLE_DEEP_SCAN
     if (deepScan.initialize()) {
       Serial.println("🔬 Deep scan module ready");
       deepScan.detectManufacturer(); // Auto-detect for proprietary PIDs
     }
     #endif
   }

4. Use in your scanning function:
   
   void performAdvancedScan() {
     #ifdef ENABLE_DEEP_SCAN
     // Get battery health for hybrids
     BatteryHealth battery = deepScan.getBatteryHealth();
     if (battery.health != "UNKNOWN") {
       Serial.println("Battery SOC: " + String(battery.stateOfCharge) + "%");
       Serial.println("Battery Health: " + battery.health);
     }
     
     // Check oil life
     OilLifeData oil = deepScan.getOilLife();
     if (oil.condition != "UNKNOWN") {
       Serial.println("Oil Life: " + String(oil.remainingPercent) + "%");
       Serial.println("Condition: " + oil.condition);
     }
     
     // Test all sensors
     std::vector<SensorReading> sensors = deepScan.testAllSensors();
     for (auto& sensor : sensors) {
       if (sensor.status != "NO_DATA") {
         Serial.println(sensor.name + ": " + String(sensor.value) + " " + sensor.unit);
       }
     }
     #endif
   }

WHAT YOU GET:
============

- Battery health & SOC for hybrid/EV vehicles
- Oil life monitoring with manufacturer-specific access
- Individual sensor testing with pass/fail analysis
- Real-time data streaming
- Tire pressure monitoring (Ford vehicles)
- Advanced ECU communication
- Manufacturer-specific proprietary PID access

YOUR EXISTING CODE:
==================

✅ Completely safe and unchanged
✅ Will compile and work exactly as before
✅ No modifications needed to your working kiosk
✅ Deep scan is 100% optional

FILES ADDED:
============

- experimental_deep_scan.h          # Header with all definitions
- experimental_deep_scan.cpp        # Implementation (conditional compilation)
- deep_scan_integration_example.cpp # Examples (not compiled by default)
- EXPERIMENTAL_DEEP_SCAN.md        # Full documentation
- README_DEEP_SCAN.txt              # This quick start guide

SAFETY:
=======

The deep scan module:
- Uses the same CAN bus setup as your existing code
- Fails gracefully if features are not supported
- Does not modify any existing functionality
- Can be completely disabled by not defining ENABLE_DEEP_SCAN

TO TEST:
========

1. Enable compilation with #define ENABLE_DEEP_SCAN
2. Upload to your ESP32
3. Check serial monitor for "🔬 Deep scan module ready"
4. Call performAdvancedScan() to see what's available for your test vehicle

Your existing OBD2 kiosk functionality remains 100% intact!