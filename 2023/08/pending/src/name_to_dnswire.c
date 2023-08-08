
#include "name_to_dnswire.h"

#include <stdint.h>
#include <x86intrin.h> // update if we need to support Windows.

size_t name_to_dnswire(const char *src, uint8_t *dst) {
  const char *srcinit = src;

  static const bool delimiter[256] = {
      1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t *counter = dst++;
  do {
    while (!delimiter[(uint8_t)*src]) {
      *dst++ = (uint8_t)*src;
      src++;
    }
    if (dst == counter || dst - counter - 1 > 63) {
      return 0;
    }
    *counter = (uint8_t)(dst - counter - 1);
    counter = dst++;
    if (*src != '.') {
      break;
    }
    src++;
  } while (true);
  *counter = 0;
  return (size_t)(src - srcinit);
}

#include <stdio.h>
void print8(const char *name, __m128i x) {
  printf("%.32s : ", name);
  uint8_t buffer[16];
  _mm_storeu_si128((__m128i *)buffer, x);
  for (size_t i = 0; i < 16; i++) {
    printf("%02x ", buffer[i]);
  }
  printf("\n");
}

// delimiters are marked as 0s, everything else as 1s
static inline uint16_t delimiter_mask(__m128i input) {
  // Delimiters for strings consisting of a contiguous set of characters
  // 0x00        :       end-of-file : 0b0000_0000
  // 0x20        :             space : 0b0010_0000
  // 0x22        :             quote : 0b0010_0010
  // 0x28        :  left parenthesis : 0b0010_1000
  // 0x29        : right parenthesis : 0b0010_1001
  // 0x09        :               tab : 0b0000_1001
  // 0x0a        :         line feed : 0b0000_1010
  // 0x3b        :         semicolon : 0b0011_1011
  // 0x0d        :   carriage return : 0b0000_1101
  const __m128i delimiters =
      _mm_setr_epi8(0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x09,
                    0x0a, 0x3b, 0x00, 0x0d, 0x00, 0x00);

  const __m128i mask = _mm_setr_epi8(-33, -1, -1, -1, -1, -1, -1, -1, -1, -33,
                                     -1, -1, -1, -1, -1, -1);
  // construct delimiter pattern for comparison
  __m128i pattern = _mm_shuffle_epi8(delimiters, input);
  // clear 5th bit (match 0x20 with 0x00, match 0x29 using 0x09)
  __m128i inputc = _mm_and_si128(input, _mm_shuffle_epi8(mask, input));
  __m128i classified = _mm_cmpeq_epi8(inputc, pattern);
  return (uint16_t)_mm_movemask_epi8(
      classified); // should be 1 wherever we have a match.
}

// the zero-trail of 'mask' indicates how many '.' we must use
// to padd input.
static inline __m128i pad_with_dots(__m128i input, uint16_t mask) {
  uint16_t length = (uint16_t)_tzcnt_u16(mask);

  static int8_t zero_masks[32] = {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                                  0,  0,  0,  0,  0,  -1, -1, -1, -1, -1, -1,
                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  __m128i zero_mask = _mm_loadu_si128((__m128i *)(zero_masks + 16 - length));
  // masking with '.'
  return _mm_blendv_epi8(input, _mm_set1_epi8('.'), zero_mask);
}

static inline __m128i dot_to_counters(__m128i input,
                                      __m128i *previous_dotcounts) {
  __m128i dots = _mm_cmpeq_epi8(input, _mm_set1_epi8('.'));
  __m128i sequential =
      _mm_setr_epi8(-128, -127, -126, -125, -124, -123, -122, -121, -120, -119,
                    -118, -117, -116, -115, -114, -113);
  __m128i dotscounts = _mm_and_si128(dots, sequential);
  __m128i origdotscounts = dotscounts;
  __m128i shifted = _mm_alignr_epi8(*previous_dotcounts, dotscounts, 1);
  dotscounts = _mm_min_epi8(shifted, dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(*previous_dotcounts, dotscounts, 1),
                            dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(*previous_dotcounts, dotscounts, 2),
                            dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(*previous_dotcounts, dotscounts, 4),
                            dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(*previous_dotcounts, dotscounts, 8),
                            dotscounts);
  __m128i next = _mm_alignr_epi8(*previous_dotcounts, dotscounts, 1);
  *previous_dotcounts = dotscounts; // save it for later
  dotscounts = _mm_subs_epu8(next, origdotscounts);
  // need to subtract one to the counters.
  dotscounts = _mm_subs_epu8(dotscounts, _mm_set1_epi8(1));
  return _mm_blendv_epi8(input, dotscounts, dots);
}

static inline __m128i dot_to_counters_no_previous(__m128i input) {
  __m128i dots = _mm_cmpeq_epi8(input, _mm_set1_epi8('.'));
  __m128i sequential =
      _mm_setr_epi8(-128, -127, -126, -125, -124, -123, -122, -121, -120, -119,
                    -118, -117, -116, -115, -114, -113);
  __m128i dotscounts = _mm_and_si128(dots, sequential);
  __m128i origdotscounts = dotscounts;
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(_mm_setzero_si128(), dotscounts, 1),
                            dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(_mm_setzero_si128(), dotscounts, 2),
                            dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(_mm_setzero_si128(), dotscounts, 4),
                            dotscounts);
  dotscounts = _mm_min_epi8(_mm_alignr_epi8(_mm_setzero_si128(), dotscounts, 8),
                            dotscounts);
  __m128i next = _mm_alignr_epi8(_mm_setzero_si128(), dotscounts, 1);
  dotscounts = _mm_subs_epu8(next, origdotscounts);
  // need to subtract one to the counters.
  dotscounts = _mm_subs_epu8(dotscounts, _mm_set1_epi8(1));
  return _mm_blendv_epi8(input, dotscounts, dots);
}

size_t name_to_dnswire_simd(const char *src, uint8_t *dst) {
  const char *srcinit = src;
  // we always insert a fake '.' initially.
  __m128i input = _mm_loadu_si128((const __m128i *)src);
  input = _mm_alignr_epi8(input, _mm_set1_epi8('.'), 15);
  src -= 1; // we pretend that we are pointing at the virtual '.'.
  uint16_t m = delimiter_mask(input);
  while (m == 0) {
    src += 16;
    dst += 16;
    input = _mm_loadu_si128((const __m128i *)src);
    m = delimiter_mask(input);
  }
  // we have reached the end.
  input = pad_with_dots(input, m);
  size_t consumed_length = (size_t)(src + _tzcnt_u16(m) - srcinit);
  if (src > srcinit) { // we need to unwind
    __m128i previous = _mm_setzero_si128();
    do {
      _mm_storeu_si128((__m128i *)dst, dot_to_counters(input, &previous));
      previous = _mm_add_epi8(previous, _mm_set1_epi8((char)0x10));
      src -= 16;
      dst -= 16;
      if (src < srcinit) {
        break;
      }
      input = _mm_loadu_si128((const __m128i *)src);
    } while (true);
    input = _mm_loadu_si128((const __m128i *)(src + 1));
    input = _mm_alignr_epi8(input, _mm_set1_epi8('.'), 15);
    _mm_storeu_si128((__m128i *)dst, dot_to_counters(input, &previous));
  } else {
    _mm_storeu_si128((__m128i *)dst, dot_to_counters_no_previous(input));
  }
  return consumed_length;
}

void print8_avx(const char *name, __m256i x) {
  printf("%.32s : ", name);
  uint8_t buffer[32];
  _mm256_storeu_si256((__m256i *)buffer, x);
  for (size_t i = 0; i < 32; i++) {
    printf("%02x ", buffer[i]);
    if (i == 15) {
      printf("| ");
    }
  }
  printf("\n");
}
static inline __m256i pad_with_dots_avx(__m256i input, uint32_t mask) {
  uint32_t length = (uint32_t)_tzcnt_u32(mask);

  static int8_t zero_masks[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  __m256i zero_mask = _mm256_loadu_si256((__m256i *)(zero_masks + 32 - length));
  // masking with '.'
  return _mm256_blendv_epi8(input, _mm256_set1_epi8('.'), zero_mask);
}

// delimiters are marked as 0s, everything else as 1s
static inline uint32_t delimiter_mask_avx(__m256i input) {
  // Delimiters for strings consisting of a contiguous set of characters
  // 0x00        :       end-of-file : 0b0000_0000
  // 0x20        :             space : 0b0010_0000
  // 0x22        :             quote : 0b0010_0010
  // 0x28        :  left parenthesis : 0b0010_1000
  // 0x29        : right parenthesis : 0b0010_1001
  // 0x09        :               tab : 0b0000_1001
  // 0x0a        :         line feed : 0b0000_1010
  // 0x3b        :         semicolon : 0b0011_1011
  // 0x0d        :   carriage return : 0b0000_1101
  const __m256i delimiters = _mm256_setr_epi8(
      0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x09, 0x0a, 0x3b,
      0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x28, 0x09, 0x0a, 0x3b, 0x00, 0x0d, 0x00, 0x00);

  const __m256i mask = _mm256_setr_epi8(
      -33, -1, -1, -1, -1, -1, -1, -1, -1, -33, -1, -1, -1, -1, -1, -1, -33, -1,
      -1, -1, -1, -1, -1, -1, -1, -33, -1, -1, -1, -1, -1, -1);
  // construct delimiter pattern for comparison
  __m256i pattern = _mm256_shuffle_epi8(delimiters, input);
  // clear 5th bit (match 0x20 with 0x00, match 0x29 using 0x09)
  __m256i inputc = _mm256_and_si256(input, _mm256_shuffle_epi8(mask, input));
  __m256i classified = _mm256_cmpeq_epi8(inputc, pattern);
  return (uint32_t)_mm256_movemask_epi8(
      classified); // should be 1 wherever we have a match.
}

static inline __m256i dot_to_counters_avx(__m256i input,
                                          __m256i *previous_dotcounts) {
  __m256i dots = _mm256_cmpeq_epi8(input, _mm256_set1_epi8('.'));
  __m256i sequential = _mm256_setr_epi8(
      -128, -127, -126, -125, -124, -123, -122, -121, -120, -119, -118, -117,
      -116, -115, -114, -113, -112, -111, -110, -109, -108, -107, -106, -105,
      -104, -103, -102, -101, -100, -99, -98, -97);
  __m256i dotscounts = _mm256_and_si256(dots, sequential);
  __m256i origdotscounts = dotscounts;
  __m256i pd = _mm256_permute2x128_si256(dotscounts, *previous_dotcounts, 0x21);
  __m256i shifted = _mm256_alignr_epi8(pd, dotscounts, 1);
  dotscounts = _mm256_min_epi8(shifted, dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 1), dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 2), dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 4), dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 8), dotscounts);
  __m256i lower = _mm256_shuffle_epi8(dotscounts, _mm256_setzero_si256());
  lower = _mm256_permute2x128_si256(lower, *previous_dotcounts, 0x21);
  dotscounts = _mm256_min_epi8(lower, dotscounts);

  __m256i next = _mm256_alignr_epi8(lower, dotscounts, 1);

  *previous_dotcounts = dotscounts; // save it for later
  dotscounts = _mm256_subs_epu8(next, origdotscounts);
  // need to subtract one to the counters.
  dotscounts = _mm256_subs_epu8(dotscounts, _mm256_set1_epi8(1));

  return _mm256_blendv_epi8(input, dotscounts, dots);
}

