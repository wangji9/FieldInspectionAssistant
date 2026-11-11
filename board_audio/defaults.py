#!/usr/bin/env python3
"""Select the USB speaker and ListenGo UAC source for the desktop session."""
import json
import subprocess
import time


def pactl(*args):
    return subprocess.check_output(['pactl', *args], text=True)


def main():
    for _ in range(30):
        cards = json.loads(pactl('-f', 'json', 'list', 'cards'))
        mic = next((c for c in cards if
                    c.get('properties', {}).get('device.vendor.id') == '2208' and
                    c.get('properties', {}).get('device.product.id') == '0001'), None)
        speaker = next((c for c in cards if
                        c.get('properties', {}).get('device.vendor.id') == '0d8c' and
                        c.get('properties', {}).get('device.product.id') == '0012'), None)
        if speaker and speaker.get('active_profile') != 'output:analog-stereo':
            pactl('set-card-profile', speaker['name'], 'output:analog-stereo')
            time.sleep(0.3)
        sinks = json.loads(pactl('-f', 'json', 'list', 'sinks'))
        sink = next((s for s in sinks if
                     'usb-C-Media_Electronics_Inc._USB_Audio_Device-' in s['name']), None)
        if speaker and not sink:
            # A USB reset/replug can leave a stale active profile with no sink.
            pactl('set-card-profile', speaker['name'], 'off')
            pactl('set-card-profile', speaker['name'], 'output:analog-stereo')
            time.sleep(0.3)
            sinks = json.loads(pactl('-f', 'json', 'list', 'sinks'))
            sink = next((s for s in sinks if
                         'usb-C-Media_Electronics_Inc._USB_Audio_Device-' in s['name']), None)
        if mic and sink:
            sources = json.loads(pactl('-f', 'json', 'list', 'sources'))
            source = next((s for s in sources if
                           'alsa_input.usb-EII_ListenGo_Circular_6-Microphone_' in s['name']), None)
            if not source:
                pactl('set-card-profile', mic['name'], 'off')
                pactl('set-card-profile', mic['name'], 'input:mono-fallback')
                time.sleep(0.3)
                sources = json.loads(pactl('-f', 'json', 'list', 'sources'))
                source = next((s for s in sources if
                               'alsa_input.usb-EII_ListenGo_Circular_6-Microphone_' in s['name']), None)
            if source:
                pactl('set-default-source', source['name'])
                pactl('set-source-mute', source['name'], '0')
                pactl('set-source-volume', source['name'], '100%')
                pactl('set-default-sink', sink['name'])
                pactl('set-sink-mute', sink['name'], '0')
                pactl('set-sink-volume', sink['name'], '30%')
                print('Input:', source['name'])
                print('Output:', sink['name'])
                return
        time.sleep(1)
    raise RuntimeError('USB microphone/speaker unavailable after 30 seconds')


if __name__ == '__main__':
    main()
