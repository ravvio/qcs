// Copies one state into another of the same size

const workgroup_size = 64u;

@group(0) @binding(1) var<storage, read_write> from_state: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> to_state: array<vec2f>;

@compute @workgroup_size(workgroup_size)
fn comp_main(@builtin(workgroup_id) group: vec3u,
             @builtin(num_workgroups) groups: vec3u,
             @builtin(local_invocation_index) local: u32) {
    let i = (group.y * groups.x + group.x) * workgroup_size + local;
    if (i >= arrayLength(&to_state)) {
        return;
    }
    to_state[i] = from_state[i];
}
