# Upstream alignment: rocjitsu contribution guide for mivirt

This document captures the rocjitsu contribution conventions that all mivirt device-server
engineers must follow, so that every change made in the `radhaksri/rocm-systems` fork
remains upstream-mergeable into `ROCm/rocm-systems`.

Upstream source of truth:
  https://github.com/ROCm/rocm-systems/tree/develop/emulation/rocjitsu

Fork (mivirt dev surface):
  https://github.com/radhaksri/rocm-systems  (branch: develop)

Maintainers / code owners (upstream):  @atgutier, @ROCm/rocjitsu-core-team
mivirt contact:  rsrimant@amd.com


## Tracking alignment

Issues are disabled on the fork; the upstream repo (ROCm/rocm-systems) requires an
AMD Enterprise Managed User account to create issues.  Contact is maintained through
AMD internal channels with the rocjitsu core team (@atgutier / @ROCm/rocjitsu-core-team).

For architectural proposals (new subsystem, KFD ioctl interface change, RPC protocol
change) the process is:
  1. Discuss with rsrimant@amd.com and the core team before writing code.
  2. Open a proposal issue or design doc on ROCm/rocm-systems once access permits.
  3. Wait for maintainer sign-off before landing the fork PR.

For localized improvements, bug fixes, test additions, and new GPU configs: open a
fork PR directly — no proposal step required.


## Where things live (source-tree map)

The canonical map lives in emulation/rocjitsu/CONTRIBUTING.md.  Reproduced here for
quick reference:

  What you are adding                      Where it goes
  -----------------------------------------  ------------------------------------------
  GPU hardware component (cache, CP, queue)  lib/rocjitsu/src/rocjitsu/vm/amdgpu/
  ISA instruction semantics                  lib/python/amdisa/codegen/_generator.py
                                               (regenerate; never edit generated files)
  Generated ISA sources / insts.h            lib/rocjitsu/src/rocjitsu/isa/arch/amdgpu/generated/<target>/
  Hand-written ISA support (addr, mma)       lib/rocjitsu/src/rocjitsu/isa/arch/amdgpu/<target>/
  KFD ioctl or interposer hook               lib/rocjitsu/src/rocjitsu/kmd/linux/
  Binary translation rule / expansion        lib/rocjitsu/src/rocjitsu/code/dbt/
  Binary instrumentation pass                lib/rocjitsu/src/rocjitsu/code/patch/
  Code object loading / ELF mutation         lib/rocjitsu/src/rocjitsu/code/
  Register liveness / def-use analysis       lib/rocjitsu/src/rocjitsu/code/analysis/
  Simulation engine (PDES, topology)         lib/simdojo/
  Shared utility (bit ops, logging, SIMD)    lib/util/
  JSON config schema change                  schemas/  +  lib/rocjitsu/src/rocjitsu/config/
  CLI changes                                tools/rocjitsu/
  New GPU topology config                    configs/
  Tests                                      tests/  (mirror source: tests/dbt/, tests/vm/, etc.)


## Code style rules

  - C++20.  Use standard library features (concepts, ranges, std::format).
  - Formatting: clang-format (C++), black (Python), gersemi (CMake) via pre-commit.
    Install once:  pip install pre-commit && pre-commit install
    Run before every commit:  pre-commit run --all-files
    Config lives at repo root (rocm-systems/.pre-commit-config.yaml).
  - Naming: clear, descriptive names; no single-letter variables.
    Non-trivial types with methods must be 'class' (not 'struct').
    Maintain public / protected / private ordering.
  - Comments: Doxygen @brief / @details for public APIs.
    Default to no comments; comment only when the *why* is non-obvious.
    No decorative lines (---  ===).
  - Logging: Logger from util/log.h only.
    Logger::print<>() / per-group helpers compile to nothing unless the group is
    enabled at configure time (-DRJ_LOG_GROUPS=...).  Use Logger::warn() for
    always-on messages.  No fprintf / printf / std::cerr in library code.
    Two carve-outs: tools/ (user-facing errors to std::cerr) and hooks/ (fatal
    diagnostics to raw stderr for async-signal-safety).
  - Errors: return Result or FailureOr<T> for expected failures; pass a
    DiagnosticEmitter for structured reasons.  Exceptions (util/except.h) only for
    unrecoverable initialization / configuration / execution failures.  Never throw
    in simulation hot paths (event handlers, instruction execution, cache lookups).
  - Zero compiler warnings before submitting.

