//SHT40 ESP266 LCD16x2 + RH + Telegram RH + Hourly Time
#include <Adafruit_SHT4x.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>

// -------------------- Google Sheet --------------------
#define GSHEET_URL "https://script.google.com/macros/s/AKfycbw9rd4NicSGTlg2-uuP2cfq73wnNhKeixVGcIUte1iTT-5pijdgeaYQiqotdYSqZ67JHg/exec?sheetId=1bdzraltDO3EhQxMEAKAyuqCRWRm5RRMpXfe_mr96kkk"
#define SHEET_NAME "Server1"

// -------------------- Telegram --------------------
#define TELEGRAM_BOT_TOKEN "7738784953:AAFbwR8LDIEbwc30OnZbufRqvGyMXweZ8HQ"
#define TELEGRAM_CHAT_ID "-1002453912581"
//#define TELEGRAM_BOT_TOKEN "8033360987:AAGCehFEygzGkC2vad_UXIVTPX_Mzxj-bEg"
//#define TELEGRAM_CHAT_ID "7266215306"

// -------------------- SHT40 Sensor --------------------
Adafruit_SHT4x sht4 = Adafruit_SHT4x();

// -------------------- LCD I2C --------------------
#define I2C_SDA D6   // GPIO12
#define I2C_SCL D1   // GPIO5
#define LCD_ADDRESS 0x27
#define LCD_COLS    16
#define LCD_ROWS    2
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

unsigned long lastLcdUpdate = 0;
const unsigned long lcdUpdateInterval = 1000;

float lcdTemp = NAN;
float lcdHumidity = NAN;
unsigned long lastLcdTempRead = 0;
const unsigned long lcdTempReadInterval = 1000;

const unsigned long normalInterval = 300000;
const unsigned long alertInterval  = 60000;
unsigned long lastSensorTime = 0;
unsigned long lastTelegramTime = 0;

float latestTemp = NAN;
float latestHumidity = NAN;

const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

int lastNotifiedHour = -1;
bool wasOutOfRange = false;

String centerText(String text, int width) {
  int len = text.length();
  if (len >= width) return text.substring(0, width);
  int left = (width - len) / 2;
  int right = width - len - left;
  String spaces = "";
  for (int i = 0; i < left; i++) spaces += " ";
  String spacesR = "";
  for (int i = 0; i < right; i++) spacesR += " ";
  return spaces + text + spacesR;
}

void showOnLCD(float temp, float rh, struct tm* timeinfo) {
  lcd.clear();
  String line1 = "T:" + String(temp, 1) + (char)223 + "C RH:" + String(rh, 0) + "%";
  lcd.setCursor(0, 0);
  lcd.print(centerText(line1, 16));

  String line2 = "";
  if (timeinfo != NULL) {
    if (timeinfo->tm_mday < 10) line2 += "0";
    line2 += String(timeinfo->tm_mday) + "/";
    if (timeinfo->tm_mon+1 < 10) line2 += "0";
    line2 += String(timeinfo->tm_mon+1) + "/";
    line2 += String(timeinfo->tm_year + 1900);
    line2 += " ";
    if (timeinfo->tm_hour < 10) line2 += "0";
    line2 += String(timeinfo->tm_hour) + ":";
    if (timeinfo->tm_min < 10) line2 += "0";
    line2 += String(timeinfo->tm_min);
  } else {
    line2 = "No Time";
  }
  lcd.setCursor(0, 1);
  lcd.print(centerText(line2, 16));
}

String formatTempRhMessage(float temp, float rh) {
  return "อุณหภูมิ " + String(temp, 1) + "°C\nความชื้นสัมพัทธ์ " + String(rh, 1) + "%";
}

String formatHourText(struct tm* t) {
  String h = (t->tm_hour < 10 ? "0" : "") + String(t->tm_hour);
  String m = (t->tm_min < 10 ? "0" : "") + String(t->tm_min);
  return h + "." + m + " น.";
}

void connectToWiFi() {
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("ServerRoom1 Setup")) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(centerText("Connect WiFi:", 16));
    lcd.setCursor(0, 1);
    lcd.print(centerText("Server1-Setup", 16));
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(centerText("ใช้มือถือเชื่อม", 16));
    lcd.setCursor(0, 1);
    lcd.print(centerText("WiFi: Server1-Setup", 16));
    delay(4000);

    ESP.restart();
  }
  Serial.println("WiFi Connected!");
}

