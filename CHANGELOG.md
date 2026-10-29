# Changelog

## 2026-10-29

- Add optional negotiated CAP_NOTIFICATIONS=0x0010 / NOTICE=28 for idle-only backend reminders; CORE chooses the new conversation ID and sends a correlated START.
- Reject foreign, unnegotiated, busy and duplicate notices; mute microphone input until reminder playback and abort an initial notification response wait after 20 seconds.
- Pass CORE host tests and a Python NOTICE -> native CORE START wire interoperability check; compile cyd-uart0 successfully. Firmware deployment and real Calendar notification playback remain pending.

- Extend the explicit WiFi connection wait from 15 to 60 seconds as a diagnostic trial, preserving a single begin and existing cancellation/auto-reconnect coordination.
- With WiFi diagnostics enabled, log association, RSSI, ESP-NETIF DHCP status/IP and query errors every five seconds and before the final result; do not restart DHCP or change radio settings.
- Test delayed 20-second success, bounded 60-second failure, no extra begin, DHCP observations and clock rollover; document CORE-only deployment and interpretation.

- Add independent `PIUU_PLAYBACK_DIAGNOSTICS`: four loop-side summaries every five seconds, spaced by 100 ms, covering RX/decoding, jitter/END admission, playback phase/event delivery and output write duration/error/bytes.
- Observe playback progress through fixed atomic counters, including an output call that has not returned; snapshot jitter with a zero-wait mutex and format after releasing it.
- Add blocked-output/concurrent-observer, short/error write and clock-rollover probe tests; retain existing playback/recovery behavior and document interpretation and CORE-only update.

- Route application logs through framed CORE→EARS UART→USB by default in UART0 environments; replace the UDP logger task/socket with a four-chunk static queue and existing UART worker.
- Add CRC-protected diagnostic packets with boot ID, sequence, uptime, dropped bytes and reset reason; prioritize commands/RX and cap text at 4.8 kB/s.
- Drain retained WiFi events without connectivity; report WiFi job memory/stack and periodic build/internal-memory status.
- Add `capture_serial.py --only ears` and cross-firmware regressions for concurrent producers, saturation, CRC and continued audio control; document update order and RAM/panic limits.

## 2026-10-28

- Retry an unread UDP datagram via `available()` after an RX mutex timeout or zero-byte read; prevent NetworkUDP's buffered-packet state from wedging future polling.
- Discard oversized and partially read datagrams whole, including their tails, while keeping the existing mutex deadlines and allocation/task layout.
- Add optional pending/oversized/short RX diagnostics and a concurrent sender regression that fails before the fix, with diagnostics both on and off.

- Separate actual WiFi connectivity from queued connect/scan jobs; preserve active conversations and UDP identity during connected scans.
- Pause Arduino auto-reconnect during explicit WiFi operations and restore its previous policy before publishing results.
- Cancel timed-out connection attempts before another job; stop a new connect/scan when cancellation fails.
- Remove unconditional server-link resets after WiFi job completion and replace misleading password-error guidance.
- Advertise the stable WiFi station MAC in HELLO (`CAP_DEVICE_MAC=0x0008`) while retaining independent session nonces; require backend confirmation.
- Renew the session nonce after six seconds without server traffic even while idle, enabling recovery after a NAT address change.
- Validate MAC negotiation, idle recovery and scan/conversation isolation with host and CORE/backend wire tests; compile `cyd-uart0` successfully.
- Keep framework versions, RTOS task affinity and priorities unchanged. The UDP receive-lock failure is corrected above; hardware microcut validation remains pending.

- Use provider inactivity for session closure; prevent ambient energy from renewing the deadline.
- Bound stale speech events and test continuous-noise timeout recovery.

- Qualify local activity above 600 for 200 ms; close idle listening even with stale provider speech.
- Add independent conversation diagnostics and noise/stale-speech regression tests.

- Enable bounded LAN UDP diagnostics with SD disabled in UART0 builds.
- Add a Python capture tool with loss/reboot detection and wire/localhost tests.

## 2026-10-27

- Defer SD mounting until audio services are ready; retain internal RAM headroom during mount.

- Reduce SD logging to one open file and eight queued chunks; report DMA heap and SD latency/stack headroom.
- Preserve the experimental UDP logger disabled while validating SD/audio coexistence.

- Suppress microphone uplink and wake triggers during replies with a 250 ms echo guard.
- Add bounded asynchronous microSD application logs and file rotation for UART0 builds.
- Test muted PCM, wake suppression, playback resume and continuous uplink sequence.

- Add UART0 EARS build variants on GPIO3/1 with application logs muted.

- Flip eyes to landscape rotation 3 and align touch coordinates.
- Adapt all settings screens and keyboard to 320x240; add an EYES return button.

## 2026-10-26

- Negotiate and acknowledge bounded UDP eye plans on a consumed-audio timeline.
- Freeze plan animation during rebuffer; reject stale/conflicting plans and suppress duplicate gestures.
- Add CORE/backend wire round-trip tests and integrated validation guidance.

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
