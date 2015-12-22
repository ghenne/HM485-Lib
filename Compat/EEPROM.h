#ifndef _EEPROM_H
#define _EEPROM_H

class Memory
{
public:
	uint8_t read(int16_t address);
	void write(int16_t address, uint8_t value);
};

extern Memory EEPROM;

#define E2END 1024

#endif // _EEPROM_H
