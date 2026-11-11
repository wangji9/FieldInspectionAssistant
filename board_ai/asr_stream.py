"""Decode actual native ASR output incrementally, including split UTF-8 bytes."""
import codecs
import re


class AsrTranscriptStream:
    def __init__(self, online=False):
        self.online = online
        self.decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
        self.output = ''
        self.text = ''

    def feed(self, data, final=False):
        self.output += self.decoder.decode(data, final=final)
        if self.online:
            matches = re.findall(r'\nFIELDOPS_ASR_PARTIAL\n(.*?)\nFIELDOPS_ASR_END\n', self.output, re.S)
            candidate = matches[-1] if matches else self.text
        else:
            marker = re.search(r'text res:\s*\n', self.output)
            if not marker:
                return None
            candidate = re.split(r'\n\n|\n-{5,}|LLM part performance', self.output[marker.end():], 1)[0]
        candidate = candidate.strip()
        if candidate != self.text:
            self.text = candidate
            return candidate
        return None
