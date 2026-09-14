# MQTT protocol

## Namespace

Each broker profile supplies its own `namespace`. No service-specific namespace is compiled into the public firmware.

Canonical topics are:

```text
{namespace}/{device_id}/status
{namespace}/{device_id}/observations
{namespace}/{device_id}/events
{namespace}/{device_id}/inventory
{namespace}/{device_id}/results
{namespace}/{device_id}/config/schema
{namespace}/{device_id}/config/get
{namespace}/{device_id}/config/apply
{namespace}/{device_id}/commands
{namespace}/{device_id}/ota
```

A broker profile may be granted only a subset of these surfaces.

## Envelope

Every MECON message includes at least:

```json
{
  "protocol": "mecon",
  "protocol_version": 1,
  "device_id": "...",
  "message_id": "...",
  "timestamp": "..."
}
```

Jobs additionally carry a stable `job_id`, operation, expiry where appropriate and parameters. Results carry the same `job_id` and explicit state.

## Results

The common job lifecycle is:

- `accepted`
- `succeeded`
- `failed`

Long-running operations may publish intermediate progress without changing the meaning of the terminal states. Duplicate delivery must not execute a completed job twice.

## Observations

Packet observations preserve raw MeshCore packet bytes plus receiver metadata such as RSSI/SNR and reception time. Observations may be dropped while a device has no authorized transport; the firmware does not promise an unbounded offline packet queue.

## Messaging

Companion builds may expose send/receive messaging through the `messaging` profile. Sending requires explicit broker authority. A backend must not infer transmit authority from observation access.

## Multi-broker behavior

The same device may connect to two brokers. Each broker has its own namespace, credentials and grants. Message/job identifiers are scoped so replay protection remains correct across both brokers. The firmware must not execute the same logical job twice merely because it arrived through two authorized paths.

## Compatibility

Profile and protocol versions determine wire compatibility. Unknown fields are ignored. Unsupported operations return an explicit failure when the envelope is sufficiently valid to identify the job.