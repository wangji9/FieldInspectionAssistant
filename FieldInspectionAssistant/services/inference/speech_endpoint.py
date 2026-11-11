"""A pause detector, not an ASR eligibility gate. Samples are signed 16-bit PCM."""
import math


class SpeechEndpoint:
    def __init__(self):
        self.last_voice = None
        self.voiced_seconds = 0
        self.noise = 8.0

    def update(self, samples, elapsed, duration):
        if samples:
            rms = math.sqrt(sum(x*x for x in samples)/len(samples))
            # Estimate the quiet floor, without learning a speaking voice as noise.
            threshold = max(18.0, min(100.0, self.noise * 2.6))
            if rms >= threshold and max(map(abs, samples)) >= 64:
                self.voiced_seconds += duration
                self.last_voice = elapsed
            elif rms < threshold:
                self.noise = self.noise * 0.95 + rms * 0.05
        if self.last_voice is not None and self.voiced_seconds >= 0.25:
            return elapsed >= 1 and elapsed-self.last_voice >= 1.8
        return elapsed >= 12
