import array
import io
import struct
import tempfile
import unittest
import wave
from pathlib import Path
from speech_audio import normalize_asr
from speech_session import wav_data_offset, spoken_text
from six_mic_control import packet, frames


class AudioTests(unittest.TestCase):
    def test_extended_wav_header(self):
        data=b'RIFF'+struct.pack('<I',1000)+b'WAVE'
        data+=b'fmt '+struct.pack('<I',16)+bytes(16)
        data+=b'JUNK'+struct.pack('<I',3)+b'abc\0'
        data+=b'data'+struct.pack('<I',100)
        self.assertEqual(wav_data_offset(io.BytesIO(data)),len(data))
        self.assertIsNone(wav_data_offset(io.BytesIO(data[:-2])))

    def test_official_wake_protocol(self):
        message=packet(5,b'{"type":"manual_wakeup","content":{"beam":0}}',17)
        self.assertEqual(message[:3],b'\xa5\x01\x05')
        self.assertEqual(struct.unpack_from('<H',message,3)[0],len(message)-8)
        self.assertEqual(sum(message)&255,0)

    def test_fragmented_uart_handshake(self):
        message=packet(1,b'\xa5\0\0\0',0xC1DA)
        found,tail=frames(b'boot\n'+message[:8])
        self.assertEqual(found,[])
        found,tail=frames(tail+message[8:])
        self.assertEqual(found,[(1,0xC1DA,b'\xa5\0\0\0')])
        self.assertEqual(tail,b'')

    def test_quiet_asr_copy_amplified_without_clipping(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'quiet.wav'
            samples=array.array('h',[150,-150]*16000)
            with wave.open(str(path),'wb') as w:
                w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(samples.tobytes())
            levels=normalize_asr(path)
            with wave.open(str(path)) as w: actual=array.array('h',w.readframes(w.getnframes()))
            self.assertEqual(levels['asr_gain'],6)
            self.assertEqual(max(actual),900)
            self.assertEqual(len(actual),len(samples))

    def test_english_is_not_silently_dropped(self):
        text=spoken_text('USB RK3588 Qwen 温度正常')
        self.assertNotIn('USB',text)
        self.assertIn('温度正常',text)


if __name__=='__main__': unittest.main()
