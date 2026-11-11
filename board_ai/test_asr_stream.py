import unittest
from asr_stream import AsrTranscriptStream


class AsrStreamTests(unittest.TestCase):
    def test_utf8_split_at_every_byte_and_metrics_not_exposed(self):
        data='model log\nlanguage res: Chinese\ntext res:\n设备温度正常。\n\n----Finished----\nLLM part performance:\nGenerate 123'.encode()
        parser=AsrTranscriptStream()
        updates=[]
        for b in data:
            text=parser.feed(bytes([b]))
            if text is not None: updates.append(text)
        parser.feed(b'',final=True)
        self.assertEqual(parser.text,'设备温度正常。')
        self.assertGreater(len(updates),2)
        self.assertFalse(any('\ufffd' in text or 'Generate' in text for text in updates))

    def test_online_revisions_replace_uncommitted_text(self):
        parser=AsrTranscriptStream(online=True)
        self.assertEqual(parser.feed(b'\nFIELDOPS_ASR_PARTIAL\n'+ '设备温度正'.encode()+b'\nFIELDOPS_ASR_END\n'),'设备温度正')
        self.assertEqual(parser.feed(b'\nFIELDOPS_ASR_PARTIAL\n'+ '设备温度正常。'.encode()+b'\nFIELDOPS_ASR_END\n'),'设备温度正常。')
        self.assertEqual(parser.feed(b'\nFIELDOPS_ASR_PARTIAL\n\nFIELDOPS_ASR_END\n'),'')

    def test_online_partial_marker_does_not_leak(self):
        parser=AsrTranscriptStream(online=True)
        self.assertIsNone(parser.feed(b'log\nFIELDOPS_ASR_PARTIAL\n'+ '测试'.encode()))
        self.assertEqual(parser.feed(b'\nFIELDOPS_ASR_END\n'),'测试')


if __name__=='__main__': unittest.main()
