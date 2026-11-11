import unittest
from speech_endpoint import SpeechEndpoint


class EndpointTests(unittest.TestCase):
    def test_wait_for_speech_and_no_speech_timeout(self):
        endpoint=SpeechEndpoint()
        self.assertFalse(endpoint.update([0]*1600,5,0.1))
        self.assertTrue(endpoint.update([0]*1600,12,0.1))

    def test_pause_ends_after_speech(self):
        endpoint=SpeechEndpoint()
        for t in (0.1,0.2,0.3,0.4):
            self.assertFalse(endpoint.update([1000,-1000]*800,t,0.1))
        self.assertFalse(endpoint.update([0]*1600,1.5,0.1))
        self.assertTrue(endpoint.update([0]*1600,2.3,0.1))

    def test_single_click_noise_does_not_count_as_speech(self):
        endpoint=SpeechEndpoint()
        self.assertFalse(endpoint.update([1000]*1600,0.1,0.1))
        self.assertFalse(endpoint.update([0]*1600,3,0.1))

    def test_quiet_voice_reaches_endpoint(self):
        endpoint=SpeechEndpoint()
        for i in range(5):
            self.assertFalse(endpoint.update([80,-80]*800,(i+1)*0.1,0.1))
        self.assertGreaterEqual(endpoint.voiced_seconds,0.25)
        self.assertTrue(endpoint.update([0]*1600,2.5,0.1))


if __name__=='__main__':unittest.main()
