#!/bin/sh
cd $HOME

fcitx &
sxhkd &
compton &
dunst &
xautolock -time 10 -locker '/usr/bin/i3lock' &
./.scripts/warningbattery.sh &
./.scripts/dwmbar.sh &
./.scripts/dwmtime.sh &