## Existing utility libraries (check before writing new infrastructure)

  lib/util/log.h          — Logger (tracing + always-on warn)
  lib/util/bit.h          — Bit masks, extraction, insertion
  lib/util/bitfield.h     — Typed bitfield access
  lib/util/data_types.h   — FP16/BF16/FP8/BF8/FP4/FP6 conversions
  lib/util/simd.h         — <experimental/simd> with scalar fallback
  lib/util/intrusive_list.h  — O(1) insert/erase linked structures
  lib/util/arena_alloc.h  — Fixed-size block pools
  lib/util/except.h       — Unrecoverable-failure exceptions
  isa/register_set.h      — ISA-independent register file modeling (SGPR/VGPR/AccVGPR)
  code/patch/spill_manager.h        — DBI spill/fill slot layout
  code/patch/instruction_builder.h  — Encoding helpers (s_branch, s_nop, ...)


## ISA codegen workflow

Never manually edit generated files.  When modifying instruction semantics:
  1. Edit  lib/python/amdisa/codegen/_generator.py
  2. Regenerate:  scripts/generate-amdisa.sh  (also runs pre-commit on generated files)
  3. Stage ALL generated files in the same commit as the generator change.

See emulation/rocjitsu/docs/codegen.md for the full pipeline.


## Testing discipline

  - Add tests for new functionality in tests/ mirroring the source structure.
  - ctest --test-dir build
  - cd lib/python && python -m pytest amdisa/tests/ -x
  - Never commit untested or broken code.
  - Commit messages: concise, focused on the *why*, not the *what*.


## Proposing architectural changes (required before implementation)

Changes that require a proposal issue first:
  - New translation pair or instrumentation pass
  - Changing the KFD ioctl interface or RPC protocol
  - Restructuring the simulation engine or cache hierarchy
  - Introducing new external dependencies

The proposal must include: Motivation, Design, Alternatives considered,
Migration / compatibility, Scope.  Wait for maintainer approval before coding.

Bug fixes, test additions, and localized improvements: open a PR directly.


## Config schema and NPI ("add a GPU") rules

### Config schema overview

rocjitsu device topology configs are JSON files validated against FlatBuffers schemas
in  emulation/rocjitsu/schemas/simulation_config.fbs.

Top-level structure of a simulator config (KFD daemon mode):

  {
    "thread_allocations": [
      {"num_threads": <int>, "cpu_dispatch_threads": <int>},
      ...
    ],
    "max_ticks": <int>,           // 0 = unlimited (use 0 for KFD mode)
    "exec_mode": "functional",    // "clocked" for cycle-accurate; "functional" is default
    "vm": {
      "arch": "<cdna3|cdna4|cdna5>",
      "gpu": {
        "num_gpus": <int>,        // 1 for single-GPU configs
        "device": { ... }         // KfdDeviceInfo fields (see below)
      }
    },
    "topology": {
      "root": {
        "name": "soc", "type": "soc",
        "children": [
          { "name": "vram", "type": "gpu_memory" },
          {
            "name": "xcd[0:<N>]", "type": "xcd",
            "children": [
              { "name": "l2", "type": "l2_cache" },
              { "name": "cp", "type": "command_processor" },
              {
                "name": "se[0:<M>]", "type": "shader_engine",
                "children": [{
                  "name": "cu[0:<K>]", "type": "compute_unit",
                  "config": [
                    { "key": "num_wf_slots",  "value": "32" },
                    { "key": "sgprs_per_wf",  "value": "104" },
                    { "key": "vgprs_per_wf",  "value": "512" },
                    { "key": "lds_size_kb",   "value": "64" }
                  ]
                }]
              }
            ]
          }
        ]
      },
      "links": [
        {
          "pattern": "xcd[i].cp.req_[j*<K>+k] -> xcd[i].se[j].cu[k].cpl",
          "for_ranges": [ ... ],
          "latency": 1, "weight": 2
        },
        {
          "pattern": "xcd[i].se[j].cu[k].req -> xcd[i].l2.cpl_[j*<K>+k]",
          "for_ranges": [ ... ],
          "latency": 1, "weight": 10
        }
      ]
    }
  }

