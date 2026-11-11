#!/usr/bin/env bash
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
libexec=/usr/local/libexec/yahboom-audio

if [[ "$(id -un)" != ztl ]]; then
  printf '%s\n' 'Run as ztl on the target board.' >&2
  exit 1
fi

sudo install -d "$libexec"
sudo install -m 0644 "$project/services/audio/ch340_init.py" "$project/services/audio/defaults.py" "$libexec/"
sudo install -m 0644 "$project/deploy/udev/xf-speech.rules" /etc/udev/rules.d/xf-speech.rules
sudo install -m 0644 "$project/deploy/systemd/yahboom-ch340@.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo udevadm control --reload-rules

install -D -m 0644 "$project/deploy/systemd/yahboom-audio-defaults.service" \
  "$HOME/.config/systemd/user/yahboom-audio-defaults.service"
systemctl --user daemon-reload
systemctl --user enable yahboom-audio-defaults.service

printf '%s\n' 'Reconnect the six-microphone USB cables to apply the udev rules, then start yahboom-audio-defaults.service in the desktop session.'
