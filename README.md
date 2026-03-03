# pianoterm

Run shell commands from your piano

## Description

Linux CLI tool to assign shell commands to keys on a USB MIDI Keyboard

## Usage

```bash
pianoterm <port>
```

Note:
Assumes ALSA is used as the soundcard driver, you can use acconnect -i to find the desired midi port.

## Configuration

- $HOME/.config/pianoterm/config
```conf
# This is a comment
#
# Trigger can be on_release or on_press for each keybind (on_hold in development)
# Syntax: port = command
# Use aseqdump -p <port> to find specific keycodes

# Example

on_press
21 = playerctl previous # first key on an 88-key keyboard
22 = playerctl play-pause
23 = playerctl next
108 = /home/me/my_script.sh

## You can assign multiple commands to the same key

### On different triggers
on_press
60 = notify-send "Middle C pressed"
on_release
60 = notify-send "Middle C released"

### Or on the same trigger
on_press
60 = notify-send "command 1"
60 = notify-send "command 2"
```

## Building
```bash
git clone https://github.com/vustagc/pianoterm.git
cd pianoterm && make
```

### Dependencies
- C compiler
- alsactl (1.2.15.2)
- make (optional)
