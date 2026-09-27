# Opt-in diagnostics performance backport — 2026-09-24

Branch: `perf/opt-in-diagnostics-20260924`.
Base: `f691f802d9078f6a23b76f4ce25075f7584de288`.
Source: upstream [1ea864712561eed358882f48f53701ce4b078f79](https://github.com/mstan/gbarecomp/commit/1ea864712561eed358882f48f53701ce4b078f79),
`perf(runtime): make costly diagnostic capture opt-in`.

This is a selective compatibility backport, not a merge of upstream main.

## Included

- Runtime branch/write trace recording is disabled by default; existing generated
  and overlay calls return without recording. Runtime dispatch/call helpers also
  guard their calls.
- MMIO capture and audio FIFO trace are opt-in. FIFO storage allocates only when
  enabled; guest FIFO stepping and audio generation remain unchanged.
- Frame-phase timing and present-cadence/DWM inspection are opt-in.
- Hang watchdog is opt-in, matching the upstream performance policy.
- Raw screen-color passthrough avoids initializing an unused color lookup table.
- TCP diagnostic responses report whether tracing is enabled, distinguishing an
  intentionally disabled recorder from a recorder with no events.

Local host composition, input replay and audio-dump behavior are preserved in the
adapted runtime.cpp changes. No renderer, camera, guest timing, frame limiter, or
serialized save-state format changes are part of this backport.

## Explicit diagnostic controls

Set before starting a fresh process:

| Recorder | Enable |
| --- | --- |
| Runtime trace | `GBARECOMP_RUNTIME_TRACE=1` |
| MMIO writes | `GBARECOMP_MMIO_CAP=1` or a `GBARECOMP_MMIO_DUMP` path |
| Audio FIFO trace | `GBARECOMP_AUDIO_FIFO_TRACE=1` |
| Frame phases | `GBARECOMP_FRAME_PHASE=<CSV path>` |
| Present cadence | `GBARECOMP_PRESENT_CADENCE=1` |
| Hang watchdog | `GBARECOMP_HANG_WATCHDOG=1` |

The upstream runtime watchpoint environment variables also enable runtime tracing.
Ordinary F10 captures still work, but these optional historical trace rings are
empty unless explicitly enabled. A hang no longer triggers the opt-in watchdog
by default. For crash/hang investigations, enable the watchdog and runtime trace
before reproducing; this has a performance cost.

## Deferred upstream changes

The arm-recomp-core migration, regenerated inline trace guards and overlay ABI 5
are not included. This branch retains its current overlay ABI and generated guest
corpus. Thus old generated calls still have the small function-call cost even with
recording disabled. Do not claim this contains every optimization in the upstream
commit or that overlays were regenerated with the newer emitter.

## Verification

Both `diagnostic_capture_tests_disabled` and `diagnostic_capture_tests_enabled`
pass: guest MMIO writes/FIFO samples commit in both modes, trace queries reflect
their enabled state, and enabled audio trace copies/reset behave correctly.
`codegen_tests` also checks the runtime recorder's disabled/enabled gate.
The consuming game's complete-frame field/combat state suite and paired guest-state
validation passed. See its `docs/UPSTREAM_PERFORMANCE_20260924.md` for build identity,
scope and actual windowed measurements; headless throughput is not displayed FPS.
