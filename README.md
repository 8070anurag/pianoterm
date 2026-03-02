# pianoterm

Run shell commands from your piano

## Description

Linux CLI tool to assign commands to run when a key is pressed on a USB MIDI Keyboard

## Usage

```bash
pianoterm <port>
```

Notes:
Assumes ALSA is used as the soundcard driver
Use acconnect -i to find the desired midi port

## Configuration

- $HOME/.config/pianoterm/config
```conf
# this is a comment
#
# trigger can be on_release or on_press
on_press

# syntax: port = command
# use aseqdump -p <port> to find specific keycodes

21 = playerctl previous # first key on an 88-key keyboard
22 = playerctl play-pause
23 = playerctl next
#...
108 = /home/me/my_script.sh
```

## Building
```bash
git clone ""
cd "" && make
```

### Dependencies
- C compiler
- alsactl (1.2.15.2)
- make (optional)

