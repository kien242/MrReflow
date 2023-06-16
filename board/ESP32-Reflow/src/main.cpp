#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <Adafruit_ADS1X15.h>
#include <PID_v1.h>

#define SSRPin 12

// Thermistor values
#define ANALOG_RESOLUTION 26400         // 1 bit = 0.125mV @ 1x gain   +/- 4.096V
#define THERMISTORNOMINAL 100000        // resistance at 25 degrees C   
#define TEMPERATURENOMINAL 25           // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 5                    // # of samples for temp averaging     
#define BCOEFFICIENT 3950               // The beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 100300           // the value of the series resistor

int tempSamples[NUMSAMPLES];


// ***** TYPE DEFINITIONS *****
typedef enum REFLOW_STATE
{
  REFLOW_STATE_IDLE,
  REFLOW_STATE_PREHEAT,
  REFLOW_STATE_SOAK,
  REFLOW_STATE_REFLOW,
  REFLOW_STATE_COOL,
  REFLOW_STATE_COMPLETE,
	REFLOW_STATE_TOO_HOT,
  REFLOW_STATE_ERROR
} reflowState_t;

typedef enum REFLOW_PROFILE
{
  REFLOW_PROFILE_LEADFREE,
  REFLOW_PROFILE_LEADED
} reflowProfile_t;

String strDescription[8] =
{
"Idle", "Preheat", "Soak", "Reflow", "Cool", "Complete", "!!! TOO HOT !!!", "ERROR"
};

typedef enum REFLOW_STATUS
{
  REFLOW_STATUS_OFF,
  REFLOW_STATUS_ON
} reflowStatus_t;

// ***** GENERAL PROFILE CONSTANTS *****
#define PROFILE_TYPE_ADDRESS 0
#define TEMPERATURE_ROOM 50
#define TEMPERATURE_SOAK_MIN 150
#define TEMPERATURE_COOL_MIN 100
#define SENSOR_SAMPLING_TIME 1000
#define SOAK_TEMPERATURE_STEP 5
// ***** LEAD FREE PROFILE CONSTANTS *****
#define TEMPERATURE_SOAK_MAX_LF 200
#define TEMPERATURE_REFLOW_MAX_LF 250
#define SOAK_MICRO_PERIOD_LF 9000

// ***** LEADED PROFILE CONSTANTS *****
#define TEMPERATURE_SOAK_MAX_PB 180
#define TEMPERATURE_REFLOW_MAX_PB 224
#define SOAK_MICRO_PERIOD_PB 10000

// ***** PID PARAMETERS *****
// ***** PRE-HEAT STAGE *****
#define PID_KP_PREHEAT 100
#define PID_KI_PREHEAT 0.025
#define PID_KD_PREHEAT 20
// ***** SOAKING STAGE *****
#define PID_KP_SOAK 300
#define PID_KI_SOAK 0.05
#define PID_KD_SOAK 250
// ***** REFLOW STAGE *****
#define PID_KP_REFLOW 300
#define PID_KI_REFLOW 0.05
#define PID_KD_REFLOW 350

#define PID_SAMPLE_TIME 1000


// ***** PID CONTROL VARIABLES *****
// Temperature variables also for PID
double tempIst = 0.0, tempSoll = 0.0;
double output;
double kp = PID_KP_PREHEAT;
double ki = PID_KI_PREHEAT;
double kd = PID_KD_PREHEAT;
int windowSize;
unsigned long windowStartTime;
unsigned long nextCheck;
unsigned long nextRead;
unsigned long updateLcd;
unsigned long timerSoak;
unsigned long buzzerPeriod;
unsigned char soakTemperatureMax;
unsigned char reflowTemperatureMax;
unsigned long soakMicroPeriod;
// Reflow oven controller state machine state variable
reflowState_t reflowState;
// Reflow oven controller status
reflowStatus_t reflowStatus;
// Reflow profile type
reflowProfile_t reflowProfile;
// Seconds timer
int timerSeconds;

// Specify PID control interface
PID reflowOvenPID(&tempIst, &output, &tempSoll, kp, ki, kd, DIRECT);

// 16-Bit ADC
Adafruit_ADS1115 ads;

// WiFi
const char* ssid = "Xiaomi_1009";
const char* password = "trankien1998";

// Webserver
AsyncWebServer server(80);
// Create a WebSocket object
AsyncWebSocket ws("/ws");

JSONVar readings;

// WebSocket message
String message = "";

// Timer variables (WiFi)
unsigned long lastTime = 0;
unsigned long timerDelay = 5000;

// Status variable
enum statusTypes
{
  started = 1,
  stopped = 0
};

