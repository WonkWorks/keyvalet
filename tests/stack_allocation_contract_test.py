#!/usr/bin/env python3
"""Focused ownership/order checks for bounded request and replay allocations."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def main() -> None:
    security = (ROOT / "main" / "security.c").read_text()
    load_state = section(
        security,
        "static esp_err_t load_state",
        "static esp_err_t confirm_running_application",
    )
    assert "return keyvalet_replay_store_init(s_enrolled, &s_replay)" in load_state

    authorize = section(
        security,
        "esp_err_t keyvalet_security_authorize_and_consume",
        "\n}",
    )
    assert "keyvalet_replay_window_t updated" not in authorize
    allocation = authorize.index("keyvalet_replay_window_t *updated = malloc(sizeof(*updated))")
    assert authorize.index("verify_signature", 0, allocation) >= 0
    assert authorize.index("updated == NULL", allocation) < authorize.index("*updated = s_replay", allocation)
    assert "result = ESP_ERR_NO_MEM" in authorize
    copy = authorize.index("*updated = s_replay", allocation)
    add = authorize.index("keyvalet_replay_window_add", copy)
    persist = authorize.index("keyvalet_replay_store_append", add)
    activate = authorize.index("s_replay = *updated", persist)
    release = authorize.index("free(updated)", activate)
    assert copy < add < persist < activate < release
    assert authorize.index("xSemaphoreGive(s_lock)", release) > release

    web = (ROOT / "main" / "web_server.c").read_text()
    websocket = section(web, "static esp_err_t websocket_handler", "static void session_closed")
    assert "uint8_t payload[" not in websocket
    limit = websocket.index("frame.len > REQUEST_BUFFER_SIZE - 1U")
    allocation = websocket.index("malloc(frame.len + 1U)")
    assert limit < allocation
    assert "if (payload == NULL)" in websocket
    assert "httpd_ws_recv_frame(request, &frame, frame.len)" in websocket
    assert websocket.count("free(payload)") == 6  # bounded control-frame reply too

    patch = section(web, "static esp_err_t config_patch_handler", "static esp_err_t config_options_handler")
    assert "char body[" not in patch
    limit = patch.index("request->content_len >= REQUEST_BUFFER_SIZE")
    allocation = patch.index("malloc((size_t)request->content_len + 1U)")
    assert limit < allocation
    assert "if (body == NULL)" in patch
    assert 'HTTPD_500_INTERNAL_SERVER_ERROR' in patch
    assert "body[received] = '\\0'" in patch
    assert patch.count("free(body)") == 3
    print("stack allocation ownership contract tests passed")


if __name__ == "__main__":
    main()
