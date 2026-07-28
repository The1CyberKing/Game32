#ifndef EEPROM_H
#define EEPROM_H

#include <cstdint>
#include <cstddef>

class EEPROMClass {
public:
    uint8_t read(int address);
    void write(int address, uint8_t val);
    void update(int address, uint8_t val);

    template<typename T>
    T &get(int address, T &t) {
        uint8_t* p = (uint8_t*)(void*)&t;
        for (size_t i = 0; i < sizeof(T); i++) {
            p[i] = read(address + i);
        }
        return t;
    }

    template<typename T>
    const T &put(int address, const T &t) {
        const uint8_t* p = (const uint8_t*)(const void*)&t;
        for (size_t i = 0; i < sizeof(T); i++) {
            update(address + i, p[i]);
        }
        return t;
    }

    void commit() {}

    void loadFromFile(const char* gameName);
    void commitToFile(const char* gameName);

private:
    uint8_t m_data[1024] = {0};
};

extern EEPROMClass EEPROM;

#endif // EEPROM_H
