#include <math.h>
#include <qcs/core.h>
#include <qcs/state.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

QCS_Error qcs_state_create(uint32_t n, QCS_State *out) {
  if (n > QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] invalid state: qubit count %u over maximum %u\n", n,
            QCS_MAX_QUBITS);
    return QCS_ERR_MAX_QUBITS;
  }

  size_t size = (size_t)1 << n;
  double complex *amps = calloc(size, sizeof(double complex));
  if (!amps) {
    fprintf(stderr,
            "[e] out of memory while allocating amplitudes of size %zu\n",
            size);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  amps[0] = 1;

  *out = (QCS_State){.n = n, .amps = amps};
  return QCS_ERR_OK;
}

QCS_Error qcs_state_create_random(uint32_t n, QCS_Random *random,
                                  QCS_State *out) {
  QCS_Error err = qcs_state_create(n, out);
  if (err != QCS_ERR_OK)
    return err;

  double s = 0;
  size_t size = qcs_state_size(out);
  for (size_t i = 0; i < size; i++) {
    double r = qcs_random_next_double(random);
    out->amps[i] = r;
    s += r * r;
  }
  // normalize
  for (size_t i = 0; i < size; i++) {
    out->amps[i] /= sqrt(s);
  }
  return QCS_ERR_OK;
}

QCS_Error qcs_state_create_basis(uint32_t n, size_t index, QCS_State *out) {
  QCS_Error err = qcs_state_create(n, out);
  if (err) {
    return err;
  }
  if (index >= qcs_state_size(out)) {
    fprintf(stderr,
            "[e] invalid state: basis index %zu sets qubits past the %u of "
            "the state\n",
            index, n);
    qcs_state_destroy(out);
    return QCS_ERR_QUBIT_RANGE;
  }
  out->amps[0] = 0;
  out->amps[index] = 1;
  return QCS_ERR_OK;
}

void qcs_state_destroy(QCS_State *state) {
  free(state->amps);
  state->amps = NULL;
}

void qcs_state_apply_gate(QCS_State *state, QCS_Gate gate) {
  size_t target_bit = (size_t)1 << gate.target;
  size_t size = (size_t)1 << state->n;
  for (size_t i = 0; i < size; i++) {
    // skip already matched state branch
    if (i & target_bit)
      continue;
    // skip if controls are off
    if ((i & gate.controls) != gate.controls)
      continue;
    // apply gate
    size_t i0 = i;
    size_t i1 = i | target_bit;
    double complex a0 = state->amps[i0];
    double complex a1 = state->amps[i1];
    // m * v
    state->amps[i0] = gate.m[0][0] * a0 + gate.m[0][1] * a1;
    state->amps[i1] = gate.m[1][0] * a0 + gate.m[1][1] * a1;
  }
}

size_t qcs_state_size(const QCS_State *state) { return (size_t)1 << state->n; }

double qcs_state_probability(const QCS_State *state, size_t index) {
  double complex a = state->amps[index];
  // |a|^2
  return creal(a) * creal(a) + cimag(a) * cimag(a);
}

double qcs_state_norm(const QCS_State *state) {
  double sum = 0;
  for (size_t i = 0; i < qcs_state_size(state); i++) {
    sum += qcs_state_probability(state, i);
  }
  return sum;
}

QCS_Error qcs_state_sample(const QCS_State *state, size_t shots,
                           QCS_Random *random, size_t *outcomes) {
  size_t size = qcs_state_size(state);

  // cumulative distribution
  double *cdf = malloc(size * sizeof(double));
  if (!cdf) {
    fprintf(stderr,
            "[e] out of memory while allocating the distribution of %zu "
            "patterns\n",
            size);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  double total = 0;
  for (size_t i = 0; i < size; i++) {
    total += qcs_state_probability(state, i);
    cdf[i] = total;
  }
  if (!(total > 0)) {
    fprintf(stderr, "[e] invalid state: total probability %.17g\n", total);
    free(cdf);
    return QCS_ERR_INVALID_STATE;
  }

  for (size_t shot = 0; shot < shots; shot++) {
    // dart in [0, total)
    double u = qcs_random_next_double(random) * total;

    // first pattern with cdf[i] > u
    size_t lo = 0;
    size_t hi = size - 1;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (cdf[mid] > u) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    outcomes[shot] = lo;
  }

  free(cdf);
  return QCS_ERR_OK;
}

QCS_Error qcs_state_fidelity(const QCS_State *a, const QCS_State *b,
                             double *out) {
  if (a->n != b->n) {
    fprintf(stderr,
            "[e] states have mismatched qubits count: count(a) = %u != %u = "
            "count(b)\n",
            a->n, b->n);
    return QCS_ERR_INVALID_STATE;
  }

  double norm_a = qcs_state_norm(a);
  double norm_b = qcs_state_norm(b);
  if (!(norm_a > 0) || !(norm_b > 0)) {
    fprintf(stderr,
            "[e] cannot calculate fidelity of a zero state norm(a) = %f, "
            "norm(b) = %f\n",
            norm_a, norm_b);
    return QCS_ERR_INVALID_STATE;
  }

  double complex s = 0;
  for (size_t i = 0; i < qcs_state_size(a); i++) {
    double complex ai = a->amps[i];
    double complex bi = b->amps[i];
    s += conj(ai) * bi;
  }

  // |<a|b>|^2 / (<a|a> * <b|b>)
  *out = (creal(s) * creal(s) + cimag(s) * cimag(s)) / (norm_a * norm_b);
  return QCS_ERR_OK;
}
