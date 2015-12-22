#ifndef ARDUINO_H
#define ARDUINO_H
#include <inttypes.h>
#include <string.h>
#include <iostream>

typedef uint8_t byte;
typedef bool boolean;

// Pins
#define A3 42

#define LOW 0
#define HIGH 1

#define OUTPUT 0
#define INPUT 1
#define INPUT_PULLUP 2

bool digitalRead(uint8_t port);
void digitalWrite(uint8_t port, bool value);
void pinMode(uint8_t port, uint16_t mode);

#define SERIAL_8E1 0
class Stream
{
public:
	void begin(uint32_t baud, uint8_t params);
	void print(const char* value);
	void print(uint8_t value, int16_t mode);
	void write(uint8_t value);
	uint8_t read();
	void flush();
	bool available();
};

extern Stream Serial;

#define F(x) x
#define HEX std::ios::hex

int32_t millis();
void randomSeed(uint32_t seedValue);
uint32_t random(uint32_t start, uint32_t end);

#define bitRead(value, bit) (value && 1 << bit)
#define bitSet(value, bit) (value |= 1 << bit)

#endif
