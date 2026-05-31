#pragma once

// Minimal Print stub. The firmware's Print class (from Arduino) is the
// base of HalFile + other output sinks. The EPUB pipeline only uses the
// raw `write(const uint8_t*, size_t)` and `write(uint8_t)` overloads, so
// that's all we expose. write(uint8_t) is left pure-virtual to force
// subclasses (HalFile in our shim, plus any others we add later) to
// implement it.

#include <cstddef>
#include <cstdint>
#include <string>

class Print {
 public:
  virtual ~Print() = default;

  // Required for any concrete subclass.
  virtual size_t write(uint8_t b) = 0;

  // Optional bulk-write -- default impl loops over write(uint8_t). Subclasses
  // override for batched IO. Matches the Arduino Print API shape.
  virtual size_t write(const uint8_t* buf, size_t size) {
    size_t written = 0;
    for (size_t i = 0; i < size; ++i) {
      if (write(buf[i]) == 0) break;
      ++written;
    }
    return written;
  }
  size_t write(const char* str) {
    if (!str) return 0;
    return write(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
  }
  size_t write(const void* buf, size_t size) {
    return write(static_cast<const uint8_t*>(buf), size);
  }

  // Convenience helpers some firmware code calls. printf is the
  // commonly-touched one for diagnostic logging.
  size_t print(const std::string& s) { return write(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
  size_t print(const char* s) { return write(s); }
  size_t println(const std::string& s) {
    size_t n = print(s);
    n += write('\n');
    return n;
  }
};