### KfdDeviceInfo fields (vm.gpu.device)

These map to /sys/class/kfd/kfd/topology/nodes/<N>/properties, which ROCR, HIP,
rocprofiler, and AMD SMI use for GPU discovery.  Field values come from:
  - Real hardware:  cat /sys/class/kfd/kfd/topology/nodes/*/properties
  - KFD kernel driver:  drivers/gpu/drm/amd/amdkfd/kfd_topology.c
  - ROCR libhsakmt:  projects/rocr-runtime/libhsakmt/src/topology.c
  - PCI device IDs:  https://pci-ids.ucw.cz/read/PC/1002  (vendor 0x1002 = AMD)

  Field                      Type    Notes
  -------------------------  ------  --------------------------------------------------
  gpu_id                     uint32  KFD GPU ID (e.g. 38144 for gfx950)
  gfx_target_version         uint32  Packed GFX version (e.g. 90500 for gfx950)
  vendor_id                  uint32  0x1002 (AMD) = 4098 decimal
  device_id                  uint32  PCI device ID from pci-ids.ucw.cz
  family_id                  uint32  AMDGPU_FAMILY_* from amdgpu_drv.c
  unique_id                  uint64  PSP-generated; use a distinctive constant for sim
  marketing_name             string  "AMD Instinct MI350X" etc.
  drm_render_minor           uint32  Default 128
  revision_id                uint32  Default 0
  pci_revision_id            uint32  Default 0
  simd_count                 uint32  Total SIMDs = XCDs * SEs_per_XCD * CUs_per_SE * 4
  max_waves_per_simd         uint32  8 for CDNA
  num_shader_engines         uint32  SEs per XCD (matching topology se[] count)
  num_shader_arrays_per_engine uint32 1 for CDNA
  num_cu_per_sh              uint32  CUs per SE
  simd_per_cu                uint32  4 for CDNA; 2 for RDNA
  wave_front_size            uint32  64 for CDNA; 32 for RDNA
  max_slots_scratch_cu       uint32  32
  local_mem_size             uint64  VRAM in bytes
  lds_size_kb                uint32  64 for CDNA1/2; 128 for CDNA3/4 and RDNA2+
  mem_width                  uint32  HBM interface width in bits (e.g. 8192)
  mem_clk_max                uint32  Max HBM clock MHz
  l1_size_kb / l1_line_size / l1_assoc  Cache geometry
  l2_size_kb / l2_line_size / l2_assoc  Cache geometry
  num_sdma_engines           uint32  Non-XGMI SDMA engines
  num_sdma_xgmi_engines      uint32  XGMI SDMA engines
  num_sdma_queues_per_engine uint32  REQUIRED to be non-zero when num_sdma_engines > 0
  num_cp_queues              uint32  Total compute queue slots
  max_engine_clk_fcompute    uint32  Max compute clock MHz
  location_id                uint32  PCI BDF: 0x0300 = bus 3, dev 0, func 0
  hive_id                    uint64  XGMI hive ID (shared by GPUs on same node)
  domain                     uint32  PCI domain (default 0)
  vram_type                  uint32  6 = HBM; 9 = GDDR6
  capability                 uint32  0 = auto-computed from gfx_target_version
  num_sdma_queues_per_engine uint32  Must be non-zero when num_sdma_engines > 0

### PciDeviceInfo fields (vm.gpu.pci) — vfio-user path

Used by the vfio-user device-boundary front end (the path mivirt uses).

  Field                    Type    Default    Notes
  -----------------------  ------  ---------  ----------------------------------------
  class_code               uint32  0x120000   Processing accelerator; amdgpu binds on this
  subsystem_vendor_id      uint32  0          0 = same as vendor_id
  subsystem_id             uint32  0          0 = same as device_id
  vram_aperture_bytes      uint64  0          0 = implementation-chosen, <= 256 MiB
  doorbell_aperture_bytes  uint64  2097152    2 MiB
  register_aperture_bytes  uint64  524288     512 KiB; must reach offset 0x5a800 for IP-discovery

### Existing configs (reference)

  File                             Arch    Marketing name          Notes
  -------------------------------  ------  ----------------------  -----------------------
  gfx90a_mi210_kmd.json            CDNA2   AMD Instinct MI210      Single GPU, KFD
  gfx942_cdna3.json                CDNA3   AMD Instinct MI300X     Single GPU, standalone
  gfx942_cdna3_kmd.json            CDNA3   AMD Instinct MI300X     Single GPU, KFD
  gfx950_mi355x.json               CDNA4   AMD Instinct MI355X     Single GPU, standalone
  gfx950_mi355x_kmd.json           CDNA4   AMD Instinct MI350X     Single GPU, KFD
  gfx950_mi355x_kmd_2gpu.json      CDNA4   AMD Instinct MI350X     Two GPU, KFD
  gfx1250_mi455x.json              CDNA5   AMD Instinct MI455X     Single GPU, standalone
  gfx1250_mi455x_kmd_4gpu.json     CDNA5   AMD Instinct MI455X     Four GPU, KFD
  gfx1100_w7900.json               RDNA3   AMD Radeon PRO W7900    Single GPU, standalone
  gfx1151.json                     RDNA3.5 (unnamed)               Single GPU, standalone
  gfx1201_r9700.json               RDNA4   AMD Radeon RX 9700      Single GPU, standalone

Multi-GPU configs pin "num_threads": 1 to avoid a known RCCL hang on AllReduce /
Broadcast / AllGather / ReduceScatter with more than one engine partition.

### NPI ("add a GPU") checklist

When introducing a new GPU profile (follows docs/npi.md):

  [ ] Run scripts/find-npi-tasks.sh to locate \\NPI markers in source
  [ ] Construct configs/<gfx>.json (standalone simulation, no KFD)
  [ ] Construct configs/<gfx>_kmd.json (daemon/KFD mode)
  [ ] Construct configs/<gfx>_kmd_<N>gpu.json if multi-GPU is needed
  [ ] For a brand-new ISA family:
        - Sync shared/machine-readable-isa with download.py
        - Add parser compatibility for the new snapshot
        - Regenerate ISA/DBT sources (scripts/generate-amdisa.sh, see codegen.md)
        - Author hand-written per-arch files under
          lib/rocjitsu/src/rocjitsu/isa/arch/amdgpu/<isa>/
            - isa.h
            - mma_exec.h
            - addr_calc.h / addr_calc.cpp
        - Generated files land under
          lib/rocjitsu/src/rocjitsu/isa/arch/amdgpu/generated/<isa>/
  [ ] Define the ISA target provider (see docs/isa-target-providers.md)
  [ ] Register the target provider in each executable / shared object that needs it
  [ ] Add HIP kernel coverage under tests/kernels/
  [ ] Do NOT leak NPI (new product) information; strip in pre-submit

mivirt arches in scope: gfx942 (MI300X, MI325X), gfx950 (MI350X, MI355X),
gfx1100 (MI450), gfx1250 (MI455X).  All four ISA families already have base configs;
the mivirt NPI work is limited to KFD-mode config validation, pci fields, and any
per-arch vfio-user extensions discovered by the gap-reporter loop.


## Rebase policy

The fork is NOT auto-rebased on upstream develop.  The pinned SHA is a parity
coordinate: the harness measures parity against that exact build.  Rebase is a
deliberate human action, taken:
  - At layer-seal boundaries (when a parity layer is sealed)
  - On demand when an upstream fix is needed

Procedure:
  1. Identify the upstream develop SHA to rebase onto.
  2. Run the parity harness against the new base to detect regressions.
  3. Resolve conflicts; re-run harness to green.
  4. Update the submodule SHA in mivirt and open a PR.


## Fork PR discipline (mivirt -> upstream path)

Every mivirt-owned change in the fork must be:
  1. Upstream-mergeable: follows rocjitsu CONTRIBUTING.md, zero warnings,
     tests included, pre-commit clean.
  2. Proposed before coding when it is an architectural change (see above).
  3. Opened as a focused PR — one gap, one config, one ioctl per PR.
  4. Validated by the mivirt parity harness before marking "ready for upstream."
  5. Self-reviewed for NPI leaks before any external sharing.

Upstream PR target: ROCm/rocm-systems, base branch develop.
Fork PR target:     radhaksri/rocm-systems, base branch develop.
