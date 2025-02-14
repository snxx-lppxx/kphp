#include <stdint.h>

#include "pointers.h"

SomeData *nullptr_data(void) {
  return (SomeData*)0;
}

uint64_t intptr_addr_value(int *ptr) {
  return (uint64_t)(ptr);
}

uint64_t voidptr_addr_value(void *ptr) {
  return (uint64_t)(ptr);
}

void write_int64(int64_t *dst, int64_t value) {
  *dst = value;
}

int64_t read_int64(const int64_t *ptr) {
  return *ptr;
}

int64_t* read_int64p(int64_t **ptr) {
  return *ptr;
}

uint8_t bytes_array_get(uint8_t *arr, int offset) {
  return arr[offset];
}

void bytes_array_set(uint8_t *arr, int offset, uint8_t val) {
  arr[offset] = val;
}

void* ptr_to_void(void *ptr) {
  return ptr;
}
