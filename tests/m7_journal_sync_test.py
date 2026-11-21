#!/usr/bin/env python3
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
web = (ROOT / "main/web_server.c").read_text()
security = (ROOT / "main/security.c").read_text()
journal = (ROOT / "main/journal.c").read_text()
sync = (ROOT / "main/journal_sync.c").read_text()
console = (ROOT / "tools/m5_console.html").read_text()
signer = (ROOT / "tools/m5_signing_helper.py").read_text()
protocol = (ROOT / "main/journal_sync_contract.c").read_text()
partitions = (ROOT / "partitions.csv").read_text()

assert "journal_sync" in web
assert '"journal_get"' in web and '"journal_batch"' in web
assert '"journal_ack"' in web and '"journal_ack_result"' in web
assert "KEYVALET_JOURNAL_SYNC_MAX_BATCH 8U" in (ROOT / "main/journal_sync_contract.h").read_text()
assert "KEYVALET_JOURNAL_BATCH_SCAN_LIMIT 16U" in (ROOT / "main/journal_ring.h").read_text()
assert "keyvalet_journal_read_batch" in journal
assert "KEYVALET-JOURNAL-ACK-V1" in protocol
assert "KEYVALET_AUTH_JOURNAL_ACK" in security
assert "nvs_set_blob" in sync and "nvs_commit" in sync
assert sync.index("nvs_set_blob") < sync.index("s_acknowledged_sequence = sequence")
assert "ALREADY_ACKNOWLEDGED" in web and "BEYOND_LATEST" in web
assert "unrecoverable_gap" in web and "gap_start_sequence" in web
assert "durably persisted" in console
assert "/api/sign/journal-ack" in signer
assert "KEYVALET-JOURNAL-ACK-V1" in signer
assert "journal," in partitions and "replay_nvs," in partitions
assert "journal_sync" not in partitions
unlock = web[web.index("static esp_err_t handle_targeted_unlock"):]
assert unlock.index("KEYVALET_AUTH_UNLOCK") < unlock.index("keyvalet_journal_append") < unlock.index("keyvalet_device_unlock_commit")
ack = web[web.index("static esp_err_t handle_journal_ack"):web.index("static esp_err_t websocket_pre_handshake")]
assert ack.index("keyvalet_security_authorize_and_consume") < ack.index("keyvalet_journal_sync_advance")
assert "stats.produced_sequence" in sync
assert "highest_produced_sequence" in web

# Real development-helper P-256 signature over the unchanged wire canonical
# form, including acknowledgment across a declared unavailable range.
sys.path.insert(0, str(ROOT / "tools"))
import m5_signing_helper as helper
from m5_security_test import verify
with tempfile.TemporaryDirectory() as directory:
    key = Path(directory) / "operational.pem"
    helper.run_openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key))
    original_key = helper.OPERATIONAL_KEY
    helper.OPERATIONAL_KEY = key
    try:
        signed = helper.signed_response("/api/sign/journal-ack", {
            "device_id": "keyvalet-a1b2c3", "highest_contiguous_sequence": 180,
        })
    finally:
        helper.OPERATIONAL_KEY = original_key
    canonical = (
        "KEYVALET-JOURNAL-ACK-V1\nkeyvalet-a1b2c3\nJOURNAL_ACK\n180\n"
        f"{signed['operation_id']}\n{signed['nonce']}\n"
        f"{signed['issued_at']}\n{signed['expires_at']}\n"
    )
    assert verify(key, canonical, signed["signature"])
    assert not verify(key, canonical.replace("180\n", "181\n"), signed["signature"])
    assert not verify(key, canonical.replace("keyvalet-a1b2c3", "keyvalet-wrong"), signed["signature"])
    assert not verify(key, canonical.replace("JOURNAL_ACK", "UNLOCK"), signed["signature"])
print("Milestone 7 journal synchronization contract tests passed")
