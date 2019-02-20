#include "Thermistor.h"

// note: the _in array should have increasing values
int multiMap(int val, int* _in, int* _out, uint8_t size) {
	// take care the value is within range
	// val = constrain(val, _in[0], _in[size-1]);
	if (val <= _in[0])
		return _out[0];
	if (val >= _in[size - 1])
		return _out[size - 1];

	// search right interval
	uint8_t pos = 1;  // _in[0] allready tested
	while (val > _in[pos])
		pos++;

	// this will handle all exact "points" in the _in array
	if (val == _in[pos])
		return _out[pos];

	// interpolate in the right segment for the rest
	return (val - _in[pos - 1]) * (_out[pos] - _out[pos - 1])
			/ (_in[pos] - _in[pos - 1]) + _out[pos - 1];
}

int SIZEOFARRAY = 27;
int in[] = { 38, 40, 48, 55, 93, 100, 117, 133, 164, 192, 253, 254, 293, 325,
		356, 377, 417, 452, 514, 669, 829, 883, 906, 942, 961, 1015, 1024, };
int out[] = { 260, 250, 240, 230, 220, 210, 200, 190, 180, 170, 155, 150, 145,
		140, 135, 130, 125, 120, 110, 90, 70, 60, 55, 50, 45, 32, 24 };

int readTemperature() {
	int raw = analogRead(A0);
	Serial.print("tmp: ");
	Serial.println(raw);
	return multiMap(raw, in, out, SIZEOFARRAY);
}
