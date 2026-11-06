"""Tools/mfport rebake: waiting out a lost network instead of failing every bake."""
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib import rebake  # noqa: E402


class NetworkWaitTests(unittest.TestCase):
    def test_online_returns_at_once(self):
        with mock.patch.object(rebake, '_online', return_value=True), mock.patch.object(rebake.time, 'sleep') as sleep:
            rebake._wait_for_network()
        sleep.assert_not_called()

    def test_offline_waits_until_back(self):
        states = iter([False, False, True])
        with mock.patch.object(rebake, '_online', side_effect=lambda: next(states)), mock.patch.object(rebake.time, 'sleep') as sleep:
            rebake._wait_for_network()
        self.assertEqual(sleep.call_count, 2)  # one poll while down, one to let the interface settle


if __name__ == '__main__':
    unittest.main()
