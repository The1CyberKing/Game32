#ifndef EEPROM_H
#define EEPROM_H

#include <cstdint>

class EEPROMClass {
public:
    uint8_t read(int address);
    void write(int address, uint8_t val);
    void update(int address, uint8_t val);

    void loadFromFile(const char* gameName);
    void commitToFile(const char* gameName);

private:
    uint8_t m_data[1024] = {0};
};

extern EEPROMClass EEPROM;

#endif // EEPROM_H
