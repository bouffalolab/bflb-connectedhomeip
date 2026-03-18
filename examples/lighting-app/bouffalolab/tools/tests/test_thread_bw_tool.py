import importlib.util
import pathlib
import sys
import unittest

MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "thread_bw_tool.py"
SPEC = importlib.util.spec_from_file_location("thread_bw_tool", MODULE_PATH)
thread_bw_tool = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
sys.modules[SPEC.name] = thread_bw_tool
SPEC.loader.exec_module(thread_bw_tool)


class ControlProtocolTests(unittest.TestCase):
    def test_format_reset_command(self):
        self.assertEqual(thread_bw_tool.format_control_command("RESET"), b"BWT1 RESET\n")

    def test_format_start_tx_command(self):
        msg = thread_bw_tool.format_control_command(
            "START_TX",
            payload=128,
            duration_ms=2000,
            dest_port=40000,
            chunk_count=8,
        )
        self.assertEqual(msg, b"BWT1 START_TX payload=128 duration_ms=2000 dest_port=40000 chunk_count=8\n")

    def test_parse_stats_response(self):
        parsed = thread_bw_tool.parse_control_response(
            b"BWT1 STATS bytes_rx=10 pkts_rx=2 bytes_tx=30 pkts_tx=4 seq_gap_count=1 active_tx=0 last_error=0\n"
        )
        self.assertEqual(parsed["verb"], "STATS")
        self.assertEqual(parsed["fields"]["bytes_rx"], 10)
        self.assertEqual(parsed["fields"]["seq_gap_count"], 1)
        self.assertEqual(parsed["fields"]["active_tx"], 0)

    def test_parse_invalid_prefix_raises(self):
        with self.assertRaises(ValueError):
            thread_bw_tool.parse_control_response(b"NOPE STATS bytes_rx=1\n")


class MetricsTests(unittest.TestCase):
    def test_compute_kbps(self):
        self.assertAlmostEqual(thread_bw_tool.compute_throughput_kbps(125000, 10.0), 100.0)

    def test_compute_loss_pct(self):
        self.assertAlmostEqual(thread_bw_tool.compute_packet_loss_pct(100, 97), 3.0)
        self.assertIsNone(thread_bw_tool.compute_packet_loss_pct(0, 0))


if __name__ == "__main__":
    unittest.main()
