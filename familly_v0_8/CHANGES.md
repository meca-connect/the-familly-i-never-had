# Changes

## v0.8

- Nightly household assignment: chosen hour (default 03:00), 3-hour window,
  up to 3 retries; NTP time kept in the RX8130 RTC; state in NVS.
- Registering or replacing the subject assigns a new household immediately.
- Prompt variety: scene × head pose × camera framing; the model is told not to
  copy the reference's pose, expression, clothing or framing.
- Gallery-style web UI (dashboard, subject, setup) and serif e-ink status cards.
- Persistent caption: household number, scene, time and origin.
- Battery mode: idle timeout, full M5PM1 power-off, scheduled wake via the PM1
  timer (primary) and RX8130 countdown (backup). Self-wake still unverified.
- Diagnostics: `POWERSTATUS`, loop timing, `SLEEPTEST <s>`.

## v0.7

- OpenAI image edits on the device (replaces the earlier free provider).
- Subject photo upload from the dashboard with consent, validation and full
  decode before commit; CSRF-protected routes.
- Fix: non-multipart POSTs to the upload route crashed the web server.

## v0.6 and earlier

- 6-colour serpentine Floyd–Steinberg dithering chosen after an A/B/C test.
- FAM6 frame format on SD; USB transfer and display commands.
- WPA2 setup hotspot with the password shown only on the panel.
