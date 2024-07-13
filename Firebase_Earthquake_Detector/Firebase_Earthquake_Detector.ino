
/*/ -------------------- Earthquake Warning System Web Server -------------------- /*/
// Code by Michael Fernandez - 212310060
// June 2024

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Firebase_ESP_Client.h>
// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// ----- Firebase config ----- //
#define USER_EMAIL "212310060@student.ibik.ac.id"
#define USER_PASSWORD "123456"
#define DATABASE_URL "https://earthquake-detector-c430e-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define API_KEY "AIzaSyBYG4IB9mdzJ5XPYrp8eFLO7x7fTE2cy10"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String uid;
String mainPath;
FirebaseJson jsonSen;
FirebaseJson jsonTim;

// ----- WiFi & time config ----- //
char* SSID = "Mike's WiFi";
char* WiFi_Password = "Testing42";

#define NTP_SERVER      "pool.ntp.org"
#define UTC_OFFSET      25200   // offset to GMT+7
#define UTC_OFFSET_DST  0
struct tm timeinfo;

// ----- ESP32 pin config ----- //
#define LED_indicator 2
#define Buzzer        5
#define CUSTOM_SCL  18
#define CUSTOM_SDA  19

// ----- declaration for MPU6050 sensor logic ----- //
Adafruit_MPU6050 mpu;
const unsigned int stnbyFre = 1000; // loop/sampling frequency when standby
unsigned int loopFre = stnbyFre;
const unsigned int quakeFre = 500;  // loop/sampling frequency when earthquake
unsigned long lastQuake;
const unsigned int timeTol = 2000;  // time Tolerance (persistent alert)
unsigned int selfCalib = timeTol + 3000;  
            /* ^ time interval to check for self calibration if 
                 there is no movement (cannot be lower than timeTol) */
const unsigned int selfCalibReset = selfCalib;
const float moveTol = 0.1;          // movement Tolerance (sensor sensitivity)
float Xoffset;
float Yoffset;
float Zoffset;
float XmaxLimit;
float YmaxLimit;
float ZmaxLimit;
float XminLimit;
float YminLimit;
float ZminLimit;
float lastX;
float lastY;
float lastZ;
float Xvalue;
float Yvalue;
float Zvalue;
unsigned long lastMili;
unsigned long currentMili;
unsigned long duraInterval;
unsigned int duration;
bool duraReset = true;
bool quake = false;
bool alert = false;
bool savingSensor = true;

unsigned int stnby;   // for monitoring serial 


/* ---------- function for setup ---------- */
void initWifi() {
  Serial.println("Connecting to WiFi");
  WiFi.begin(SSID, WiFi_Password);
  // Searching wifi connection LED indicator
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(LED_indicator, HIGH);
    delay(500);
    digitalWrite(LED_indicator, LOW);
    delay(500);
  }
  // Connected wifi LED indicator
  digitalWrite(LED_indicator, HIGH);
  delay(2000);
  digitalWrite(LED_indicator, LOW);
  delay(250);
  digitalWrite(LED_indicator, HIGH);
  delay(250);
  digitalWrite(LED_indicator, LOW);

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void initSensor() {
  if (!mpu.begin()) {
		Serial.println("Failed to find MPU6050 chip");
		while (1) {
		  delay(10);
		}
	}
	Serial.println("MPU6050 Found!");
	// set accelerometer range to +-8G
	mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  // set gyro range to +- 500 deg/s
	mpu.setGyroRange(MPU6050_RANGE_500_DEG);
	// set filter bandwidth to 21 Hz
	mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void calibSensor() {
  Serial.println("Calibrating MPU6050");
  digitalWrite(LED_indicator, HIGH);
  delay(1500);
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  Xoffset = a.acceleration.x;
  Yoffset = a.acceleration.y;
  Zoffset = a.acceleration.z;
  XmaxLimit = a.acceleration.x + moveTol;
  YmaxLimit = a.acceleration.y + moveTol;
  ZmaxLimit = a.acceleration.z + moveTol;
  XminLimit = a.acceleration.x - moveTol;
  YminLimit = a.acceleration.y - moveTol;
  ZminLimit = a.acceleration.z - moveTol;
  digitalWrite(LED_indicator, LOW);
  Serial.println("Calibration done");
}


/* ---------- logic/loop function ---------- */
void printLocalTime() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
  }else{
  // Format date and time as strings
  char timeStr[9];   // "HH:MM:SS"
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  char dateStr[11];  // "DD/MM/YYYY"
  strftime(dateStr, sizeof(dateStr), "%d/%m/%Y", &timeinfo);

    jsonTim.set("/Date", dateStr);
    jsonTim.set("/Clock", timeStr);
    jsonTim.set("/Duration", duration);
    if (Firebase.RTDB.setJSON(&fbdo, mainPath + "/Time", &jsonTim)) {
      Serial.println("Time sent");
    } else {
      Serial.println();
      Serial.println("Time failed: " + fbdo.errorReason());
      Serial.println();
    }    
    Serial.println(&timeinfo, "%H:%M:%S");
    Serial.println(&timeinfo, "%d/%m/%Y");
  }
}

