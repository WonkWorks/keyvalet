# Scoped ESP-IDF HTTPS component correction

Source, header, Kconfig and CMakeLists are copied from the installed Espressif
ESP-IDF **v5.5.5**, component `esp_https_server`, retaining its SPDX headers.
The sole functional change is in `httpd_ssl_open`: when transport-context
allocation fails after a successful handshake, delete the still-owned TLS
session before returning `ESP_ERR_NO_MEM`. Upstream otherwise leaks that session.

This project component overrides only the corresponding IDF component. It does
not replace ESP-TLS, HTTPD, mbedTLS, TLS policy, or the SDK installation. The
copied API/config remain unchanged. `tests/tls_transport_test.py` executes the
actual patched function with success/handshake/allocation-failure stubs. Actual
ESP32 low-memory/connection-cycle acceptance remains required. On an IDF upgrade,
compare the component and remove this override once the upstream fix is present.
