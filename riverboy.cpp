#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// --- CONFIGURATION ---
#define WIFI_SSID "TMLaptop"
#define WIFI_PASSWORD "saa5181k"
#define DATABASE_URL "trial-b43bd-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyDml9d0sjRCWK9JxleB9bc2lhP-7L3F1Hg"

// --- PIN CONFIGURATION ---
#define PH_PIN 34         
#define TURB_PIN 32       
#define TDS_PIN 35        
#define TDS_PWR_PIN 25    

// --- CALIBRATION CONSTANTS ---
float neutralVoltage = 2.50; 
float phSlope = 4.5;         
float volt_clean = 2.12;     
float volt_cloudy = 1.42;    

// Firebase Objects
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdo; 

// --- HELPER FUNCTIONS ---

void initWiFi(){
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected!");
}

float getTDS() {
  digitalWrite(TDS_PWR_PIN, HIGH); 
  delay(1000); 
  long sum = 0;
  for(int i = 0; i < 30; i++) {
    sum += analogRead(TDS_PIN);
    delay(10);
  }
  digitalWrite(TDS_PWR_PIN, LOW); 
  float voltage = ((float)sum / 30.0) * 3.3 / 4095.0;
  return (133.42 * pow(voltage, 3) - 255.86 * pow(voltage, 2) + 857.39 * voltage) * 0.5;
}

float getPH() {
  digitalWrite(TDS_PWR_PIN, LOW);
  delay(2500); // Settling time for isolation
  
  for(int i = 0; i < 20; i++) { analogRead(PH_PIN); delay(5); } // Flush ADC

  int buffer_arr[10], temp;
  unsigned long int avgval = 0;
  for (int i = 0; i < 10; i++) {
    buffer_arr[i] = analogRead(PH_PIN);
    delay(30);
  }
  // Sort
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buffer_arr[i] > buffer_arr[j]) {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }
  for (int i = 2; i < 8; i++) avgval += buffer_arr[i];
  float volt = (float)avgval * 3.3 / 4095.0 / 6.0;
  return constrain(7.0 + ((volt - neutralVoltage) * phSlope), 0.0, 14.0);
}

float getTurbidity() {
  long sum = 0;
  for(int i = 0; i < 30; i++) { sum += analogRead(TURB_PIN); delay(10); }
  float voltage = ((float)sum / 30.0) * 3.3 / 4095.0;
  return constrain(((volt_clean - voltage) / (volt_clean - volt_cloudy)) * 100.0, 0.0, 100.0);
}

void uploadRiverData(float ph, float turb, float tds) {
  if (Firebase.ready()) {
    // 1. Get the current Date and Time
    struct tm timeinfo;
    char dateTimeNow[25];
    if(!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
      return; 
    }
    strftime(dateTimeNow, sizeof(dateTimeNow), "%Y-%m-%d %H:%M:%S", &timeinfo);
    String dt = String(dateTimeNow);

    // 2. Calculate Health Score
    float score_ph = (100.0 - (abs(7.0 - ph) * 25.0)) * 0.30;
    float score_tds = (100.0 - (tds / 15.0)) * 0.35;
    float score_turb = (100.0 - (turb * 2.0)) * 0.35;
    int finalScore = constrain((int)(score_ph + score_tds + score_turb), 0, 100);

    FirebaseJson json;
    json.set("ph", ph);
    json.set("tds", (int)tds);
    json.set("turbidity", (int)turb);
    json.set("score", finalScore);
    json.set("dateTime", dt);

    // 3. Update Live Monitor
    Firebase.RTDB.setJSON(&fbdo, "Current", &json);

    // 4. Update History (Modified to use DateTime as the KEY)
    // This creates: History / 2026-04-06 00:30:00 / {data}
    String historyPath = "History/" + dt;
    if (Firebase.RTDB.setJSON(&fbdo, "History", &json)) {
      Serial.println("Historical Data Logged at " + dt);
    }
  }
}

// --- MAIN SETUP ---

void setup() {
  Serial.begin(115200);
  
  analogReadResolution(12);       
  analogSetAttenuation(ADC_11db); 
  
  pinMode(PH_PIN, INPUT);
  pinMode(TURB_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);
  pinMode(TDS_PWR_PIN, OUTPUT);
  digitalWrite(TDS_PWR_PIN, LOW); 
configTime(28800, 0, "pool.ntp.org", "time.nist.gov");
  initWiFi();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("Firebase Auth OK");
  } else {
    Serial.printf("Auth Error: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// --- MAIN LOOP ---

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    initWiFi();
  }

  Serial.println("--- Starting Measurement Cycle ---");
  
  float currentTds = getTDS();
  float currentPh = getPH();
  float currentTurb = getTurbidity();

  // Print to Serial for local check
  Serial.print("pH: "); Serial.print(currentPh, 2);
  Serial.print(" | Turb: "); Serial.print(currentTurb, 1);
  Serial.print(" | TDS: "); Serial.println(currentTds, 0);

  // Upload to Firebase
  uploadRiverData(currentPh, currentTurb, currentTds);

  Serial.println("--- Cycle Complete ---\n");
  
  delay(10000); // 10-second gap between uploads to avoid spamming the DB
}
