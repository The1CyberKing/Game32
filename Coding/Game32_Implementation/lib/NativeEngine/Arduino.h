#ifndef ARDUINO_H
#define ARDUINO_H

#ifdef ARDUINO
#include_next <Arduino.h>
#endif
#include "Arduboy2ESP.h"
#include "EEPROM.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <string>

#undef min
#define min(a,b) ((a)<(b)?(a):(b))
#undef max
#define max(a,b) ((a)>(b)?(a):(b))

#undef abs
#define abs(x) ((x)>0?(x):-(x))

#include "AVRCompat.h"

#endif // ARDUINO_H
