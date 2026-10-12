# Changelog

## 2026-10-11

- Add configurable 480 ms EARS PCM queue headroom and drain again after UI work.
- Report PCM queue occupancy, peak and consumer polling gap with UART diagnostics.

- Add optional UDP arrival, playback skip and connection diagnostics; disable performance sampling.
- Classify local UDP send failures and reject failed packet setup or short writes.
- Lower EARS UART RX FIFO threshold to 64 bytes and report it in diagnostics.
- Add optional UART driver error counters and bounded RX/application sequence-gap samples.
