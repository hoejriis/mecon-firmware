# Public device contract

The contracts a manager implements to manage mecon-firmware devices. They need no particular Backend, broker or domain, and no knowledge of any manager's private source. A device-visible behaviour these pages do not specify is a documentation defect.

## Normative pages

| Page | Defines |
|---|---|
| [Device interface](DEVICE_INTERFACE.md) | Identity, versions, the envelope, framing over USB, BLE, MQTT and the console, settings, the base operations, resource bounds. Wins on wire details where another page differs |
| [Management authority](AUTHORITY.md) | Deployments, authority sets and rotation, signed requests and results, replay, caller kinds, local unlock, enrolment changes and recovery |
| [Managed operations](OPERATIONS.md) | Every operation, with its class, roles and transports; jobs, owed results, confirm-or-revert and reset scopes |
| [Telemetry](TELEMETRY.md) | The stream model, every stream and its fields, access and limits |
| [Signed updates](UPDATES.md) | Release anchors and manifests, preflight, who may offer, delivery per target, the update lifecycle |
| [Management channel](MANAGEMENT_CHANNEL.md) | Binding one native channel as the management channel, with commit-or-revert |
| [Management over LoRa](LORA_MANAGEMENT.md) | The device interface over point-to-point and channel LoRa (draft) |
| [Security and authority](SECURITY_AND_AUTHORITY.md) | Security rules that the pages above cite, such as continuity material and BLE bonds |

The [schemas](schema) and [fixtures](fixtures) belong to the device interface. `release-keys.json` lists the release keys that builds trust ([signed updates](UPDATES.md)).

What a given device supports is never inferred from these pages: `hello` lists every operation and stream its build, board and role can run. What each release contains is in its release notes.

## Principles

1. MeshCore 1.18 native behaviour is reused where suitable. The contract marks the boundary rather than inventing duplicates.
2. The MeshCore CLI and configuration are the management plane for native behaviour. The interface extends them rather than duplicating them.
3. USB, BLE, MQTT and LoRa are transports for the same operations, never separate products.
4. Operations are allowlisted and versioned. There is no raw shell, even though CLI semantics are canonical.
5. The contracts are backend-neutral.
6. Capabilities and independently versioned profiles describe behaviour.
7. Authority is explicit and least-privilege. A transport is never authority.
8. A state-changing job has a stable identity, an explicit result and replay protection.
9. Unknown fields in a request envelope or a result are ignored, and an unknown operation is refused. Authority that is not understood is never granted. Signed objects (authority sets, signed requests, release manifests) are verified over their exact bytes, so a field added to one is either signed or rejected, never trusted ([authority](AUTHORITY.md) section 2).
10. Resource limits, such as contact capacity, are reported by the running build, not assumed.

## Background pages

- [Second authority review: decisions](AUTHORITY_REVIEW_2.md): the outcome of a security review of the authority layer. Where it differs from a normative page, the normative page wins.
