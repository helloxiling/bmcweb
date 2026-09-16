# ComponentIntegrity and Composite EAT

## Status

This is an OpenBMC reference implementation for Redfish ComponentIntegrity and
platform-scoped Composite EAT retrieval. Both features are disabled by default
and require explicit build-time enablement.

The Composite EAT Bundle API is currently implemented as an OpenBMC OEM
extension. It will be proposed for Redfish standardization. Until a standard
API is defined, clients should use the OEM schema and routes documented here.

The public API and D-Bus contracts are platform-neutral. Device enumeration,
transport topology, measurement selection, signing policy, and endorsement
material remain the responsibility of the producer behind the D-Bus boundary.

## Build Options

Enable ComponentIntegrity resources with:

```text
-Dredfish-component-integrity=enabled
```

Enable the Composite EAT Bundle extension with:

```text
-Dredfish-composite-eat=enabled
```

`redfish-composite-eat` depends on `redfish-component-integrity`. Both options
are disabled by default.

## Redfish Resources

| Method | URI | Purpose |
|---|---|---|
| `GET` | `/redfish/v1/ComponentIntegrity` | List ComponentIntegrity resources |
| `GET` | `/redfish/v1/ComponentIntegrity/{ComponentIntegrityId}` | Read one ComponentIntegrity resource |
| `POST` | `/redfish/v1/ComponentIntegrity/Actions/Oem/OpenBMCCompositeEATBundle.Generate` | Start platform Composite EAT generation |
| `GET` | `/redfish/v1/ComponentIntegrity/CompositeEATBundle` | Read the current generation state or completed bundle |

The ServiceRoot advertises `/redfish/v1/ComponentIntegrity` when
`redfish-component-integrity` is enabled.

## ComponentIntegrity D-Bus Contract

bmcweb discovers objects implementing:

```text
xyz.openbmc_project.Attestation.ComponentIntegrity
```

under `/xyz/openbmc_project`. The final D-Bus object-path segment becomes the
Redfish `ComponentIntegrityId`.

Each object provides these properties:

| D-Bus property | D-Bus type | Redfish property |
|---|---|---|
| `Enabled` | `b` | `ComponentIntegrityEnabled` |
| `Type` | enum string | `ComponentIntegrityType` |
| `TypeVersion` | `s` | `ComponentIntegrityTypeVersion` |
| `LastUpdated` | `t` | `LastUpdated` |

`LastUpdated` is expressed in milliseconds since the Unix epoch. A value of
zero omits the Redfish property.

The implementation maps the following security technology types:

| D-Bus enum suffix | Redfish value |
|---|---|
| `.SPDM` | `SPDM` |
| `.OEM` | `OEM` |

Other values fail closed. An enabled SPDM resource includes an `SPDM.Requester`
link to the Redfish Manager resource.

### Target Association

Each ComponentIntegrity object must expose exactly one `authenticating`
association endpoint. bmcweb maps the associated inventory object to
`TargetComponentURI` as follows:

| Inventory interface | Redfish target |
|---|---|
| `xyz.openbmc_project.Inventory.Item.Board` | `/redfish/v1/Chassis/{id}` |
| `xyz.openbmc_project.Inventory.Item.Chassis` | `/redfish/v1/Chassis/{id}` |
| `xyz.openbmc_project.Inventory.Item.Cpu` | `/redfish/v1/Systems/{system}/Processors/{id}` |
| `xyz.openbmc_project.Inventory.Item.Dimm` | `/redfish/v1/Systems/{system}/Memory/{id}` |
| `xyz.openbmc_project.Inventory.Item.PCIeDevice` | `/redfish/v1/Systems/{system}/PCIeDevices/{id}` |

Missing, multiple, or unsupported association targets produce an internal
error. Duplicate D-Bus leaf IDs or multiple providers for one member ID are
also rejected because they would create ambiguous Redfish resources.

### Example Member

```json
{
  "@odata.id": "/redfish/v1/ComponentIntegrity/spdm0",
  "@odata.type": "#ComponentIntegrity.v1_4_0.ComponentIntegrity",
  "Id": "spdm0",
  "Name": "Component Integrity spdm0",
  "ComponentIntegrityEnabled": true,
  "ComponentIntegrityType": "SPDM",
  "ComponentIntegrityTypeVersion": "1.2",
  "LastUpdated": "2026-09-15T12:00:00.000+00:00",
  "SPDM": {
    "Requester": {
      "@odata.id": "/redfish/v1/Managers/bmc"
    }
  },
  "TargetComponentURI": "/redfish/v1/Systems/system/Processors/cpu0"
}
```

