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

int SIZEOFARRAY = 35;
int in[] = { 55, 55, 56, 56, 61, 62, 65, 68, 72, 76, 80, 85, 90, 96, 100, 112,
		125, 141, 159, 168, 178, 187, 197, 219, 243, 269, 296, 326, 342, 357,
		375, 390, 404, 440, 618 };
int out[] = { 245, 250, 235, 240, 230, 225, 220, 215, 210, 205, 200, 195, 190,
		185, 180, 170, 160, 150, 140, 135, 130, 125, 120, 110, 95, 90, 80, 70,
		65, 60, 55, 50, 45, 30, 16 };

int readTemperature() {
	int raw = analogRead(A0);
	return multiMap(raw, in, out, SIZEOFARRAY);
}
