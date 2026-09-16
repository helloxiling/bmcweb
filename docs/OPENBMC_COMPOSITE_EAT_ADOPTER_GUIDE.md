# OpenBMC Composite EAT Adopter Guide

## Status

This is an OpenBMC reference implementation for cross-vendor integration. The
Composite EAT Bundle API is currently an OpenBMC OEM extension and will be
proposed for Redfish standardization.

The public bmcweb implementation is on branch
`xsun/bmcweb-composite-eat-reference` in
`https://github.com/helloxiling/bmcweb`.

The corresponding spdmd implementation is under review in
`https://github.com/NVIDIA/spdm/pull/2`. That pull request implements the
external D-Bus contract described below.

## Scope

Composite EAT is platform-scoped. It is separate from the standard per-device
ComponentIntegrity member APIs and never takes a ComponentIntegrity member ID.

The canonical Redfish routes are:

```text
POST /redfish/v1/ComponentIntegrity/Actions/Oem/
  OpenBMCCompositeEATBundle.Generate

GET  /redfish/v1/ComponentIntegrity/CompositeEATBundle
```

The trigger body contains only a base64-encoded 32-byte nonce:

```json
{
  "Nonce": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
}
```

There is no device selector or measurement-index selector in version 1.

## D-Bus Producer Contract

Each platform provides one producer:

```text
Service:   xyz.openbmc_project.SPDM
Object:    /xyz/openbmc_project/SPDM/CompositeEATBundle
Interface: xyz.openbmc_project.SPDM.CompositeEATBundle
```

The interface exposes:

| Member | Type | Requirement |
|---|---|---|
| `Generate` | method `ay -> void` | Accept exactly one 32-byte nonce and start platform collection |
| `Status` | property `s` | `Idle`, `InProgress`, `Ready`, or `Error` |
| `Bundle` | property `ay` | Complete tag-602 CBOR Detached EAT Bundle when `Status=Ready` |

The producer must:

1. Reject a nonce whose decoded length is not 32 bytes.
2. Reject concurrent generation with `EBUSY`.
3. Clear the previous bundle before publishing `InProgress`.
4. Collect the configured platform attestation scope.
5. Publish complete `Bundle` bytes before publishing `Ready`.
6. Clear `Bundle` before publishing `Error`.
7. Isolate concurrent collection state by responder identity.

bmcweb treats `Bundle` as opaque bytes. Signature verification, appraisal, and
platform policy remain outside bmcweb.

## bmcweb Integration

Enable both Meson features:

```text
-Dredfish-component-integrity=enabled
-Dredfish-composite-eat=enabled
```

`redfish-composite-eat` depends on `redfish-component-integrity`. Both features
are disabled by default.

When the D-Bus producer is available, the ComponentIntegrity collection
advertises this OEM object:

```json
{
  "Oem": {
    "OpenBMC": {
      "@odata.type": "#OpenBMCCompositeEATBundle.v1_0_0.ComponentIntegrityCollection",
      "CompositeEATBundle": {
        "@odata.id": "/redfish/v1/ComponentIntegrity/CompositeEATBundle"
      },
      "Actions": {
        "#OpenBMCCompositeEATBundle.Generate": {
          "target": "/redfish/v1/ComponentIntegrity/Actions/Oem/OpenBMCCompositeEATBundle.Generate"
        }
      }
    }
  }
}
```

The action returns `202 Accepted` with a `Location` header naming the result
resource. A concurrent request returns `503 Service Unavailable` with
`Retry-After`. Clients poll the result until `Ready` or an error response.

## Required Validation

Run this matrix on each platform and record exact source, firmware, executable,
and evidence hashes.

| Test | Expected result |
|---|---|
| Feature disabled | Composite routes and schemas are absent |
| Producer absent | Collection does not advertise the OEM action |
| Unauthorized POST | `401` |
| Missing nonce | `400` |
| Malformed or wrong-length nonce | `400` |
| Extra version-1 parameter | `400` |
| Valid nonce | `202` and result `Location` |
| Concurrent request | `503` and `Retry-After` |
| Poll while running | `200`, `Status=InProgress`, no bundle |
| Poll when complete | `200`, `Status=Ready`, non-empty bundle |
| Producer error | Redfish error and no stale bundle |
| Metadata and OEM schemas | Available and internally consistent |
| Redfish versus D-Bus | Decoded Redfish bundle equals D-Bus bytes exactly |
| Repeated nonce requests | Each result binds the supplied nonce |
| Service restart | No stale or partially published bundle is returned |

For each returned artifact, verify:

1. The CBOR stream is complete and has tag 602.
2. The signed EAT uses the expected COSE algorithm and a trusted certificate
   chain.
3. The signed nonce equals the verifier nonce.
4. The profile and UEID claims are present and acceptable to verifier policy.
5. Signed `submods` labels match the detached Claims-Set labels.
6. Every signed detached digest matches its corresponding Claims-Set bytes.

## Interoperability Report

An adopter report should include:

- bmcweb repository and commit;
- spdmd or producer repository and commit;
- BMC firmware and platform identifiers suitable for public disclosure;
- enabled feature flags;
- D-Bus producer service, object, and interface;
- Redfish status matrix results;
- generated bundle size and SHA-256;
- signature and detached-digest verification results; and
- producer service-health results before and after generation.

Do not add platform device paths, endpoint IDs, signer internals, or collection
policy to public bmcweb. Those belong below the D-Bus producer boundary.

## Upstreaming

Treat bmcweb and producer reviews as separate changes:

1. Review and stabilize the platform-neutral D-Bus producer contract.
2. Validate at least two independent platform implementations.
3. Keep the OpenBMC OEM schema and route stable during interoperability work.
4. Submit DMTF standardization separately; the validator can check schema and
   Redfish correctness but cannot standardize an OEM API.