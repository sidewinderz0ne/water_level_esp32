
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Ticker.h>  // Include the Ticker library for ESP32
int csPin = 5;

#include <SD.h>
#include <RTClib.h>
#include <Timer.h>

RTC_DS3231 rtc;

#define WATCHDOG_TIMEOUT 60
volatile int watchdogMin = 0;

int watchdogTimer = 11;
String ssid = "SRS";
String password = "SRS@2023";
int delaySend = 3000;
int delayWriteData = 30000;
int delayMeasure = 1000;
File myFile;
int id;
Timer sendTimer, getTimer, measureTimer;
Ticker watchdogTicker;
String payload;

#define SOUND_SPEED 0.034

int sensorD, sensorN, minimum, maximum, cal, idwl, debug;
const int distanceAvgCount = 30;
int distanceCmArr[distanceAvgCount] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const int ultrasonic[2] = {2, 4};

void resetWatchdog();
String getDataSD(String fileName);
float getDistance(const int *ultrasonic, float cal);
float getDistanceAct(const int *ultrasonic, float cal);
void sendData();
void connectWiFi();
void deleteTopLine();
int getAvgDistance();
String getTime();
void writeData();
void measureData();
void appendFile(fs::FS &fs, const char *path, String message);

void resetWatchdog() {
  watchdogMin++;
  if (watchdogMin >= watchdogTimer) {
    esp_cpu_reset(0);
    ESP.restart();
  }
}

String getDataSD(String fileName){
  File myFile;                    // create a file object
  String text = "";
  myFile = SD.open("/"+fileName);  // open the file
  if (myFile) {
    text = myFile.readStringUntil('\n');  // read the first line of the file
    Serial.print(fileName);
    Serial.print(" : [");
    Serial.print(text);
    Serial.println("]");
    myFile.close();                           // close the file
  } else {
    Serial.println("Error opening file!");
  }
  return text;
}

void setup() {
  Serial.begin(115200);
  watchdogTicker.attach(WATCHDOG_TIMEOUT, resetWatchdog);
  if (!SD.begin(csPin)) {
    Serial.println("Card failed, or not present");
  }else{
    Serial.println("Card initialized successfully");
  }

  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC");
    //    Serial.flush();
    //    abort();
  }
  if (rtc.lostPower())
  {
    Serial.println("RTC lost power, let's set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(ultrasonic[0],OUTPUT);
  pinMode(ultrasonic[1],INPUT);

  ssid = getDataSD("ssid.txt");
  password = getDataSD("password.txt");
  minimum = getDataSD("minimum.txt").toInt();
  maximum = getDataSD("maximum.txt").toInt();
  sensorD = getDataSD("sensorD.txt").toInt();
  sensorN = getDataSD("sensorN.txt").toInt();
  cal = getDataSD("cal.txt").toInt();
  idwl = getDataSD("idwl.txt").toInt();
  debug = getDataSD("debug.txt").toInt();
  delaySend = getDataSD("delaySend.txt").toInt()*1000;
  delayWriteData = getDataSD("delayWriteData.txt").toInt()*1000;
  delayMeasure = getDataSD("delayMeasure.txt").toInt()*1000;


  // Connect to Wi-Fi network with SSID and password
  Serial.println();

  //pengukuran awal
  for(int i = 0; i < distanceAvgCount+5; i++){


    Serial.print("Pengukuran ke: ");
    Serial.print(i);
    Serial.print(" | average ");
    Serial.print(distanceAvgCount);
    Serial.print(" distance: ");
    int avgDistance = getAvgDistance();
    Serial.println(avgDistance);
    delay(200);
  }

  getTimer.every(delayWriteData, writeData);
  sendTimer.every(delaySend, sendData);
  measureTimer.every(delayMeasure, measureData);
}

void measureData(){
  getAvgDistance();
}

void writeData(){
  int avgDistance = getAvgDistance();
  String timenow = getTime();
  String payload = "lvl_in=" + String(avgDistance) + "&d=" + timenow + "&idwl=" + idwl;
  appendFile(SD, "/data.txt", String(payload));
}

void sendData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    connectWiFi();
  }

  String rawText;
  myFile = SD.open("/data.txt");  // open the file
  if (myFile) {
    rawText = myFile.readStringUntil('\n');  // read the first line of the file
    myFile.close();                          // close the file
    if (rawText != "") {

      WiFiClient client;
      HTTPClient http;

      if (payload != rawText) {
        Serial.println("data: " + rawText);
        // Send the POST request
        http.begin(client, "http://srs-ssms.com/post-wl-data.php");
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        int httpCode = http.POST(rawText);

        // Check the response
        if (httpCode == 200) {
          deleteTopLine();
          String response = http.getString();
          Serial.println("HTTP response: " + response);
          payload = rawText;
        } else if (httpCode > 0) {
          String response = http.getString();
          Serial.println("HTTP error response: " + response);
        } else {
          Serial.print("HTTP error: ");
          Serial.println(httpCode);
        }
        http.end();
      } else {
        Serial.println("Data masih sama!");
      }
    }
  } else {
    Serial.println("Error opening file!");
  }
}

