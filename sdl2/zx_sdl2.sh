#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
source $controlfolder/device_info.txt

get_controls

GAMEDIR="$(cd "$(dirname "$0")" && pwd)/zxtune"

EXEC="$GAMEDIR/zxtune_sdl2"
GPTK_CONFIG="$GAMEDIR/zxtune.gptk"

CUR_TTY=/dev/tty0
$ESUDO chmod 666 $CUR_TTY

exec > >(tee "$GAMEDIR/log.txt") 2>&1

cd $GAMEDIR

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export LD_LIBRARY_PATH="$GAMEDIR/lib:$LD_LIBRARY_PATH"

if [ ! -f "$EXEC" ]; then
    echo "ERROR: Cannot find $EXEC"
    exit 1
fi

if [ ! -f "$GPTK_CONFIG" ]; then
    echo "ERROR: Cannot find $GPTK_CONFIG"
    exit 1
fi

PROC_NAME="zxtune_sdl2"
$GPTOKEYB "$PROC_NAME" -c "$GPTK_CONFIG" &

"$EXEC"

$ESUDO kill -9 $(pidof gptokeyb)
$ESUDO systemctl restart oga_events &
printf "\033c" > /dev/tty0
