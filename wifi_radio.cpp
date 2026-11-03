#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <VS1053.h> // 请根据你使用的VS1053库调整
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

// ================= 配置区 =================
const char* ssid = "你的WiFi名称";
const char* password = "你的WiFi密码";

// 电台预设列表 (需替换为真实MP3流地址)
const int stationCount = 3;
const char* stationNames[stationCount] = {"Radio One", "Jazz FM", "Classic Rock"};
const char* stationURLs[stationCount] = {
  "http://stream.example.com/radio1.mp3",
  "http://stream.example.com/jazz.mp3",
  "http://stream.example.com/rock.mp3"
};

// 天气 API (使用 Open-Meteo 免费无 Key 接口)
// 请将 latitude 和 longitude 改为你所在城市的坐标
String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=25.03&longitude=121.56&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=3";

// ================= 引脚定义 =================
#define VS1053_CS    15
#define VS1053_DCS   0
#define VS1053_DREQ  16
#define BTN_PREV     2
#define BTN_NEXT     3
#define BTN_VOL_UP   10
#define BTN_VOL_DOWN 9

// OLED 定义
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// 全局状态
int currentStation = 0;
int volume = 70;
int currentPage = 0; // 0=电台, 1=天气, 2=时间
unsigned long lastDebounceTime = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastPageSwitch = 0;

// 天气数据结构
struct DailyWeather {
  String date;
  float tempMax;
  float tempMin;
  int code;
};
DailyWeather forecasts[3];

// 模拟 VS1053 对象 (实际需根据库实例化)
// VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);
// ================= 函数声明 =================
void connectWiFi();
void playStation(int index);
void adjustVolume(int delta);
void updateWeather();
void drawRadioPage();
void drawWeatherPage();
void drawTimePage();
void drawWeatherIcon(int x, int y, int code);
String getWeatherDesc(int code);

void setup() {
  Serial.begin(115200);
  
  // 初始化引脚
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_VOL_UP, INPUT_PULLUP);
  pinMode(BTN_VOL_DOWN, INPUT_PULLUP);
  
  // 初始化 OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.display();
  
  connectWiFi();
  
  // 初始化 VS1053 (根据你的库调整)
  // SPI.begin();
  // player.begin();
  // player.setVolume(volume);
  
  // 初始播放
  playStation(0);
  
  // 首次获取天气
  updateWeather();
}

void loop() {
  // ---- 按键扫描与防抖 ----
  if (millis() - lastDebounceTime > 200) {
    if (digitalRead(BTN_PREV) == LOW) {
      currentStation = (currentStation - 1 + stationCount) % stationCount;
      playStation(currentStation);
      lastDebounceTime = millis();
    }
    else if (digitalRead(BTN_NEXT) == LOW) {
      currentStation = (currentStation + 1) % stationCount;
      playStation(currentStation);
      lastDebounceTime = millis();
    }
    else if (digitalRead(BTN_VOL_UP) == LOW) {
      adjustVolume(5);
      lastDebounceTime = millis();
    }
    else if (digitalRead(BTN_VOL_DOWN) == LOW) {
      adjustVolume(-5);
      lastDebounceTime = millis();
    }
    // 长按或额外按键切换页面 (此处演示用 NEXT 长按切换，实际建议加独立按键)
    // 为简化，这里通过一个虚拟的切换逻辑：每 10 秒自动轮换页面？不，用按键组合较好。
    // 实际建议增加一个独立的 PAGE 按键接 GPIO1(TX) 或 GPIO1 冲突较大，可用 A0。
    // 此处假设我们用 BTN_PREV 和 BTN_NEXT 同时按下来切页，逻辑略复杂，建议加按键。
  }
  
  // ---- 定时更新天气 (每30分钟) ----
  if (millis() - lastWeatherUpdate > 1800000) {
    updateWeather();
    lastWeatherUpdate = millis();
  }
  
  // ---- 刷新屏幕 ----
  display.clearDisplay();
  if (currentPage == 0) drawRadioPage();
  else if (currentPage == 1) drawWeatherPage();
  else drawTimePage();
  display.display();
  
  delay(50);
}

