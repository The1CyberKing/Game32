#ifndef PRINT_H
#define PRINT_H

#ifdef ARDUINO
#include_next <Print.h>
#else

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

class __FlashStringHelper;

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        while (size--) {
            if (write(*buffer++)) n++;
            else break;
        }
        return n;
    }
    virtual size_t write(const char *str) {
        if (!str) return 0;
        return write((const uint8_t *)str, strlen(str));
    }
    
    size_t print(const char *str) {
        return write(str);
    }
    size_t print(char c) {
        return write((uint8_t)c);
    }
    size_t print(const __FlashStringHelper *ifsh) {
        return print((const char *)ifsh);
    }
    size_t print(int n, int base = 10) {
        char buf[33];
        if (base == 10) snprintf(buf, sizeof(buf), "%d", n);
        else if (base == 16) snprintf(buf, sizeof(buf), "%x", n);
        else snprintf(buf, sizeof(buf), "%d", n);
        return print(buf);
    }
    size_t print(unsigned int n, int base = 10) {
        char buf[33];
        if (base == 10) snprintf(buf, sizeof(buf), "%u", n);
        else if (base == 16) snprintf(buf, sizeof(buf), "%x", n);
        else snprintf(buf, sizeof(buf), "%u", n);
        return print(buf);
    }
    size_t print(long n, int base = 10) {
        char buf[33];
        if (base == 10) snprintf(buf, sizeof(buf), "%ld", n);
        else if (base == 16) snprintf(buf, sizeof(buf), "%lx", n);
        else snprintf(buf, sizeof(buf), "%ld", n);
        return print(buf);
    }
    size_t print(unsigned long n, int base = 10) {
        char buf[33];
        if (base == 10) snprintf(buf, sizeof(buf), "%lu", n);
        else if (base == 16) snprintf(buf, sizeof(buf), "%lx", n);
        else snprintf(buf, sizeof(buf), "%lu", n);
        return print(buf);
    }
    size_t println() {
        return print("\r\n");
    }
    size_t println(const char *s) {
        size_t n = print(s);
        n += println();
        return n;
    }
    size_t println(const __FlashStringHelper *ifsh) {
        size_t n = print(ifsh);
        n += println();
        return n;
    }
    size_t println(int num, int base = 10) {
        size_t n = print(num, base);
        n += println();
        return n;
    }
};

#endif // ARDUINO

#endif // PRINT_H
