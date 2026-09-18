# UPSTREAM-SYNC.md

Tracks each upstream PR cycle from this fork (radhaksri/rocm-systems) to
ROCm/rocm-systems. One cycle = one entry. Updated by the mivirt P3-R2
recurring task after each cycle completes.

This file lives in the radhaksri/rocm-systems fork, not in ROCm/rocm-systems.

Fork: https://github.com/radhaksri/rocm-systems (branch: develop)
Upstream: https://github.com/ROCm/rocm-systems (branch: develop)


---

## Cycle 1 — gfx942/gfx950 IP-discovery profiles (2026-09-18)

### Fork branch

radhaksri/rocm-systems @ feat/ip-discovery-gfx942-gfx950
Commit: c1fc8a78404d1080efae0c4e105d966151ed69e5

Based on upstream HEAD at time of branching:
  fde7bef17b test(rccl): add host microtests for src/ras/ras.cc

### Upstream PR

Status: OPEN (pending browser-open from radhaksri account — see note)
URL: https://github.com/ROCm/rocm-systems/pull/PENDING

Note: AMD Enterprise Managed User (EMU) policy blocks cross-org PR creation
from rsrimant_amdeng. PR branch is pushed and ready. Open via browser as radhaksri:
  https://github.com/radhaksri/rocm-systems/compare/feat/ip-discovery-gfx942-gfx950

Reviewers: @atgutier, @ROCm/rocjitsu-core-team
mivirt tracking issue: AMD-AIOSS/mivirt#115

### What this PR contains

6 files changed (467 insertions, 15 deletions):

  NEW: lib/rocjitsu/src/rocjitsu/vm/amdgpu/pci/ip_discovery_profile.h
    IpDiscoveryProfile struct + IpDiscoveryProfileRegistry mapping
    gfx_target_version to per-arch GpuDiscovery V3 tables.

  NEW: lib/rocjitsu/src/rocjitsu/vm/amdgpu/pci/ip_discovery_profile.cpp
    GpuDiscovery V3 tables for gfx942 and gfx950:
    - gfx942 (CDNA3, GC 9.4.3): MI300X/MI325X
      8 XCDs, 4 SEs/XCD, 8 CUs/SE, 256 CUs total
      IP blocks: GC, OSSSYS, HDP, SDMA x4, MP0, MP1, THM, SMUIO, VCN
    - gfx950 (CDNA4, GC 9.5.0): MI355X
      4 XCDs, 4 SEs/XCD, 10 CUs/SE, 160 CUs total
      IP blocks: GC, OSSSYS, HDP, SDMA x4, MP0, MP1, THM, SMUIO, VCN

  NEW: tests/pci/ip_discovery_test.cpp
    29 contract tests (all pass). Covers: registry lookup, IP-block
    presence, GC version/instance accuracy, serialised table structure.

  MODIFIED: lib/rocjitsu/src/rocjitsu/vm/amdgpu/pci/gpu_pci_device_spec.cpp
    Wires ip_discovery_profile into the vfio-user device-spec path.

  MODIFIED: cmake/rj_add_device_kernel.cmake
    Adds RJ_HIP_EXTRA_FLAGS passthrough for hermetic builds.

  MODIFIED: .gitignore
    Excludes generated *.deb packages.

### What is NOT upstreamed in this cycle

  - docs/upstream-alignment.md — mivirt-internal guide; not upstream-relevant
  - gap-reporter changes (P3-5) — not yet stabilised
  - MI325/MI450 variant configs — not yet implemented


---

## Template for next cycle

## Cycle N — <title> (YYYY-MM-DD)

### Fork branch

radhaksri/rocm-systems @ <branch-name>
Commit: <sha>

### Upstream PR

Status: <PENDING|OPEN|MERGED|REJECTED>
URL: https://github.com/ROCm/rocm-systems/pull/<N>

### What this PR contains

<description>

### What is NOT upstreamed in this cycle

<rationale>
