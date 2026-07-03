#ifndef FIXED_POINTS_H
#define FIXED_POINTS_H

#include <cstdint>

class SQ7x8 {
public:
    float val{0.0f};
    
    constexpr SQ7x8() : val(0.0f) {}
    constexpr SQ7x8(float v) : val(v) {}
    constexpr SQ7x8(int v) : val((float)v) {}
    constexpr SQ7x8(double v) : val((float)v) {}
    
    static constexpr SQ7x8 fromInternal(int16_t raw) {
        return SQ7x8(raw / 256.0f);
    }
    
    operator float() const { return val; }
    explicit operator int() const { return (int)val; }
    explicit operator int16_t() const { return (int16_t)val; }
    
    SQ7x8& operator+=(const SQ7x8& other) { val += other.val; return *this; }
    SQ7x8& operator-=(const SQ7x8& other) { val -= other.val; return *this; }
    SQ7x8& operator*=(const SQ7x8& other) { val *= other.val; return *this; }
    SQ7x8& operator/=(const SQ7x8& other) { val /= other.val; return *this; }
};

inline constexpr SQ7x8 operator+(SQ7x8 l, SQ7x8 r) { return SQ7x8(l.val + r.val); }
inline constexpr SQ7x8 operator-(SQ7x8 l, SQ7x8 r) { return SQ7x8(l.val - r.val); }
inline constexpr SQ7x8 operator*(SQ7x8 l, SQ7x8 r) { return SQ7x8(l.val * r.val); }
inline constexpr SQ7x8 operator/(SQ7x8 l, SQ7x8 r) { return SQ7x8(l.val / r.val); }

inline constexpr bool operator<(const SQ7x8& l, const SQ7x8& r) { return l.val < r.val; }
inline constexpr bool operator>(const SQ7x8& l, const SQ7x8& r) { return l.val > r.val; }
inline constexpr bool operator<=(const SQ7x8& l, const SQ7x8& r) { return l.val <= r.val; }
inline constexpr bool operator>=(const SQ7x8& l, const SQ7x8& r) { return l.val >= r.val; }
inline constexpr bool operator==(const SQ7x8& l, const SQ7x8& r) { return l.val == r.val; }
inline constexpr bool operator!=(const SQ7x8& l, const SQ7x8& r) { return l.val != r.val; }

inline constexpr bool operator<(const SQ7x8& l, float r) { return l.val < r; }
inline constexpr bool operator>(const SQ7x8& l, float r) { return l.val > r; }
inline constexpr bool operator<=(const SQ7x8& l, float r) { return l.val <= r; }
inline constexpr bool operator>=(const SQ7x8& l, float r) { return l.val >= r; }
inline constexpr bool operator==(const SQ7x8& l, float r) { return l.val == r; }
inline constexpr bool operator!=(const SQ7x8& l, float r) { return l.val != r; }

inline constexpr bool operator<(float l, const SQ7x8& r) { return l < r.val; }
inline constexpr bool operator>(float l, const SQ7x8& r) { return l > r.val; }
inline constexpr bool operator<=(float l, const SQ7x8& r) { return l <= r.val; }
inline constexpr bool operator>=(float l, const SQ7x8& r) { return l >= r.val; }
inline constexpr bool operator==(float l, const SQ7x8& r) { return l == r.val; }
inline constexpr bool operator!=(float l, const SQ7x8& r) { return l != r.val; }

inline constexpr bool operator<(const SQ7x8& l, int r) { return l.val < (float)r; }
inline constexpr bool operator>(const SQ7x8& l, int r) { return l.val > (float)r; }
inline constexpr bool operator<=(const SQ7x8& l, int r) { return l.val <= (float)r; }
inline constexpr bool operator>=(const SQ7x8& l, int r) { return l.val >= (float)r; }
inline constexpr bool operator==(const SQ7x8& l, int r) { return l.val == (float)r; }
inline constexpr bool operator!=(const SQ7x8& l, int r) { return l.val != (float)r; }

inline constexpr bool operator<(int l, const SQ7x8& r) { return (float)l < r.val; }
inline constexpr bool operator>(int l, const SQ7x8& r) { return (float)l > r.val; }
inline constexpr bool operator<=(int l, const SQ7x8& r) { return (float)l <= r.val; }
inline constexpr bool operator>=(int l, const SQ7x8& r) { return (float)l >= r.val; }
inline constexpr bool operator==(int l, const SQ7x8& r) { return (float)l == r.val; }
inline constexpr bool operator!=(int l, const SQ7x8& r) { return (float)l != r.val; }

typedef SQ7x8 SQ15x16;

#endif // FIXED_POINTS_H
