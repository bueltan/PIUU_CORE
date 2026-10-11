# Changelog

## 2026-10-11

- Add optional UDP arrival, playback skip and connection diagnostics; disable performance sampling.
- Classify local UDP send failures and reject failed packet setup or short writes.
- Lower EARS UART RX FIFO threshold to 64 bytes and report it in diagnostics.
- Add optional UART driver error counters and bounded RX/application sequence-gap samples.
