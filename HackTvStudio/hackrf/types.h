#ifndef TYPES_H
#define TYPES_H
#pragma once
#include <math.h>
#define DB_M_PI     3.14159265358979323846
#define FL_M_PI     3.1415926535f

#define DB_M_SQRT2  1.4142135623730951
#define FL_M_SQRT2  1.4142135623f

namespace dsp {
    struct complex_tx {
        complex_tx operator*(const float b) {
            return complex_tx{ re * b, im * b };
        }

        complex_tx operator*(const double b) {
            return complex_tx{ re * (float)b, im * (float)b };
        }

        complex_tx operator/(const float b) {
            return complex_tx{ re / b, im / b };
        }

        complex_tx operator/(const double b) {
            return complex_tx{ re / (float)b, im / (float)b };
        }

        complex_tx operator*(const complex_tx& b) {
            return complex_tx{ (re * b.re) - (im * b.im), (im * b.re) + (re * b.im) };
        }

        complex_tx operator+(const complex_tx& b) {
            return complex_tx{ re + b.re, im + b.im };
        }

        complex_tx operator-(const complex_tx& b) {
            return complex_tx{ re - b.re, im - b.im };
        }

        complex_tx& operator+=(const complex_tx& b) {
            re += b.re;
            im += b.im;
            return *this;
        }

        complex_tx& operator-=(const complex_tx& b) {
            re -= b.re;
            im -= b.im;
            return *this;
        }

        complex_tx& operator*=(const float& b) {
            re *= b;
            im *= b;
            return *this;
        }

        inline complex_tx conj() {
            return complex_tx{ re, -im };
        }

        inline float phase() {
            return atan2f(im, re);
        }

        inline float fastPhase() {
            float abs_im = fabsf(im);
            float r, angle;
            if (re == 0.0f && im == 0.0f) { return 0.0f; }
            if (re >= 0.0f) {
                r = (re - abs_im) / (re + abs_im);
                angle = (FL_M_PI / 4.0f) - (FL_M_PI / 4.0f) * r;
            }
            else {
                r = (re + abs_im) / (abs_im - re);
                angle = (3.0f * (FL_M_PI / 4.0f)) - (FL_M_PI / 4.0f) * r;
            }
            if (im < 0.0f) {
                return -angle;
            }
            return angle;
        }

        inline float amplitude() {
            return sqrt((re * re) + (im * im));
        }

        inline float fastAmplitude() {
            float re_abs = fabsf(re);
            float im_abs = fabsf(im);
            if (re_abs > im_abs) { return re_abs + 0.4f * im_abs; }
            return im_abs + 0.4f * re_abs;
        }

        float re;
        float im;
    };

    struct stereo_tx {
        stereo_tx operator*(const float b) {
            return stereo_tx{ l * b, r * b };
        }

        stereo_tx operator+(const stereo_tx& b) {
            return stereo_tx{ l + b.l, r + b.r };
        }

        stereo_tx operator-(const stereo_tx& b) {
            return stereo_tx{ l - b.l, r - b.r };
        }

        stereo_tx& operator+=(const stereo_tx& b) {
            l += b.l;
            r += b.r;
            return *this;
        }

        stereo_tx& operator-=(const stereo_tx& b) {
            l -= b.l;
            r -= b.r;
            return *this;
        }

        stereo_tx& operator*=(const float& b) {
            l *= b;
            r *= b;
            return *this;
        }

        float l;
        float r;
    };
}

#endif // TYPES_H
