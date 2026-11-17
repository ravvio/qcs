// Bell state (|00> + |11>) / sqrt(2) 

OPENQASM 3.0;
include "stdgates.inc";

qubit[2] q;
bit[2] c;

// Put q[0] into an equal superposition
h q[0];

// Entangle q[0] with q[1]
cx q[0], q[1];

// Measure both qubits
c = measure q;