void sendData() {
  if (!alert) {
    alert = true;
    Firebase.RTDB.setBool(&fbdo, mainPath + "/Alert", alert);
    Serial.print("Alert = ");
    Serial.println(alert);
  }
  jsonSen.set("/X", Xvalue - Xoffset);
  jsonSen.set("/Y", Yvalue - Yoffset); 
  jsonSen.set("/Z", Zvalue - Zoffset); 
  if (Firebase.RTDB.setJSON(&fbdo, mainPath + "/SensorValue", &jsonSen)) {
    Serial.println("sensor sent");
  } else {
    Serial.println();
    Serial.println("sensor failed: " + fbdo.errorReason());
    Serial.println();
  } 
  Serial.print("Acceleration X: ");
  Serial.print(Xvalue - Xoffset);
  Serial.print(", Y: ");
  Serial.print(Yvalue - Yoffset);
  Serial.print(", Z: ");
  Serial.print(Zvalue - Zoffset);
  Serial.println(" m/s^2");
  Serial.println(duration);
  printLocalTime();
}

void getDuration() {
  currentMili = millis();
  if (duraReset) {
    duraInterval = 0;
    duraReset = false;
  } else {
    duraInterval = currentMili - lastMili;
  }
  
  duration += duraInterval;
  lastMili = currentMili;
}

void checkMovement() {
  if (savingSensor){
    lastX = Xvalue;
    lastY = Yvalue;
    lastZ = Zvalue;
    savingSensor = false;
    Serial.println("saving, savingSensor = false");
    Serial.println("");
  }else if (!savingSensor &&
      (lastX - Xvalue <= moveTol && lastX - Xvalue >= -moveTol) &&
      (lastY - Yvalue <= moveTol && lastY - Yvalue >= -moveTol) &&
      (lastZ - Zvalue <= moveTol && lastZ - Zvalue >= -moveTol)) {
    Serial.println("no movement detected");
    reseting();
    calibSensor();
  }else if (!savingSensor){
    lastX = Xvalue;
    lastY = Yvalue;
    lastZ = Zvalue;
    savingSensor = false;
    Serial.println("not calibrated, saving, savingSensor = false");
    Serial.println("");
  }
}

void reseting() {
  digitalWrite(Buzzer, LOW);
  loopFre = stnbyFre;
  quake = false;
  duration = 0;
  duraReset = true;
  selfCalib = selfCalibReset;
  savingSensor = true;

  alert = false;
  Firebase.RTDB.setBool(&fbdo, mainPath + "/Alert", alert);
  Serial.print("Alert = ");
  Serial.println(alert);
}


// for visualizing serial monitor flow/refresh rate
void monitorSerial() {
  if (quake) {
    stnby = 0;
  }else{
    Serial.print("standby ");
    Serial.println(stnby);
    stnby++;
  }
}


/* -------------------- main code -------------------- */
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  delay(1000);

  Wire.begin(CUSTOM_SDA, CUSTOM_SCL);
  pinMode(LED_indicator, OUTPUT);
  pinMode(Buzzer, OUTPUT);

  initWifi();
  // get time
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  Serial.println("Connecting to NTP");
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("Connected to NTP");
  Serial.println(&timeinfo, "%H:%M:%S");
  Serial.println(&timeinfo, "%d/%m/%Y");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;
  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);
  mainPath = "UserID: " + uid;

  // initialize MPU6050
  initSensor();
  // MPU6050 calibration & offset
  calibSensor();
}

void loop() {
  delay(loopFre);
  sensors_event_t a, g, temp;
	mpu.getEvent(&a, &g, &temp);
  Xvalue = a.acceleration.x;
  Yvalue = a.acceleration.y;
  Zvalue = a.acceleration.z;

  if ((Xvalue >= XmaxLimit || Xvalue <= XminLimit) || 
      (Yvalue >= YmaxLimit || Yvalue <= YminLimit) || 
      (Zvalue >= ZmaxLimit || Zvalue <= ZminLimit)) {
    quake = true;
    loopFre = quakeFre;
    lastQuake = millis();
  }else if (lastQuake < millis() - timeTol && quake) {
    reseting();
  }

  if (quake) {
    getDuration();

    sendData();

    if (!digitalRead(Buzzer)) {
      digitalWrite(Buzzer, HIGH);
    }else{
      digitalWrite(Buzzer, LOW);
    }

    if (duration >= selfCalib) {
      selfCalib += selfCalibReset;
      checkMovement();
    }
  }

  monitorSerial();
}