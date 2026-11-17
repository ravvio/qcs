#include "qcs/gate.h"
#include "qcs/random.h"
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
  size_t target_mask = target_bit - 1;
  // 2^(n-1) pairs
  size_t pairs_count = qcs_state_size(state) / 2;
  uint32_t controls = gate.control_ones | gate.control_zeros;
  for (size_t p = 0; p < pairs_count; p++) {
    // i0/i1 is the pair with 0/1 inserted at target position
    size_t low = (p & target_mask);
    size_t high = (p & ~target_mask) << 1;
    // work out pairs
    size_t i0 = high | low;
    size_t i1 = i0 | target_bit;
    // check controls: the ones at 1, the zeros at 0
    if ((i0 & controls) != gate.control_ones) {
      continue;
    }
    // retrieve amplitudes
    double complex a0 = state->amps[i0];
    double complex a1 = state->amps[i1];
    // apply gate: m * v
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

  qcs_sample_cdf(cdf, size, shots, random, outcomes);
  free(cdf);
  return QCS_ERR_OK;
}

void qcs_sample_cdf(const double *cdf, size_t size, size_t shots,
                    QCS_Random *random, size_t *outcomes) {
  double total = cdf[size - 1];
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

QCS_Error qcs_state_measure(QCS_State *state, uint32_t qubit,
                            QCS_Random *random, bool *value) {
  if (qubit >= state->n) {
    fprintf(stderr, "[e] invalid qubit: %u, state has %u qubits\n", qubit,
            state->n);
    return QCS_ERR_QUBIT_RANGE;
  }

  size_t size = qcs_state_size(state);

  // probabilities that the qubit is 0 or 1
  size_t bit = (size_t)1 << qubit;
  double p[2] = {0, 0};
  for (size_t i = 0; i < size; i++) {
    bool one = (i & bit) != 0;
    p[one] += qcs_state_probability(state, i);
  }
  double total = p[0] + p[1];
  if (!(total > 0)) {
    fprintf(stderr, "[e] invalid state: total probability %.17g\n", total);
    return QCS_ERR_INVALID_STATE;
  }

  // draw
  *value = qcs_random_next_double(random) * total < p[0] ? 0 : 1;

  // collapse state and renormalize
  double scale = 1 / sqrt(p[*value]);
  for (size_t i = 0; i < size; i++) {
    bool one = (i & bit) != 0;
    bool has_value = one == (*value == 1);
    state->amps[i] = has_value ? state->amps[i] * scale : 0;
  }
  return QCS_ERR_OK;
}

QCS_Error qcs_state_reset(QCS_State *state, uint32_t qubit,
                          QCS_Random *random) {
  // measure
  bool value;
  QCS_Error err = qcs_state_measure(state, qubit, random, &value);
  if (err != QCS_ERR_OK) {
    return err;
  }
  if (value == 1) {
    qcs_state_apply_gate(state, qcs_gate_create_x(qubit));
  }
  return QCS_ERR_OK;
}
