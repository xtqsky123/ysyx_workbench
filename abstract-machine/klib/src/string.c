#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

size_t strlen(const char *s) {
  size_t len = 0;
  while (*s ++) { len ++; }
  return len;
}

char *strcpy(char *dst, const char *src) {
  char *ret = dst;
  while ((*dst ++ = *src ++));
  return ret;
}

char *strncpy(char *dst, const char *src, size_t n) {
  char *ret = dst;
  for (size_t i = 0; i < n; i++) {
    if ((*dst ++ = *src ++) == '\0') {
      while (i++ < n) {
        *dst ++ = '\0';
      }
      break;
    }
  }
  return ret;
}

char *strcat(char *dst, const char *src) {
  char *ret = dst;
  while (*dst) { dst ++; }
  while ((*dst ++ = *src ++));
  return ret;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    if (*s1 != *s2) {
      return *s1 - *s2;
    }
    s1++;
    s2++;
  }
  return *s1 - *s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (*s1 != *s2) {
      return *s1 - *s2;
    }
    if (*s1 == '\0') {
      return 0;
    }
    s1++;
    s2++;
  }
  return 0;
}

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  for (size_t i = 0; i < n; i++) {
    p[i] = (uint8_t)c;
  }
  return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *p_dst = (uint8_t *)dst;
  const uint8_t *p_src = (const uint8_t *)src;
  if (p_dst < p_src) {
    for (size_t i = 0; i < n; i++) {
      p_dst[i] = p_src[i];
    }
  } else {
    for (size_t i = n; i > 0; i--) {
      p_dst[i - 1] = p_src[i - 1];
    }
  }
  return dst;
}

void *memcpy(void *out, const void *in, size_t n) {
  uint8_t *p_out = (uint8_t *)out;
  const uint8_t *p_in = (const uint8_t *)in;
  for (size_t i = 0; i < n; i++) {
    p_out[i] = p_in[i];
  }
  return out;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) {
      return p1[i] - p2[i];
    }
  }
  return 0;
}

#endif
