#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <time.h>
#include <x86intrin.h> // use GCC or clang, please

#include "benchmark.h"
typedef struct pcg_state_setseq_64 { // Internals are *Private*.
  uint64_t state;                    // RNG state.  All values are possible.
  uint64_t inc;                      // Controls which RNG sequence (stream) is
                                     // selected. Must *always* be odd.
} pcg32_random_t;

static inline uint32_t pcg32_random_r(pcg32_random_t *rng) {
  uint64_t oldstate = rng->state;
  rng->state = oldstate * 6364136223846793005ULL + rng->inc;
  uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

// assume array is aligned on 64-byte cache lines
uint32_t sumrandom(uint32_t *array, size_t powertwosize, size_t howmany,
                   pcg32_random_t *rng) {
  __m256i counter = _mm256_setzero_si256();
  for (size_t i = 0; i < howmany; i++) {
    size_t idx = pcg32_random_r(rng) & (powertwosize - 1);
    __m256i vals = _mm256_load_si256((__m256i *)(array + 16 * idx));
    counter = _mm256_add_epi32(counter, vals);
  }
  uint32_t buffer[8];
  _mm256_storeu_si256((__m256i *)buffer, counter);
  return buffer[0] + buffer[1] + buffer[2] + buffer[3] + buffer[4] + buffer[5] +
         buffer[6] + buffer[7];
}

// assume array is aligned on 64-byte cache lines
uint32_t sumrandomscalar(size_t percache, uint32_t *array, size_t powertwosize,
                         size_t howmany, pcg32_random_t *rng) {
  uint32_t counter = 0;
  for (size_t i = 0; i < howmany; i++) {
    size_t idx = pcg32_random_r(rng) & (powertwosize - 1);
    for (size_t j = 0; j < percache; j++)
      counter += array[16 * idx + j];
  }
  return counter;
}

uint32_t sumrandomscalardep(size_t percache, uint32_t *array,
                            size_t powertwosize, size_t howmany,
                            pcg32_random_t *rng) {
  uint32_t counter = 0;
  for (size_t i = 0; i < howmany; i++) {
    size_t idx = pcg32_random_r(rng) & (powertwosize - 1);
    for (size_t j = 0; j < percache; j++)
      counter += array[16 * idx + j];
    rng->state += counter;
  }
  return counter;
}
// assume array is aligned on 64-byte cache lines
uint32_t sumrandombis(uint32_t *array, size_t powertwosize, size_t howmany,
                      pcg32_random_t *rng) {
  __m256i counter = _mm256_setzero_si256();
  for (size_t i = 0; i < howmany; i++) {
    size_t idx = pcg32_random_r(rng) & (powertwosize - 1);
    __m256i vals1 = _mm256_load_si256((__m256i *)(array + 16 * idx));
    __m256i vals2 = _mm256_load_si256((__m256i *)(array + 16 * idx + 8));
    __m256i vals = _mm256_add_epi32(vals1, vals2);
    counter = _mm256_add_epi32(counter, vals);
  }
  uint32_t buffer[8];
  _mm256_storeu_si256((__m256i *)buffer, counter);
  return buffer[0] + buffer[1] + buffer[2] + buffer[3] + buffer[4] + buffer[5] +
         buffer[6] + buffer[7];
}

uint32_t *create_random_array(size_t count) {
  uint32_t *targets;
  int r = posix_memalign((void **)&targets, 64, count * 64);
  if (r != 0) {
    printf("oh!\n\n");
    return NULL;
  }
  printf("hopefully allocated %zu 32-bit ints\n", 16 * count);
  for (size_t i = 0; i < 16 * count; i++)
    targets[i] = i;
  return targets;
}

void demo(size_t level) {
  size_t size = UINT64_C(1) << level;
  printf("number = %zu.\n", size);
  pcg32_random_t key = {.state = 3244441, .inc = 4444};
  printf("memory usage: %zu MB \n", size * 64 / (1024 * 1024));

  uint32_t *testvalues = create_random_array(size);
  int repeat = 5;
  bool verbose = true;
  int width = 10000000;
  key.state = 333;
  uint32_t expected;

  struct timespec time;

  clock_gettime(CLOCK_REALTIME, &time);
  size_t bef = time.tv_sec * 1000000000ULL + time.tv_nsec;
  clock_gettime(CLOCK_REALTIME, &time);
  size_t aft = time.tv_sec * 1000000000ULL + time.tv_nsec;
  printf("Time per cache line access %3.f ns \n", (aft - bef) * 1.0 / width);
  key.state = 333;
  expected = sumrandomscalar(1, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalar(1, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalardep(1, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalardep(1, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalar(4, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalar(4, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalardep(4, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalardep(4, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalar(8, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalar(8, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalardep(8, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalardep(8, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalar(16, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalar(16, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandomscalardep(16, testvalues, size, width, &key);
  BEST_TIME(sumrandomscalardep(16, testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  key.state = 333;
  expected = sumrandom(testvalues, size, width, &key);
  BEST_TIME(sumrandom(testvalues, size, width, &key), expected, key.state = 333,
            repeat, width, width * 64, verbose);

  key.state = 333;
  expected = sumrandombis(testvalues, size, width, &key);
  BEST_TIME(sumrandombis(testvalues, size, width, &key), expected,
            key.state = 333, repeat, width, width * 64, verbose);
  free(testvalues);
  printf("\n");
}

int main() {
  demo(27);
  return EXIT_SUCCESS;
}
