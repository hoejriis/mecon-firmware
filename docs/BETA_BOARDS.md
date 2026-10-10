# Hosted beta: tested hardware and firmware

*Updated 10 October 2026.*

This page lists what has actually run on real hardware for the first hosted beta. A board that is not listed as tested is not offered, even if the firmware compiles for it. Inclusion here is not a promise of future support.

The builds are private beta builds; this repository still publishes no firmware downloads (see the [README](../README.md)).

## Offered for the beta (beta firmware 0.77.0)

| Board | Role | Connection | Tested on real hardware |
|---|---|---|---|
| Heltec V3 | Companion gateway | Wi-Fi / MQTT, set up over USB | Remote update and its automatic fallback on this release; messaging, settings and broker failover on recent releases |
| Heltec V4 (not the R8 revision) | Companion gateway | Wi-Fi / MQTT, set up over USB | Remote update on this release; messaging and broker failover on recent releases |

Each holds 50 contacts.

## Not offered yet

| Board | Why |
|---|---|
| SenseCAP T1000-E (Bluetooth companion) | Experimental. Settings over Bluetooth work on this release; it updates over USB only. |
| Heltec V3 repeater | No hardware test of the current release. |
| Wio Tracker L1 repeater | The current release has not yet been tested on the device. |
| Heltec V4 R8, and every other board | Not hardware-tested: build-tested only. |

## Limits

- The Wi-Fi companion port on a gateway is off by default. If it is switched on, it is not password protected, so use it only on a network you trust.
- The T1000-E cannot be updated over the air; it updates over USB.

## Recovery

If a remote update fails, the gateway returns to its previous firmware by itself. A step-by-step recovery guide will be linked here before the beta opens.

## See it in use

The [MeshContinuum demonstration](https://github.com/hoejriis/MeshContinuum#one-reader-more-reception) shows the Reader with two Heltec gateways (V3 and V4). Its screenshots use synthetic demo data and were taken on an earlier beta firmware (0.76.0); they show those two boards only and say nothing about any other board or role.

This list changes when new hardware tests pass.
