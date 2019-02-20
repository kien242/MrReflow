#include "Thermistor.h"
#include "ControllerBase.h"

void wifiSearch() {
	pinMode(BUZZER_A, OUTPUT);
	tone(BUZZER_A, 1240, 100);
	delay(200);
	tone(BUZZER_A, 740, 100);
	delay(200);
	tone(BUZZER_A, 240, 100);
	delay(200);
}

void notifySound(){
	for(int i = 0;i<10;i++){
		tone(BUZZER_A, 1240, 100);
		delay(200);
		tone(BUZZER_A, 740, 100);
		delay(200);
		tone(BUZZER_A, 240, 100);
		delay(200);
	}
}

void alarmSound(){
	for(int i = 0;i<5000;i++){
		tone(BUZZER_A, i, 3);
		delay(3);
	}
}

void wifiOK() {
 	tone(BUZZER_A, 240, 500);
 	delay(500);
 	tone(BUZZER_A, 740, 500);
 	delay(500);
 	tone(BUZZER_A, 1240, 500);
 	delay(500);
}