// ================= 页面绘制 =================
void drawRadioPage() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("STATION ");
  display.print(currentStation + 1);
  display.print("/");
  display.println(stationCount);
  
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.println(stationNames[currentStation]);
  
  // 音量条
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print("VOL:");
  int barWidth = map(volume, 0, 100, 0, 80);
  display.drawRect(28, 48, 82, 8, WHITE);
  display.fillRect(30, 50, barWidth, 4, WHITE);
  display.setCursor(112, 48);
  display.print(volume);
}

void drawWeatherPage() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("3-DAY FORECAST");
  
  for (int i = 0; i < 3; i++) {
    int y = 12 + i * 17;
    display.setCursor(0, y);
    display.print(forecasts[i].date.substring(5)); // MM-DD
    
    // 天气图标 (简易绘制)
    drawWeatherIcon(45, y - 1, forecasts[i].code);
    
    display.setCursor(70, y);
    display.print(forecasts[i].tempMin, 0);
    display.print("-");
    display.print(forecasts[i].tempMax, 0);
    display.print("C");
  }
}

void drawTimePage() {
  // 使用 NTP 时间 (需在 setup 中配置 configTime)
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("DATE & TIME");
  
  display.setTextSize(2);
  display.setCursor(0, 20);
  char dateBuf[20];
  sprintf(dateBuf, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  display.println(dateBuf);
  
  display.setCursor(0, 44);
  char timeBuf[10];
  sprintf(timeBuf, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  display.println(timeBuf);
}

void drawWeatherIcon(int x, int y, int code) {
  // 根据 WMO code 绘制极简图标 (0=晴, 1-3=云, 45+=雾, 51+=雨, 71+=雪)
  if (code == 0) {
    // 太阳
    display.drawCircle(x + 4, y + 4, 3, WHITE);
  } else if (code >= 1 && code <= 3) {
    // 云
    display.drawCircle(x + 2, y + 4, 2, WHITE);
    display.drawCircle(x + 6, y + 4, 3, WHITE);
    display.drawLine(x, y + 6, x + 10, y + 6, WHITE);
  } else if (code >= 51 && code <= 67) {
    // 雨
    display.drawLine(x, y + 2, x + 8, y + 2, WHITE);
    display.drawLine(x + 2, y + 4, x + 1, y + 7, WHITE);
    display.drawLine(x + 5, y + 4, x + 4, y + 7, WHITE);
  } else {
    // 默认点
    display.drawPixel(x + 4, y + 4, WHITE);
  }
}

// ================= 网络与功能 =================
void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  // 配置 NTP 时间 (用于第三页)
  configTime(8 * 3600, 0, "pool.ntp.org"); // 东八区
}

void playStation(int index) {
  // 实际代码需调用 VS1053 库的连接函数
  // player.stop();
  // player.connecttohost(stationURLs[index]);
  Serial.print("Playing: ");
  Serial.println(stationNames[index]);
  delay(500); // 模拟缓冲
}

void adjustVolume(int delta) {
  volume += delta;
  if (volume < 0) volume = 0;
  if (volume > 100) volume = 100;
  // player.setVolume(volume); // 实际库函数
}

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(weatherUrl);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    
    JsonArray dates = doc["daily"]["time"];
    JsonArray tMax = doc["daily"]["temperature_2m_max"];
    JsonArray tMin = doc["daily"]["temperature_2m_min"];
    JsonArray codes = doc["daily"]["weather_code"];
    
    for (int i = 0; i < 3 && i < dates.size(); i++) {
      forecasts[i].date = dates[i].as<String>();
      forecasts[i].tempMax = tMax[i].as<float>();
      forecasts[i].tempMin = tMin[i].as<float>();
      forecasts[i].code = codes[i].as<int>();
    }
  }
  http.end();
}