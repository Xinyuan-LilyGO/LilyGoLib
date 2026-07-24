# Changelog

## Unreleased

- Narrowed the project to the `vk-daemon` runtime and removed bundled client, rendering, and
  process-local device components.
- Defined a stable loopback HTTP contract for external simulators: device state, sessions,
  notifications, button input, knob input, and permission decisions.
- Kept Bluetooth LE as the remote device boundary, with the daemon as central and hardware or
  simulators as peripherals using fixed GATT UUIDs.
- Added authoritative HTTP and BLE integration documentation.
- Added macOS microphone and synthesized-event consent requests at daemon startup, authorization
  diagnostics, and actionable Voice/Dictation errors when the host application lacks access.

## 0.1.0

- Initial independent Python implementation.
- Ported core domain types, protocol codec, transports, display, input mocks, UI state machine,
  simulator, daemon, setup hooks, and desktop launcher.
- Added daemon-hosted browser dashboard as a fallback for Python builds without Tk.
