# Changelog

## 2026-10-28

- Separate actual WiFi connectivity from queued connect/scan jobs; preserve active conversations and UDP identity during connected scans.
- Pause Arduino auto-reconnect during explicit WiFi operations and restore its previous policy before publishing results.
- Cancel timed-out connection attempts before another job; stop a new connect/scan when cancellation fails.
- Remove unconditional server-link resets after WiFi job completion and replace misleading password-error guidance.
- Advertise the stable WiFi station MAC in HELLO (`CAP_DEVICE_MAC=0x0008`) while retaining independent session nonces; require backend confirmation.
- Renew the session nonce after six seconds without server traffic even while idle, enabling recovery after a NAT address change.
- Validate MAC negotiation, idle recovery and scan/conversation isolation with host and CORE/backend wire tests; compile `cyd-uart0` successfully.
- Keep framework versions, RTOS task affinity and priorities unchanged. The UDP receive-lock failure remains a separate unresolved issue; these changes have not been flashed or validated on hardware in this chat.

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
