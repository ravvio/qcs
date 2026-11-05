#include <qcs/random.h>

QCS_Random qcs_random_create(uint64_t seed) {
  return (QCS_Random){.state = seed};
}

// splitmix64, https://prng.di.unimi.it/splitmix64.c
uint64_t qcs_random_next(QCS_Random *random) {
  uint64_t z = (random->state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

// top 53 bits / 2^53
double qcs_random_next_double(QCS_Random *random) {
  return (double)(qcs_random_next(random) >> 11) * 0x1.0p-53;
}