bool readSensor() {
  sensors_event_t humidity, temp;
  if (sht4.getEvent(&humidity, &temp)) {
    latestTemp = temp.temperature;
    latestHumidity = humidity.relative_humidity;
    Serial.print("Sensor reading -> Temp: ");
    Serial.print(latestTemp);
    Serial.print(" °C | RH: ");
    Serial.print(latestHumidity);
    Serial.println(" %");
    return true;
  } else {
    Serial.println("Sensor read error");
    return false;
  }
}

bool recordToSheet(float temp, float rh) {
  WiFiClientSecure client;
  client.setInsecure();
  String url = String(GSHEET_URL) + "&sheetName=" + SHEET_NAME + "&temp=" + String(temp, 1) + "&hum=" + String(rh, 1);
  if (!client.connect("script.google.com", 443)) return false;
  client.print(String("GET ") + url + " HTTP/1.1\r\nHost: script.google.com\r\nConnection: close\r\n\r\n");
  while (!client.available()) delay(10);
  while (client.available()) client.read();
  client.stop();
  return true;
}

bool sendTelegramMsg(String message) {
  message.replace(" ", "%20");
  message.replace("\n", "%0A");
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + message;
  if (!client.connect("api.telegram.org", 443)) return false;
  client.print(String("GET ") + url + " HTTP/1.1\r\nHost: api.telegram.org\r\nConnection: close\r\n\r\n");
  while (!client.available()) delay(10);
  while (client.available()) client.read();
  client.stop();
  return true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText("Server1 Booting", 16));
  lcd.setCursor(0, 1);
  lcd.print(centerText("Booting...", 16));
  delay(1500);

  connectToWiFi();
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  sht4.begin();

  bool validRead = false;
  for (int i = 0; i < 10 && !validRead; i++) {
    validRead = readSensor();
    if (!validRead) delay(1000);
  }
  if (validRead) recordToSheet(latestTemp, latestHumidity);

  lastSensorTime = millis();
  lastTelegramTime = millis();

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  String startMsg = "โปรแกรมวัดอุณหภูมิและความชื้น Server Room 1 เริ่มทำงาน\n";
  if (timeinfo) {
    startMsg += "⏰ เวลา ณ " + formatHourText(timeinfo) + "\n";
  } else {
    startMsg += "⏰ เวลา ไม่ทราบ\n";
  }
  startMsg += formatTempRhMessage(latestTemp, latestHumidity);
  sendTelegramMsg(startMsg);
}

void loop() {
  unsigned long currentMillis = millis();
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  if (millis() - lastLcdTempRead > lcdTempReadInterval) {
    readSensor();
    lcdTemp = latestTemp;
    lcdHumidity = latestHumidity;
    lastLcdTempRead = millis();
  }

  bool outOfRangeNow = (latestTemp < 15 || latestTemp > 25);
  unsigned long useInterval = outOfRangeNow ? alertInterval : normalInterval;

  if (currentMillis - lastSensorTime >= useInterval) {
    if (readSensor()) {
      recordToSheet(latestTemp, latestHumidity);
    }
    lastSensorTime = currentMillis;
  }

  if (!timeinfo) return;

  if (outOfRangeNow) {
    if (currentMillis - lastTelegramTime >= 60000) {
      String msg = wasOutOfRange
        ? "Server Room 1 ยังคงนอกช่วงค่ะ\n"
        : "😱 ALERT: Server Room 1 อุณหภูมินอกช่วง!\n\n";
      msg += formatTempRhMessage(latestTemp, latestHumidity);
      sendTelegramMsg(msg);
      wasOutOfRange = true;
      lastTelegramTime = currentMillis;
    }
  } else {
    if (wasOutOfRange) {
      sendTelegramMsg("😊 ตอนนี้อุณหภูมิ Server Room 1 กลับมาอยู่ในช่วงปกติแล้ว\n" + formatTempRhMessage(latestTemp, latestHumidity));
      wasOutOfRange = false;
      lastTelegramTime = currentMillis;
    } else if (timeinfo->tm_min == 0 && timeinfo->tm_sec < 10 && lastNotifiedHour != timeinfo->tm_hour) {
      sendTelegramMsg("🕒 Hourly Update Server Room 1 เวลา " + formatHourText(timeinfo) + "\n\n" + formatTempRhMessage(latestTemp, latestHumidity));
      lastNotifiedHour = timeinfo->tm_hour;
    }
  }

  if (millis() - lastLcdUpdate > lcdUpdateInterval) {
    showOnLCD(lcdTemp, lcdHumidity, timeinfo);
    lastLcdUpdate = millis();
  }
}