void connectWiFi() {
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to ");
  Serial.println(ssid);
  unsigned long startTime = millis(); // Record the start time

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000) { // Check both connection and timeout
    delay(1000);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet mask: ");
    Serial.println(WiFi.subnetMask());
  } else {
    Serial.println("\nFailed to connect to WiFi within the timeout period.");
  }
}


void deleteTopLine() {
  File originalFile, newFile;

  originalFile = SD.open("/data.txt", FILE_READ);

  // create new file for writing
  newFile = SD.open("/new_file.txt", FILE_WRITE);

  // read and discard the first line
  originalFile.readStringUntil('\n');

  // read each subsequent line and write to new file
  while (originalFile.available()) {
    String line = originalFile.readStringUntil('\n');
    // check if the line is not empty before writing to the new file
    if (line.length() > 0) {
      newFile.println(line);
    }
  }

  // close both files
  originalFile.close();
  newFile.close();

  // remove original file
  SD.remove("/data.txt");

  // rename new file to original file name
  SD.rename("/new_file.txt", "/data.txt");
}

float getDistance(const int *ultrasonicS, float cal)
{
  float dt = 0;
  dt = (getDistanceAct(ultrasonicS, cal) - sensorN) * -1;
  return dt;
}

float getDistanceAct(const int *ultrasonicS, float cal)
{
  long duration;
  float dt = 0;
  // Clears the trigPin
  digitalWrite(ultrasonicS[0], LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(ultrasonicS[0], HIGH);
  delayMicroseconds(10);
  digitalWrite(ultrasonicS[0], LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(ultrasonicS[1], HIGH);
  duration *SOUND_SPEED / 2;

  dt = duration * SOUND_SPEED / 2;
  dt += cal;
  return dt;
}

int getAvgDistance(){
  for (int j = distanceAvgCount - 1; j > 0; j--) {
    distanceCmArr[j] = distanceCmArr[j - 1];
  }
  distanceCmArr[0] = getDistance(ultrasonic, cal);

  int sum = 0;

  // Calculate the sum of all elements in the array
  for (int i = 0; i < distanceAvgCount; i++) {
    sum += distanceCmArr[i];
  }

  // Calculate the average
  // Serial.print("sum: ");
  // Serial.println(sum);
  // // Calculate the average
  // Serial.print("distanceAvgCount: ");
  // Serial.println(distanceAvgCount);

  int avgDist = sum / distanceAvgCount;
  // Serial.println(avgDist);

  return avgDist;
}

void loop() {
  getTimer.update();
  sendTimer.update();
  measureTimer.update();
  watchdogMin = 0;
}

String getTime()
{
  DateTime now = rtc.now();
  String timeS = String(now.year(), DEC) + "-" + String(now.month(), DEC) + "-" + String(now.day(), DEC) + " " + String(now.hour(), DEC) + ":" + String(now.minute(), DEC) + ":" + String(now.second(), DEC);
  return timeS;
}

void appendFile(fs::FS &fs, const char *path, String message)
{
  Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    Serial.println("- failed to open file for appending");
    return;
  }
  if (file.println(message))
  {
    Serial.println("- message appended");

    //getDataSD(path);
  }
  else
  {
    Serial.println("- append failed");
  }
  file.close();
}
