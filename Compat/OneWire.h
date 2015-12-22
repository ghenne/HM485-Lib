#ifndef _ONEWIRE_H
#define _ONEWIRE_H
#include "Arduino.h"

class OneWire
{
public:
	OneWire(int16_t Port);
	void reset_search();
	bool search(uint8_t* addr);
	static uint8_t crc8(uint8_t* addr, int16_t size);
	void reset();
	void select(uint8_t* addr);
	void write(uint8_t Value, uint8_t Powermode = 0);
	uint8_t read();
};

#endif // _ONEWIRE_H