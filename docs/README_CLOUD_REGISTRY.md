# Cloud Registry Integration

This module exposes the Cloud Connectivity registers to the
registry framework so they can be read and written over the
same channels as Audio, RGB, Curtain and Outputs.

## Registers

| Address | Name        | Type   | Access | Notes                        |
|---------|-------------|--------|--------|------------------------------|
| 0xC800  | ServerFQDN  | string | R/W    | Persisted in NVS             |
| 0xC801  | ServerIP    | string | R/W    | Updates `apiBaseUrl` on write|
| 0xC200  | ServerPort  | u16    | R/W    | Updates `apiBaseUrl` on write|
| 0x4800  | DeviceID    | string | R      | Synced from CloudManager     |
| 0xC802  | Username    | string | R/W    | Persisted in NVS             |
| 0xC803  | Password    | string | R/W    | Read returns `****` (masked) |

## Architecture

- `CloudRegistryStore` holds the values and persists them in
  the `cloud-reg` NVS namespace. It survives reboots.
- `CloudRegistryController` is the registry-facing layer. It
  binds to `cloud_object` (the process image) through the
  bindings created in `ecosmart_registery_init()`, and
  forwards changes to `CloudManager` for connection-impacting
  fields.
- `CloudManager` receives the new values through existing
  setters (`setApiBaseUrl`).

## Security

- The password is never returned in clear text over the WS
  channel. The read path always returns `****`.
- NVS is encrypted on devices with flash encryption enabled.
  Without flash encryption, NVS is stored in plain text.
- The WS channel used by the frontend is plaintext and
  token-authenticated. The backend channel (`/ws`) is
  AES-GCM encrypted.

## Behavior on Write

| Register    | Effect                                         |
|-------------|------------------------------------------------|
| ServerFQDN  | Persisted only. Informational.                 |
| ServerIP    | Persisted, then `CloudManager::setApiBaseUrl`  |
| ServerPort  | Persisted, then `CloudManager::setApiBaseUrl`  |
| Username    | Persisted. Used on next login.                 |
| Password    | Persisted. Used on next login.                 |
| DeviceID    | Read-only from the registry layer.             |

## Usage from the WS channel

To set the backend IP to `192.168.88.93`:

    WRITE 0xC801 = "192.168.88.93"

To set the port to `3000`:

    WRITE 0xC200 = "3000"

To read the current IP:

    READ 0xC801  ->  "192.168.88.93"

To read the masked password:

    READ 0xC803  ->  "****"

## Migration from hardcoded values

On first boot the NVS namespace is empty, so the store falls
back to defaults (empty strings, port 3000). `AppController`
uses `OWNER_USERNAME` and `OWNER_PASSWORD` as fallback values
for the initial login. Once the user writes new values through
the WS channel, they are persisted and used on subsequent
boots.

If `CloudRegistryStore::isConfigured()` returns true (i.e.
ServerIP is set), `AppController` calls
`CloudManager::setApiBaseUrl()` with the persisted URL before
the first login attempt.
