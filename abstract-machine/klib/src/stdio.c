#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

static void buf_putc(char *out, size_t n, size_t *pos, char ch) {
  if (n > 0 && *pos < n - 1) {
    out[*pos] = ch;
  }
  (*pos) ++;
}

static void buf_puts(char *out, size_t n, size_t *pos, const char *s) {
  if (s == NULL) {
    s = "(null)";
  }
  while (*s != '\0') {
    buf_putc(out, n, pos, *s);
    s ++;
  }
}

static void buf_putd(char *out, size_t n, size_t *pos, int val) {
  static const unsigned int divs[] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u
  };

  unsigned int mag;
  if (val < 0) {
    buf_putc(out, n, pos, '-');
    mag = 0u - (unsigned int)val;
  } else {
    mag = (unsigned int)val;
  }

  int started = 0;
  for (int i = 0; i < (int)LENGTH(divs); i ++) {
    unsigned int d = divs[i];
    int digit = 0;
    while (mag >= d) {
      mag -= d;
      digit ++;
    }
    if (digit != 0 || started || d == 1u) {
      buf_putc(out, n, pos, '0' + digit);
      started = 1;
    }
  }
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  size_t pos = 0;

  while (*fmt != '\0') {
    if (*fmt != '%') {
      buf_putc(out, n, &pos, *fmt);
      fmt ++;
      continue;
    }

    fmt ++;

    if (*fmt == '\0') {
      break;
    }

    int zero_pad = 0;
    int width = 0;

    if (*fmt == '0') {
      zero_pad = 1;
      fmt ++;
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt ++;
      }
      if (*fmt == '\0') {
        break;
      }
    }

    switch (*fmt) {
      case '%':
        buf_putc(out, n, &pos, '%');
        break;
      case 's':
        buf_puts(out, n, &pos, va_arg(ap, const char *));
        break;
      case 'd': {
        int val = va_arg(ap, int);
        if (zero_pad && width > 0) {
          char tmp[16];
          size_t tpos = 0;
          int negative = val < 0;
          unsigned int mag = negative ? (0u - (unsigned int)val) : (unsigned int)val;
          static const unsigned int divs[] = {
            1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
            10000u, 1000u, 100u, 10u, 1u
          };
          int started = 0;

          if (negative) {
            tmp[tpos++] = '-';
          }

          for (int i = 0; i < (int)LENGTH(divs); i ++) {
            unsigned int d = divs[i];
            int digit = 0;
            while (mag >= d) {
              mag -= d;
              digit ++;
            }
            if (digit != 0 || started || d == 1u) {
              tmp[tpos++] = '0' + digit;
              started = 1;
            }
          }

          if (negative) {
            buf_putc(out, n, &pos, '-');
            int digits_len = (int)tpos - 1;
            for (int i = digits_len; i < width; i ++) {
              buf_putc(out, n, &pos, '0');
            }
            for (int i = 1; i < (int)tpos; i ++) {
              buf_putc(out, n, &pos, tmp[i]);
            }
          } else {
            int digits_len = (int)tpos;
            for (int i = digits_len; i < width; i ++) {
              buf_putc(out, n, &pos, '0');
            }
            for (int i = 0; i < (int)tpos; i ++) {
              buf_putc(out, n, &pos, tmp[i]);
            }
          }
        } else {
          buf_putd(out, n, &pos, val);
        }
        break;
      }
      default:
        buf_putc(out, n, &pos, '%');
        if (zero_pad) {
          buf_putc(out, n, &pos, '0');
          if (width >= 10) {
            char wbuf[16];
            int wlen = 0;
            int w = width;
            while (w > 0) {
              wbuf[wlen++] = '0' + (w % 10);
              w /= 10;
            }
            while (wlen > 0) {
              buf_putc(out, n, &pos, wbuf[--wlen]);
            }
          } else if (width > 0) {
            buf_putc(out, n, &pos, '0' + width);
          }
        }
        buf_putc(out, n, &pos, *fmt);
        break;
    }

    fmt ++;
  }

  if (n > 0) {
    out[pos < n ? pos : n - 1] = '\0';
  }

  return (int)pos;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  return vsnprintf(out, (size_t)-1, fmt, ap);
}

int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsprintf(out, fmt, ap);
  va_end(ap);
  return ret;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return ret;
}

int printf(const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  for (int i = 0; buf[i] != '\0'; i ++) {
    putch(buf[i]);
  }

  return ret;
}

#endif