## Composite EAT Bundle D-Bus Contract

The platform provides one producer at the fixed service, object, and interface:

```text
Service:   xyz.openbmc_project.SPDM
Object:    /xyz/openbmc_project/SPDM/CompositeEATBundle
Interface: xyz.openbmc_project.SPDM.CompositeEATBundle
```

The interface exposes:

| Member | Type | Requirement |
|---|---|---|
| `Generate` | method `ay -> void` | Accept one 32-byte nonce and start generation |
| `Status` | property `s` | `Idle`, `InProgress`, `Ready`, or `Error` |
| `Bundle` | property `ay` | Complete Composite EAT Bundle bytes when `Status` is `Ready` |

The producer must clear stale bundle data before reporting `InProgress` or
`Error`, and it must publish the complete bundle before reporting `Ready`.
bmcweb treats the returned bundle as opaque bytes.

The ComponentIntegrity collection advertises the OEM action and result resource
only when the fixed producer is present.

The advertised action uses the CSDL namespace and action name consistently:

```json
{
  "#OpenBMCCompositeEATBundle.Generate": {
    "target": "/redfish/v1/ComponentIntegrity/Actions/Oem/OpenBMCCompositeEATBundle.Generate"
  }
}
```

## Generate A Composite EAT Bundle

The action starts asynchronous platform collection and bundle generation. It
accepts one canonical, standard-base64 encoded 32-byte nonce:

```http
POST /redfish/v1/ComponentIntegrity/Actions/Oem/OpenBMCCompositeEATBundle.Generate
Content-Type: application/json

{
  "Nonce": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
}
```

The encoded nonce must contain exactly 44 characters, use the standard base64
alphabet, end in one `=`, and decode to exactly 32 bytes. URL-safe or unpadded
forms are rejected.

On success, bmcweb returns:

```http
HTTP/1.1 202 Accepted
Location: /redfish/v1/ComponentIntegrity/CompositeEATBundle
```

If generation is already active, bmcweb returns `503 Service Unavailable` with
`Retry-After: 5`.

## Read The Result

Clients poll:

```text
GET /redfish/v1/ComponentIntegrity/CompositeEATBundle
```

While generation is active, the result contains no bundle:

```json
{
  "@odata.id": "/redfish/v1/ComponentIntegrity/CompositeEATBundle",
  "@odata.type": "#OpenBMCCompositeEATBundle.v1_0_0.CompositeEATBundle",
  "Id": "CompositeEATBundle",
  "Name": "Platform Composite EAT Bundle",
  "Status": "InProgress"
}
```

When generation completes, `CompositeEATBundle` contains the standard-base64
encoding of the producer's complete byte array:

```json
{
  "@odata.id": "/redfish/v1/ComponentIntegrity/CompositeEATBundle",
  "@odata.type": "#OpenBMCCompositeEATBundle.v1_0_0.CompositeEATBundle",
  "Id": "CompositeEATBundle",
  "Name": "Platform Composite EAT Bundle",
  "Status": "Ready",
  "CompositeEATBundle": "2QJY..."
}
```

A producer-reported `Error`, missing required property, unknown status, or empty
bundle in the `Ready` state produces an internal error. A missing producer
result resource returns `404 Not Found`.

## Security Boundary

bmcweb validates the request envelope and translates between Redfish and D-Bus.
It does not:

- select devices or measurements;
- transport SPDM messages;
- construct the Composite EAT Bundle;
- verify signatures or certificate chains;
- appraise EAT claims; or
- define platform trust or endorsement policy.

Those responsibilities belong to the producer and the external verifier. The
version 1 action intentionally has no device selector or measurement selector.

## Validation

Implementations should verify at least:

- feature-off and feature-on builds;
- collection and member discovery;
- duplicate and ambiguous provider rejection;
- target association mapping;
- producer-present and producer-absent collection responses;
- canonical nonce acceptance and malformed nonce rejection;
- `202` and result `Location` behavior;
- concurrent generation handling;
- all status transitions and stale bundle removal;
- exact equality between decoded Redfish bytes and D-Bus `Bundle` bytes; and
- OEM CSDL and JSON schema consistency.

See [OPENBMC_COMPOSITE_EAT_ADOPTER_GUIDE.md](OPENBMC_COMPOSITE_EAT_ADOPTER_GUIDE.md)
for deployment validation and interoperability-report guidance.