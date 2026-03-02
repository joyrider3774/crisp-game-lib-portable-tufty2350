#pragma once
// EEPROM emulation for RP2350 (Tufty 2350)
// Drop-in compatible with ESP8266/Arduino EEPROM.h
//
// Tufty 2350 flash layout (verified by analyzing actual UF2 files v2.0.1):
//
//   offset 0x00000000 - 0x00198900  znax game binary         (1634 KB)
//   offset 0x00000000 - 0x0012A900  MicroPython firmware     (1194 KB)
//   offset 0x00200000 - 0x00266700  ROMFS                    ( 409 KB, always present in both MicroPython UF2s)
//   offset 0x002FF000               EEPROM sector            (   4 KB) <-- here, safe from all of the above
//   offset 0x00300000 - 0x004ED000  FatFS user filesystem    (1972 KB, only in micropython-with-filesystem.uf2)
//   offset 0x00F00000 - 0x00FFFFFF  LittleFS                 (1024 KB, verified via uos.statvfs)
//
// Gap analysis vs EEPROM sector at 0x002FF000:
//   znax binary ends       1433 KB before EEPROM
//   MicroPython ends       1873 KB before EEPROM
//   ROMFS ends              610 KB before EEPROM
//   FatFS starts after EEPROM - no overlap
//   LittleFS starts after EEPROM - no overlap
//
// Reflashing MicroPython (either UF2) does NOT overwrite the EEPROM sector.
//
// Usage:
//   EEPROM.begin(256);
//   EEPROM.write(0, 42);
//   EEPROM.commit();
//   uint8_t val = EEPROM.read(0);
//   EEPROM.end();

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

// One sector in the gap between ROMFS (ends 0x00266700) and FatFS (starts 0x00300000)
// Untouched by all known Tufty 2350 UF2 images
#ifndef EEPROM_FLASH_OFFSET
#define EEPROM_FLASH_OFFSET 0x002FF000
#endif

#ifndef EEPROM_MAX_SIZE
#define EEPROM_MAX_SIZE FLASH_SECTOR_SIZE  // 4096 bytes max
#endif

extern uint32_t __flash_binary_end;

class EEPROMClass {
public:
    EEPROMClass() : _data(nullptr), _size(0), _dirty(false) {}

    ~EEPROMClass() {
        end();
    }

    void begin(size_t size) {
        if (size == 0 || size > EEPROM_MAX_SIZE) size = EEPROM_MAX_SIZE;
        _size = size;

        // Panic if binary has grown into our EEPROM sector - better to crash than silently corrupt
        uint32_t binary_end = (uint32_t)&__flash_binary_end - XIP_BASE;
        if (binary_end >= EEPROM_FLASH_OFFSET) {
            panic("EEPROM: binary (0x%08X) overlaps EEPROM sector (0x%08X)",
                  binary_end, EEPROM_FLASH_OFFSET);
        }

        if (_data) delete[] _data;
        _data = new uint8_t[_size];

        // Load current flash contents into RAM buffer
        const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + EEPROM_FLASH_OFFSET);
        memcpy(_data, flash_ptr, _size);
        _dirty = false;
    }

    void end() {
        if (_dirty) commit();
        if (_data) {
            delete[] _data;
            _data = nullptr;
        }
        _size = 0;
    }

    uint8_t read(int addr) {
        if (!_data || addr < 0 || (size_t)addr >= _size) return 0xFF;
        return _data[addr];
    }

    void write(int addr, uint8_t val) {
        if (!_data || addr < 0 || (size_t)addr >= _size) return;
        if (_data[addr] != val) {
            _data[addr] = val;
            _dirty = true;
        }
    }

    bool commit() {
        if (!_data || !_dirty) return true;

        // Pad buffer to full sector size for flash_range_program
        uint8_t sector_buf[FLASH_SECTOR_SIZE];
        // Fill with current flash contents first (preserve bytes outside _size)
        const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + EEPROM_FLASH_OFFSET);
        memcpy(sector_buf, flash_ptr, FLASH_SECTOR_SIZE);
        // Overlay our data
        memcpy(sector_buf, _data, _size);

        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(EEPROM_FLASH_OFFSET, FLASH_SECTOR_SIZE);
        flash_range_program(EEPROM_FLASH_OFFSET, sector_buf, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);

        _dirty = false;
        return true;
    }

    // Returns true if there are uncommitted changes
    bool dirty() const { return _dirty; }

    // Returns allocated size
    size_t length() const { return _size; }

    // Direct byte access: EEPROM[addr]
    uint8_t &operator[](int addr) {
        _dirty = true;  // assume write intent
        return _data[addr];
    }

    // put/get for arbitrary types (same as ESP8266/Arduino)
    template<typename T>
    const T &put(int addr, const T &val) {
        const uint8_t *src = (const uint8_t *)&val;
        for (size_t i = 0; i < sizeof(T); i++) {
            write(addr + i, src[i]);
        }
        return val;
    }

    template<typename T>
    T &get(int addr, T &val) {
        uint8_t *dst = (uint8_t *)&val;
        for (size_t i = 0; i < sizeof(T); i++) {
            dst[i] = read(addr + i);
        }
        return val;
    }

private:
    uint8_t *_data;
    size_t   _size;
    bool     _dirty;
};

// Global instance, just like ESP8266
static EEPROMClass EEPROM;