int status = statusTypes::stopped;

enum profileStatus
{
  change = 1,
  noChange = 0
};

int profile = profileStatus::noChange;

// Get Sensor Readings and return JSON object
String getSensorReadings()
{
  readings["tempIst"] = String(tempIst);
  readings["tempSoll"] = String(tempSoll);

  if (status == statusTypes::started)
    readings["status"] = "started";
  else
    readings["status"] = "stopped";

  readings["phase"] = String(strDescription[reflowState]);
  readings["profile"] = reflowProfile;

  readings["soakMaxTemp"] = soakTemperatureMax;
  readings["reflowMaxTemp"] = reflowTemperatureMax;

  String jsonString = JSON.stringify(readings);
  return jsonString;
}

// Initialize SPIFFS
void initSPIFFS() 
{
  if (!SPIFFS.begin()) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  else{
    Serial.println("SPIFFS mounted successfully");
  }
}

// Initialize WiFi
void initWiFi() 
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(250);
  }
  Serial.println(WiFi.localIP());
}

// Read Temperature from Thermistor
float readTemp(void)
{
  uint8_t i;
  float average;

  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) 
  {
    //Read from ADS1115 A0
    tempSamples[i] = ads.readADC_SingleEnded(0);
    delay(10);
  }
  
  // average all the samples out
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) 
  {
     average += tempSamples[i];
  }
  average /= NUMSAMPLES;
  
  // convert the value to resistance
  average = ANALOG_RESOLUTION / average - 1;
  average = SERIESRESISTOR / average;
  
  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert absolute temp to C

  return steinhart;
}

void notifyClients(String strValue) 
{
  ws.textAll(strValue);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) 
{
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) 
  {
    data[len] = 0;
    message = (char*)data;
    if (message.indexOf("start") >= 0) 
    {
      status = statusTypes::started;
      Serial.println("start");
      notifyClients(getSensorReadings());
    }
    if (message.indexOf("stop") >= 0) 
    {
      status = statusTypes::stopped;
      Serial.println("stop");
      notifyClients(getSensorReadings());
    }    
    if (message.indexOf("profile") >= 0) 
    {
      Serial.println("changeProfile");
      profile = profileStatus::change;
      notifyClients(getSensorReadings());
    } 
    if (strcmp((char*)data, "getValues") == 0) 
    {
      notifyClients(getSensorReadings());
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup() 
{
  // Serial port for debugging purposes
  Serial.begin(115200);
  initSPIFFS();
  initWiFi();

  // ADS1115
  // 1x gain   +/- 4.096V  1 bit = 0.125mV
  ads.setGain(GAIN_ONE);
  ads.begin();

  // Check current selected reflow profile
  unsigned char value = EEPROM.read(PROFILE_TYPE_ADDRESS);
  if (value == 0) 
  {
    // Valid reflow profile value
    reflowProfile = REFLOW_PROFILE_LEADFREE;
  }
  else if (value == 1)
  {
    // Valid reflow profile value
    reflowProfile = REFLOW_PROFILE_LEADED;
  }
  else
  {
    // Default to lead-free profile
    EEPROM.write(PROFILE_TYPE_ADDRESS, 0);
    reflowProfile = REFLOW_PROFILE_LEADFREE;
  }

  // Load profile specific constant
  if (reflowProfile == REFLOW_PROFILE_LEADFREE)
  {
    soakTemperatureMax = TEMPERATURE_SOAK_MAX_LF;
    reflowTemperatureMax = TEMPERATURE_REFLOW_MAX_LF;
    soakMicroPeriod = SOAK_MICRO_PERIOD_LF;
  }
  else
  {
    soakTemperatureMax = TEMPERATURE_SOAK_MAX_PB;
    reflowTemperatureMax = TEMPERATURE_REFLOW_MAX_PB;
    soakMicroPeriod = SOAK_MICRO_PERIOD_PB;
  }

  // Set window size
  windowSize = 2000;
  // Initialize time keeping variable
  nextCheck = millis();
  // Initialize thermocouple reading variable
  nextRead = millis();

  initWebSocket();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  // Favicon
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "image/png", "/favicon.png");
  });

  server.serveStatic("/", SPIFFS, "/");

  // Start server
  server.begin();

  // SSR
  pinMode(SSRPin, OUTPUT);
  digitalWrite(SSRPin,LOW);

  // stabilize components
  delay(500);
}

