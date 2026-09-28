# Security notes

- **One identity per device.** Issue each HUD its own client certificate
  (HUD-001, HUD-002, …) from the TAK Server CA, and never an admin
  certificate. Revoke it on the server if a HUD is lost.
- **No secrets in git.** Certificates, keys and Wi-Fi passwords go in the
  `hudcfg` NVS partition through `tools/provision.py` or the console.
  `.gitignore` excludes `provisioning/`, `*.p12`, `*.pem` and `*.key`.
- **TLS checks.** The TAK client verifies the server against the
  provisioned CA. `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` makes mbedTLS also check
  certificate dates, which needs wall-clock time: SNTP (DHCP-provided
  server first) or the GNSS time on the backpack. If the server
  certificate's CN isn't the hostname you connect to, set it with
  `tak <host> 8089 tls <server_cn>` instead of disabling verification.
- **Protect the key on the device.** Enable **flash encryption** and
  **secure boot v2** for anything that leaves the bench, and switch the
  `hudcfg` partition to encrypted NVS (add the `nvs_keys` partition, as
  noted in `partitions.csv`). Without it, anyone with the device and a USB
  cable can read the client key.
- **Local interfaces.** The serial console and the HTTP live-view API are
  unauthenticated by design, for the bench. For field use, disable the HTTP
  API (don't call `telemetry_http_start()`) or add a token, and consider a
  console lock.
- **What it sends.** By default the HUD sends only `t-x-c-t` pings. It
  publishes its own SA only when `send_sa` is set and it has its own GNSS
  fix, so it doesn't duplicate the phone's position.
