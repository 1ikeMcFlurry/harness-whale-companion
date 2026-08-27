import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "harness" / "bridge"))

from harness_ble_protocol import (  # noqa: E402
    Balance,
    Currency,
    FLAG_BALANCE_AVAILABLE,
    FLAG_HAS_BALANCE,
    FLAG_NEW_TURN,
    MESSAGE_TYPE,
    Snapshot,
    State,
    Tool,
    make_frame,
    make_question_frame,
    normalize_tool,
    pack_payload,
    snapshot_from_mapping,
)
from harness_ble_bridge import StatusModel, parse_balance_document  # noqa: E402


class HarnessBleProtocolTests(unittest.TestCase):
    def test_canonical_payload_matches_firmware_contract(self):
        payload = pack_payload(Snapshot(
            state=State.TOOL,
            tool=Tool.EDIT,
            seq=0x12345678,
            elapsed_s=42,
            todo_done=3,
            todo_total=7,
            balance=Balance(500, Currency.CNY),
            new_turn=True,
        ))
        self.assertEqual(22, len(payload))
        self.assertEqual(
            (2, State.TOOL, Tool.EDIT,
             FLAG_HAS_BALANCE | FLAG_BALANCE_AVAILABLE | FLAG_NEW_TURN,
             0x12345678, 42, 3, 7, 500, Currency.CNY),
            struct.unpack("<BBBBIIHHIB", payload[:21]),
        )
        self.assertEqual(0, payload[21])

    def test_frame_header(self):
        frame = make_frame(Snapshot(state=State.IDLE))
        self.assertEqual(bytes((1, MESSAGE_TYPE, 22, 0)), frame[:4])
        self.assertEqual(26, len(frame))

    def test_utf8_title_is_carried_and_truncated_on_codepoint_boundary(self):
        payload = pack_payload(Snapshot(state=State.THINKING, title="中文任务" * 30))
        self.assertLessEqual(payload[21], 72)
        self.assertEqual(payload[21], len(payload) - 22)
        payload[22:].decode("utf-8")

    def test_mapping_is_bounded_and_hides_tool_outside_tool_state(self):
        snapshot = snapshot_from_mapping({
            "state": "thinking",
            "tool": "exec_command",
            "todoDone": 99,
            "todoTotal": 4,
            "elapsed": -1,
        }, seq=1)
        self.assertEqual(State.THINKING, snapshot.state)
        self.assertEqual(Tool.NONE, snapshot.tool)
        self.assertEqual((4, 4), (snapshot.todo_done, snapshot.todo_total))
        self.assertEqual(0, snapshot.elapsed_s)

    def test_tool_normalization(self):
        self.assertEqual(Tool.TERMINAL, normalize_tool("exec_command"))
        self.assertEqual(Tool.READ, normalize_tool("ReadFile"))
        self.assertEqual(Tool.EDIT, normalize_tool("apply_patch"))
        self.assertEqual(Tool.SEARCH, normalize_tool("grep_search"))
        self.assertEqual(Tool.WEB, normalize_tool("browser.open"))
        self.assertEqual(Tool.OTHER, normalize_tool("mystery"))

    def test_balance_api_response_is_reduced_to_amount_and_currency(self):
        balance = parse_balance_document({
            "is_available": True,
            "balance_infos": [
                {"currency": "USD", "total_balance": "1.20"},
                {"currency": "CNY", "total_balance": "12.345"},
            ],
        })
        self.assertEqual(Balance(1235, Currency.CNY, True), balance)

    def test_question_frame_sanitizes_unsupported_glyphs(self):
        frame = make_question_frame(["继续✅", "稍后"])
        self.assertEqual((1, 8), tuple(frame[:2]))
        payload = frame[4:]
        self.assertEqual((1, 2), tuple(payload[:2]))
        self.assertIn(b"?", payload)

    def test_new_states_keep_wire_values_stable(self):
        self.assertEqual(7, State.STOPPED)
        self.assertEqual(8, State.QUESTION)

    def test_bridge_routes_a_hardware_answer_to_the_requesting_plugin(self):
        class FakeTransport:
            def __init__(self):
                self.sent = []

            def sendto(self, data, addr):
                self.sent.append((data, addr))

        model = StatusModel()
        transport = FakeTransport()
        model.datagram_transport = transport
        addr = ("127.0.0.1", 54321)
        model.set_question({
            "requestId": "request-1",
            "options": ["继续", "暂停"],
        }, addr)
        self.assertEqual(8, model.question_frame()[1])
        model.answer_question(1)
        self.assertIsNone(model.question)
        self.assertEqual(addr, transport.sent[0][1])
        self.assertIn(b'"selected":1', transport.sent[0][0])

    def test_bridge_pages_long_questions_and_maps_back_to_original_index(self):
        class FakeTransport:
            def __init__(self):
                self.sent = []

            def sendto(self, data, addr):
                self.sent.append((data, addr))

        model = StatusModel()
        transport = FakeTransport()
        model.datagram_transport = transport
        addr = ("127.0.0.1", 54321)
        model.set_question({
            "requestId": "request-many",
            "options": ["A", "B", "C", "D", "E", "F"],
        }, addr)

        visible, start, paging_index = model.question_page_view()
        self.assertEqual((["A", "B", "C", "下一页"], 0, 3),
                         (visible, start, paging_index))
        first_revision = model.question_revision
        model.answer_question(3)
        self.assertIsNotNone(model.question)
        self.assertEqual(first_revision + 1, model.question_revision)
        self.assertEqual((["D", "E", "F", "回到开头"], 3, 3),
                         model.question_page_view())

        model.answer_question(1)
        self.assertIsNone(model.question)
        self.assertEqual(addr, transport.sent[0][1])
        self.assertIn(b'"selected":4', transport.sent[0][0])

    def test_bridge_balance_refresh_request_is_coalesced(self):
        model = StatusModel()
        self.assertFalse(model.balance_refresh.is_set())
        model.request_balance_refresh()
        model.request_balance_refresh()
        self.assertTrue(model.balance_refresh.is_set())


if __name__ == "__main__":
    unittest.main()
