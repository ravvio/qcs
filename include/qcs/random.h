#ifndef QCS_RANDOM_DECL
#define QCS_RANDOM_DECL

#include <stdint.h>

// Seeded random numbers (splitmix64)
typedef struct {
  uint64_t state;
} QCS_Random;

QCS_Random qcs_random_create(uint64_t seed);
uint64_t qcs_random_next(QCS_Random *random);
// Uniform in [0, 1)
double qcs_random_next_double(QCS_Random *random);

#endif
