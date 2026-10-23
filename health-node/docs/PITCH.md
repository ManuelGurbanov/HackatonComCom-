# One-page pitch — Machine Health Listening Node

## Problem

Rotating and reciprocating machines — HVAC fans, pumps, compressors, elevator
motors and doors, conveyors — fail in ways you can *hear* long before they stop:
a new rattle, a shifting hum, a rising hiss, a belt squeal. Today that early
signal is caught only when a human happens to walk past, or not at all until a
breakdown. Dedicated vibration-sensor programs exist but cost hundreds of
dollars per point plus installation, so most equipment in most buildings is
simply unmonitored.

## Solution

A cheap node that **listens**. Place an ESP32-S3 with a microphone near a
machine, press "learn normal", and it builds an acoustic baseline of that
specific machine. From then on it continuously scores how far the live sound has
drifted from normal (0–100 health), tracks tonal peaks for new/shifted tones, and
raises WARNING / ALERT states with hysteresis so they don't flicker. Four nodes
report over ESP-NOW to a gateway board that serves a live web dashboard — health
scores, live spectra, per-machine history, and an alert log — from a phone or
laptop, no cloud or wiring required.

## Market

- **Facilities / HVAC:** fans, AHUs, pumps, chillers in commercial buildings.
- **Elevators:** motor and door-mechanism noise, a classic ride-quality and
  safety concern.
- **Light manufacturing / factories:** conveyors, compressors, gearboxes —
  first-pass screening across many low-value machines that never justified a
  vibration program.

## Differentiator

- **Cheap** — a few dollars of ESP32 + mic vs. hundreds per vibration point.
- **Retrofit** — sticks near any existing machine; nothing to bolt or rewire.
- **Always-on & self-learning** — learns each machine's own normal, no modelling.
- **Networked out of the box** — ESP-NOW mesh to one gateway, local dashboard,
  easy bridge to MQTT for a real deployment.

## Honest limitations

- **Airborne sound is a screening layer, not a vibration sensor.** A contact
  accelerometer sees structure-borne and ultrasonic signatures (early bearing
  wear) that a cheap mic cannot. We find "this machine sounds different from
  normal", not a specific diagnosed fault.
- **No accuracy numbers yet.** Our tests use synthetic signals and prove only
  that the math responds in the right direction. Detection rate, false-alarm
  rate, and lead time all require real recordings per machine class before we can
  quote them.
- **Environment-sensitive.** Nearby louder machines, clipping at high SPL, wind
  on the mic port, and the mic's own frequency response all bound performance;
  these must be verified at each install (see HARDWARE.md).
- **Range.** ESP-NOW is room-scale indoors; large plants need repeaters.

**The pitch in one line:** an always-on, self-learning acoustic early-warning
layer that makes predictive maintenance cheap enough to put on machines nobody
monitors today — honest about being a first screen, not the final word.