void loop() 
{
   // Current time
  unsigned long now;

  // Time to read thermocouple?
  if (millis() > nextRead)
  {
    // Read thermocouple next sampling period
    nextRead += SENSOR_SAMPLING_TIME;

    // Read current temperature + Notify Client
    notifyClients(getSensorReadings());
    tempIst = readTemp();
				
    // If thermocouple problem detected
    //TODO: konform mit NTC??
		if (isnan(tempIst))
		{
      // Illegal operation
      reflowState = REFLOW_STATE_ERROR;
      reflowStatus = REFLOW_STATUS_OFF;
    }
  }

  if (millis() > nextCheck)
  {
    // Check tempIst in the next seconds
    nextCheck += SENSOR_SAMPLING_TIME;
    // If reflow process is on going
    if (reflowStatus == REFLOW_STATUS_ON)
    {
      // Toggle red LED as system heart beat
      //digitalWrite(ledRedPin, !(digitalRead(ledRedPin)));
      // Increase seconds timer for reflow curve analysis
      timerSeconds++;
      // Send temperature and time stamp to serial 
      Serial.print(timerSeconds);
      Serial.print(" ");
      Serial.print(tempSoll);
      Serial.print(" ");
      Serial.print(tempIst);
      Serial.print(" ");
      Serial.println(output);
    }
    else
    {
      // Turn off red LED
      //digitalWrite(ledRedPin, HIGH);
    }

    // If currently in error state
    if (reflowState == REFLOW_STATE_ERROR)
    {
      // No thermocouple wire connected
      Serial.println("TC Error!");
    }
    
  }

  // Reflow oven controller state machine
  switch (reflowState)
  {
  case REFLOW_STATE_IDLE:
		// If oven temperature is still above room temperature
		if (tempIst >= TEMPERATURE_ROOM)
		{
			reflowState = REFLOW_STATE_TOO_HOT;
		}
		else
		{
			// If switch is pressed to start reflow process
			if (status == statusTypes::started)
			{
        // Send header for CSV file
        Serial.println("Time tempSoll tempIst Output");
        // Intialize seconds timer for serial debug information
        timerSeconds = 0;
        // Initialize PID control window starting time
        windowStartTime = millis();
        // Ramp up to minimum soaking temperature
        tempSoll = TEMPERATURE_SOAK_MIN;
        // Load profile specific constant
        if (reflowProfile == REFLOW_PROFILE_LEADFREE)
        {
          soakTemperatureMax = TEMPERATURE_SOAK_MAX_LF;
          reflowTemperatureMax = TEMPERATURE_REFLOW_MAX_LF;
          soakMicroPeriod = SOAK_MICRO_PERIOD_LF;
        }
        else
        {
          soakTemperatureMax = TEMPERATURE_SOAK_MAX_PB;
          reflowTemperatureMax = TEMPERATURE_REFLOW_MAX_PB;
          soakMicroPeriod = SOAK_MICRO_PERIOD_PB;
        }

        // Tell the PID to range between 0 and the full window size
        reflowOvenPID.SetOutputLimits(0, windowSize);
        reflowOvenPID.SetSampleTime(PID_SAMPLE_TIME);
        // Turn the PID on
        reflowOvenPID.SetMode(AUTOMATIC);
        // Proceed to preheat stage
        reflowState = REFLOW_STATE_PREHEAT;
      }
      else
      {
        tempSoll = 0.0;
      }
    }
    break;

  case REFLOW_STATE_PREHEAT:
    reflowStatus = REFLOW_STATUS_ON;
    // If minimum soak temperature is achieve       
    if (tempIst >= TEMPERATURE_SOAK_MIN)
    {
      // Chop soaking period into smaller sub-period
      timerSoak = millis() + soakMicroPeriod;
      // Set less agressive PID parameters for soaking ramp
      reflowOvenPID.SetTunings(PID_KP_SOAK, PID_KI_SOAK, PID_KD_SOAK);
      // Ramp up to first section of soaking temperature
      tempSoll = TEMPERATURE_SOAK_MIN + SOAK_TEMPERATURE_STEP;   
      // Proceed to soaking state
      reflowState = REFLOW_STATE_SOAK; 
    }
    break;

  case REFLOW_STATE_SOAK:     
    // If micro soak temperature is achieved       
    if (millis() > timerSoak)
    {
      timerSoak = millis() + soakMicroPeriod;
      // Increment micro tempSoll
      tempSoll += SOAK_TEMPERATURE_STEP;
      if (tempSoll > soakTemperatureMax)
      {
        // Set agressive PID parameters for reflow ramp
        reflowOvenPID.SetTunings(PID_KP_REFLOW, PID_KI_REFLOW, PID_KD_REFLOW);
        // Ramp up to first section of soaking temperature
        tempSoll = reflowTemperatureMax;   
        // Proceed to reflowing state
        reflowState = REFLOW_STATE_REFLOW; 
      }
    }
    break; 

  case REFLOW_STATE_REFLOW:
    // We need to avoid hovering at peak temperature for too long
    // Crude method that works like a charm and safe for the components
    if (tempIst >= (reflowTemperatureMax - 5))
    {
      // Set PID parameters for cooling ramp
      reflowOvenPID.SetTunings(PID_KP_REFLOW, PID_KI_REFLOW, PID_KD_REFLOW);
      // Ramp down to minimum cooling temperature
      tempSoll = TEMPERATURE_COOL_MIN;   
      // Proceed to cooling state
      reflowState = REFLOW_STATE_COOL; 
    }
    break;   

  case REFLOW_STATE_COOL:
    // If minimum cool temperature is achieve       
    if (tempIst <= TEMPERATURE_COOL_MIN)
    {
      // Retrieve current time for buzzer usage
      buzzerPeriod = millis() + 1000;
      // Turn on buzzer and green LED to indicate completion
			//TODO: COMPLETE!!
      // Turn off reflow process
      reflowStatus = REFLOW_STATUS_OFF;                
      // Proceed to reflow Completion state
      reflowState = REFLOW_STATE_COMPLETE; 
    }         
    break;    

  case REFLOW_STATE_COMPLETE:
    if (millis() > buzzerPeriod)
    {
			// Reflow process ended
      reflowState = REFLOW_STATE_IDLE; 
    }
    break;
	
	case REFLOW_STATE_TOO_HOT:
		// If oven temperature drops below room temperature
		if (tempIst < TEMPERATURE_ROOM)
		{
			// Ready to reflow
			reflowState = REFLOW_STATE_IDLE;
		}
		break;
		
  case REFLOW_STATE_ERROR:
    // If thermocouple problem is still present
			if (isnan(tempIst))
		{
      // Wait until thermocouple wire is connected
      reflowState = REFLOW_STATE_ERROR; 
    }
    else
    {
      // Clear to perform reflow process
      reflowState = REFLOW_STATE_IDLE; 
    }
    break;    
  }    

  // STOP
  if (status == statusTypes::stopped)
  {
    // If currently reflow process is on going
    if (reflowStatus == REFLOW_STATUS_ON)
    {
      // Button press is for cancelling
      // Turn off reflow process
      reflowStatus = REFLOW_STATUS_OFF;
      // Reinitialize state machine
      reflowState = REFLOW_STATE_IDLE;
    }
  } 

  // Profile change
  if (profile == profileStatus::change)
  {
    // Only can switch reflow profile during idle
    if (reflowState == REFLOW_STATE_IDLE)
    {
      // reset state
      profile = profileStatus::noChange;
      // Currently using lead-free reflow profile
      if (reflowProfile == REFLOW_PROFILE_LEADFREE)
      {
        // Switch to leaded reflow profile
        reflowProfile = REFLOW_PROFILE_LEADED;
        EEPROM.write(PROFILE_TYPE_ADDRESS, 1);
      }
      // Currently using leaded reflow profile
      else
      {
        // Switch to lead-free profile
        reflowProfile = REFLOW_PROFILE_LEADFREE;
        EEPROM.write(PROFILE_TYPE_ADDRESS, 0);
      }

      // Load profile specific constant
      if (reflowProfile == REFLOW_PROFILE_LEADFREE)
      {
        soakTemperatureMax = TEMPERATURE_SOAK_MAX_LF;
        reflowTemperatureMax = TEMPERATURE_REFLOW_MAX_LF;
        soakMicroPeriod = SOAK_MICRO_PERIOD_LF;
      }
      else
      {
        soakTemperatureMax = TEMPERATURE_SOAK_MAX_PB;
        reflowTemperatureMax = TEMPERATURE_REFLOW_MAX_PB;
        soakMicroPeriod = SOAK_MICRO_PERIOD_PB;
      }

    }
  }

  // PID computation and SSR control
  if (reflowStatus == REFLOW_STATUS_ON)
  {
    now = millis();

    reflowOvenPID.Compute();

    if((now - windowStartTime) > windowSize)
    { 
      // Time to shift the Relay Window
      windowStartTime += windowSize;
    }
    if(output > (now - windowStartTime)) 
      digitalWrite(SSRPin, HIGH);
    else 
      digitalWrite(SSRPin, LOW);   
  }
  // Reflow oven process is off, ensure oven is off
  else 
  {
    digitalWrite(SSRPin, LOW);
  }

// WebSockets
ws.cleanupClients();
}