static inline __m256i dot_to_counters_no_previous_avx(__m256i input) {
  __m256i dots = _mm256_cmpeq_epi8(input, _mm256_set1_epi8('.'));
  __m256i sequential = _mm256_setr_epi8(
      -128, -127, -126, -125, -124, -123, -122, -121, -120, -119, -118, -117,
      -116, -115, -114, -113, -112, -111, -110, -109, -108, -107, -106, -105,
      -104, -103, -102, -101, -100, -99, -98, -97);
  __m256i dotscounts = _mm256_and_si256(dots, sequential);
  __m256i origdotscounts = dotscounts;
  __m256i pd =
      _mm256_permute2x128_si256(dotscounts, _mm256_setzero_si256(), 0x21);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 1), dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 2), dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 4), dotscounts);
  dotscounts =
      _mm256_min_epi8(_mm256_alignr_epi8(pd, dotscounts, 8), dotscounts);
  __m256i lower = _mm256_shuffle_epi8(dotscounts, _mm256_setzero_si256());
  lower = _mm256_permute2x128_si256(lower, _mm256_setzero_si256(), 0x21);
  dotscounts = _mm256_min_epi8(lower, dotscounts);
  __m256i next = _mm256_alignr_epi8(lower, dotscounts, 1);
  dotscounts = _mm256_subs_epu8(next, origdotscounts);
  // need to subtract one to the counters.
  dotscounts = _mm256_subs_epu8(dotscounts, _mm256_set1_epi8(1));
  return _mm256_blendv_epi8(input, dotscounts, dots);
}

