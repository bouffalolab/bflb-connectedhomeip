import pathlib
import unittest

HEADER = pathlib.Path(__file__).resolve().parents[2] / 'common' / 'ThreadUdpBandwidthService.h'


class ThreadUdpBandwidthHeaderTests(unittest.TestCase):
    def test_uses_global_chip_error_type(self):
        text = HEADER.read_text()
        self.assertNotIn('chip::CHIP_ERROR', text)
        self.assertIn('CHIP_ERROR Init();', text)
        self.assertIn('HandleUdpReceiveError(chip::Inet::UDPEndPoint * endPoint, CHIP_ERROR err,', text)


if __name__ == '__main__':
    unittest.main()
