# Changelog

## 2026-10-26

- Boot into landscape PIUU_EYES; hold touch over five seconds to open the existing portrait UI.
- Accept bounded UDP v3 and EARS UART eye commands with UDP visual priority and stale-response rejection.
- Render in fixed four-row strips, follow playback for speaking gestures, and document commands and diagnostics.

## 2026-10-12

- Update offline cross-language tests for the backend package layout; firmware unchanged.

- Disable DAC diagnostics by default.
- Add bounded Opus downlink NACK requests and acknowledged audio END boundaries.
- Drain missing tail frames with bounded recovery/concealment; reject stale retries.
- Require downlink capability at handshake and expose recovery counters.

## 2026-10-11

- Add optional bounded DAC onset PCM/write diagnostics without playback-task logging.

- Reserve the recovery pool once at startup and prioritize requested frames.
- Extend bounded uplink NACK recovery and expose successful retries, expiry and recovery counters.
- Add delayed recovery, saturation and cross-language regression coverage.

- Add optional UDP RX scheduling and processing timing probes for downlink loss investigation.

- Add configurable 480 ms EARS PCM queue headroom and drain again after UI work.
- Report PCM queue occupancy, peak and consumer polling gap with UART diagnostics.

- Add optional UDP arrival, playback skip and connection diagnostics; disable performance sampling.
- Classify local UDP send failures and reject failed packet setup or short writes.
- Lower EARS UART RX FIFO threshold to 64 bytes and report it in diagnostics.
- Add optional UART driver error counters and bounded RX/application sequence-gap samples.
