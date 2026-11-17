// Quantum teleportation

OPENQASM 3.0;
include "stdgates.inc";

qubit[3] q;     // q[0]: state to teleport, q[1]: Alice, q[2]: Bob
bit[2] c;       // Alice's measurement results
bit result;     // Bob's final check

// Prepare arbitrary state |psi> on q[0] to teleport
ry(pi/3) q[0];
rz(pi/4) q[0];
barrier q;

// Create a bell pair between Alice and Bob
h q[1];        // put in superposizione
cx q[1], q[2]; // entangle
barrier q;

// Entangle |psi> with Alice
cx q[0], q[1];
h q[0];
// Alice measure
c[0] = measure q[0];
c[1] = measure q[1];
barrier q;

// Bob applies correction based on Alice measure
if (c[1]) x q[2];
if (c[0]) z q[2];
barrier q;

// Check result by undoing preparation on q[2], then if teleportation worked
// q[2] is back to |0>
rz(-pi/4) q[2];
ry(-pi/3) q[2];
result = measure q[2];
