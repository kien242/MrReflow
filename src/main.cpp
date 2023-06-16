/**
 *  Copyright (C) 2018  foxis (Andrius Mikonis <andrius.mikonis@gmail.com>)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include "ReflowController_v1.h"
#include <ArduinoJson.h>
#include "AsyncJson.h"
#include "Config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>

#define OLED_RESET 4        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/event");
ControllerBase *controller = NULL;
AsyncWebSocketClient *_client = NULL;
Config config("/config.json", "/profiles.json");
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void textThem(const char *text)
{
  int tryId = 0;
  for (int count = 0; count < ws.count();)
  {
    if (ws.hasClient(tryId))
    {
      ws.client(tryId)->text(text);
      count++;
    }
    tryId++;
  }
}

void textThem(const String &text)
{
  textThem(text.c_str());
}

void textThem(JsonObject &root, AsyncWebSocketClient *client)
{
  String json;
  root.printTo(json);

  if (client != NULL)
    client->text(json);
  else
    textThem(json);
}

void textThem(JsonObject &root)
{
  textThem(root, NULL);
}

void send_reading(float reading, float target, float time, AsyncWebSocketClient *client, bool reset)
{
  S_printf("Sending readings...");

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();

  JsonObject &data = root.createNestedObject("readings");
  JsonArray &times = root.createNestedArray("times");
  JsonArray &readings = root.createNestedArray("readings");
  JsonArray &targets = root.createNestedArray("targets");

  times.add(time);
  readings.add(reading);
  targets.add(target);
  data["reset"] = reset;

  textThem(root);
}

void setupController(ControllerBase *c)
{
  S_printf("Controller setup..");

  // report messages
  c->onMessage([](const char *msg)
               {
		StaticJsonBuffer<200> jsonBuffer;
		JsonObject &root = jsonBuffer.createObject();
		root["message"] = msg;
		textThem(root); });

  c->onHeater([](bool heater)
              {
		S_printf("Heater: %s", heater ? "on" : "off");
		StaticJsonBuffer<200> jsonBuffer;
		JsonObject &root = jsonBuffer.createObject();
		root["heater"] = heater;
		textThem(root); });

  // report readings
  c->onReadingsReport([](const std::vector<ControllerBase::Temperature_t> &readings, unsigned long elapsed)
                      { send_reading(controller->log_to_temperature(readings[readings.size() - 1]), controller->target(), elapsed / 1000.0, NULL, readings.size() == 1); });

  // report mode change
  c->onMode([](ControllerBase::MODE_t last, ControllerBase::MODE_t current)
            {
		S_printf("Change mode: from %s to %s", controller->translate_mode(last), controller->translate_mode(current));
		StaticJsonBuffer<200> jsonBuffer;
		JsonObject &root = jsonBuffer.createObject();
		root["mode"] = controller->translate_mode(current);

		textThem(root); });
  c->onStage([](const char *stage, float target)
             {
		S_printf("Reflow stage: %s", stage);
		StaticJsonBuffer<200> jsonBuffer;
		JsonObject &root = jsonBuffer.createObject();
		root["stage"] = stage;
		root["target"] = target;
		textThem(root); });

  controller = c;
  S_printf("Controller setup DONE");
}

void send_data(AsyncWebSocketClient *client)
{
  S_printf("Sending all data...");
  std::vector<ControllerBase::Temperature_t>::iterator I = controller->readings().begin();
  std::vector<ControllerBase::Temperature_t>::iterator end = controller->readings().end();
  DynamicJsonBuffer jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  JsonArray &times = root.createNestedArray("times");
  JsonArray &readings = root.createNestedArray("readings");
  JsonArray &targets = root.createNestedArray("targets");
  root["reset"] = true;
  root["message"] = "INFO: Connected!";
  root["mode"] = controller->translate_mode();
  root["target"] = controller->target();
  root["profile"] = controller->profile();
  root["stage"] = controller->stage();
  root["heater"] = controller->heater();

  float seconds = 0;
  while (I != end)
  {
    times.add(seconds);
    readings.add(controller->log_to_temperature(*I));
    targets.add(controller->target());
    seconds += config.reportInterval / 1000.0;
    I++;
  }

  textThem(root, client);
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(I2C_SCL, I2C_SDA, 100000);
  pinMode(18, OUTPUT);
  pinMode(23, OUTPUT);
  SPIFFS.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  display.setRotation(2);
  display.clearDisplay();
  display.setTextColor(1);

  display.display();

  config.load_config();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("");

  display.setCursor(0, 12);
  display.setTextSize(4);
  display.println("");

  display.setCursor(0, 64 - 16);
  display.setTextSize(2);
  display.println("config ok");
  display.display();

  config.load_profiles();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("");

  display.setCursor(0, 12);
  display.setTextSize(4);
  display.println("");

  display.setCursor(0, 64 - 16);
  display.setTextSize(2);
  display.println("profile ok");
  display.display();

  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    String pr = "Wifi";
    for (int i = 0; i < count; i++)
    {
      pr = pr + "+";
    }
    display.setTextWrap(true);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println(pr);
    Serial.println(pr);
    display.display();

    delay(500);
    count++;
  }
  display.setTextWrap(false);
  Serial.println(WiFi.localIP());

  server.addHandler(&ws);
  server.addHandler(&events);
  server.serveStatic("/", SPIFFS, "/web").setDefaultFile("index.html");
  // Heap for general Servertest
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", String(ESP.getFreeHeap())); });
  server.on("/profiles", HTTP_GET, [](AsyncWebServerRequest *request)
            {
		AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/profiles.json");
		//request->send(SPIFFS, "/profiles.json");
		response->addHeader("Access-Control-Allow-Origin", "*");
		response->addHeader("Access-Control-Allow-Methods", "GET");
		response->addHeader("Content-Type", "application/json");
		request->send(response); });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
            {
		AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/config.json");
		//request->send(SPIFFS, "/profiles.json");
		response->addHeader("Access-Control-Allow-Origin", "*");
		response->addHeader("Access-Control-Allow-Methods", "GET");
		response->addHeader("Content-Type", "application/json");
		request->send(response); });
  server.on(
      "/profiles", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
		config.save_profiles(request, data, len, index, total);
		config.load_profiles(); });
  server.on(
      "/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      { config.save_config(request, data, len, index, total); });
  server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request)
            {
		AsyncWebServerResponse *response = request->beginResponse(200, "application/json", controller->calibrationString());
		response->addHeader("Access-Control-Allow-Origin", "*");
		response->addHeader("Access-Control-Allow-Methods", "GET");
		request->send(response); });
  server.onNotFound(
      [](AsyncWebServerRequest *request)
      { request->send(404); });

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
             {
		if (type == WS_EVT_DATA) {
			char cmd[256] = "";
			memcpy(cmd, data, min(len, sizeof(cmd) - 1));

			if (strcmp(cmd, "WATCHDOG") == 0) {
			} else if (strncmp(cmd, "profile:", 8) == 0) {
				controller->profile(String(cmd + 8));
				sprintf(cmd, "{\"profile\": \"%s\"}", controller->profile().c_str());
				textThem(cmd);
			} else if (strcmp(cmd, "ON") == 0) {
				controller->mode(ControllerBase::ON);
			} else if (strcmp(cmd, "REBOOT") == 0) {
				ESP.restart();
			} else if (strcmp(cmd, "CALIBRATE") == 0) {
				controller->mode(ControllerBase::CALIBRATE);
			} else if (strcmp(cmd, "TARGET_PID") == 0) {
				controller->mode(ControllerBase::TARGET_PID);
			} else if (strcmp(cmd, "REFLOW") == 0) {
				controller->mode(ControllerBase::REFLOW);
				delay(2000);
			} else if (strcmp(cmd, "OFF") == 0) {
				controller->mode(ControllerBase::OFF);
			} else if (strcmp(cmd, "COOLDOWN") == 0) {
				controller->mode(ControllerBase::CALIBRATE_COOL);
			} else if (strcmp(cmd, "CURRENT-TEMPERATURE") == 0) {
				unsigned long now = millis();
				send_reading(controller->measure_temperature(now), controller->target(), controller->elapsed(now)/1000.0, NULL, false);
			} else if (strncmp(cmd, "target:", 7) == 0) {
				controller->target(max(0, min(atoi(cmd + 7), MAX_TEMPERATURE)));
				sprintf(cmd, "{\"target\": %.2f}", controller->target());
				textThem(cmd);
			}
		} else if (type == WS_EVT_CONNECT) {
			send_data(client);
			S_printf("Connected...");
		} else if (type == WS_EVT_DISCONNECT) {
			S_printf("Disconnected...");
			//controller->mode(ControllerBase::ERROR_OFF);
		} });

  server.begin();
  setupController(new ReflowController(config, display));

  S_printf("Server started..");
  display.clearDisplay();

  esp_task_wdt_init(3, true); // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);
}

int count = 0;
void loop()
{
  digitalWrite(23, HIGH);
  digitalWrite(18,LOW);
  esp_task_wdt_reset();
  unsigned long now = millis();
  controller->loop(now);
}