size_t name_to_dnswire_avx(const char *src, uint8_t *dst) {
  const char *srcinit = src;
  __m256i input = _mm256_loadu_si256((const __m256i *)src);
  __m256i pd = _mm256_permute2x128_si256(_mm256_set1_epi8('.'), input, 0x21);
  input = _mm256_alignr_epi8(input, pd, 15);
  src -= 1; // we pretend that we are pointing at the virtual '.'.
  uint32_t m = delimiter_mask_avx(input);

  if (m != 0) {
    if ((m & 0xffff) != 0) {

      __m128i input128 =
          pad_with_dots(_mm256_castsi256_si128(input), (uint16_t)m);
      size_t consumed_length = (size_t)(src + _tzcnt_u32(m) - srcinit);
      _mm_storeu_si128((__m128i *)dst, dot_to_counters_no_previous(input128));
      return consumed_length;
    }
    input = pad_with_dots_avx(input, m);
    size_t consumed_length = (size_t)(src + _tzcnt_u32(m) - srcinit);
    _mm256_storeu_si256((__m256i *)dst, dot_to_counters_no_previous_avx(input));
    return consumed_length;
  }
  do {
    src += 32;
    dst += 32;
    input = _mm256_loadu_si256((const __m256i *)src);
    m = delimiter_mask_avx(input);
  } while (m == 0);
  // we have reached the end.
  input = pad_with_dots_avx(input, m);
  size_t consumed_length = (size_t)(src + _tzcnt_u32(m) - srcinit);

  __m256i previous = _mm256_setzero_si256();
  do {
    _mm256_storeu_si256((__m256i *)dst, dot_to_counters_avx(input, &previous));
    previous = _mm256_add_epi8(previous, _mm256_set1_epi8((char)0x20));
    src -= 32;
    dst -= 32;
    if (src < srcinit) {
      break;
    }
    input = _mm256_loadu_si256((const __m256i *)src);
  } while (true);
  input = _mm256_loadu_si256((const __m256i *)(src + 1));
  pd = _mm256_permute2x128_si256(_mm256_set1_epi8('.'), input, 0x21);
  input = _mm256_alignr_epi8(input, pd, 15);
  _mm256_storeu_si256((__m256i *)dst, dot_to_counters_avx(input, &previous));

  return consumed_length;
}