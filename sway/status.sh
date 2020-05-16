#!/bin/sh
percent () {
  echo "$((($1 * 100) / $2))%"
}

dotime () {
  date +"%a %d %b %y, %H:%M"
}

dovolume () {
  data="$(pactl list | grep "Sink #$1" -A 20)"
  vol="$(printf "%s" "$data" | grep "Volume" | sed 1q | grep -o '[^ ]\+%' | tr '\n' ' ' | sed 's/ $//' | tr -d '\n')"

  if printf "%s" "$data" | grep "Mute: " | sed 1q | grep "yes">/dev/null; then
    printf "%s" "<span foreground=\"#ffb946\">$vol</span>"
  else
    printf "%s" "$vol"
  fi
}

dovols () {
  pactl list | grep "Sink #" | while read -r line; do
    id="$(echo "$line" | sed 's/Sink #//')"
    printf "%s" "#$id $(dovolume "$id")"
  done | sed 's/, $//'
}

update () {
  printf \
    "%s | SND: %s" "$(dotime)" "$(dovols)"
  return $?
}

dir="$(dirname "$0")"
file="$(basename "$0")"
pidfile="$dir/.$file.pid~"

if [ "$1" = "reload" ]; then
  kill -USR1 "$(cat "$pidfile")"
  exit $?
fi

if [ "$1" != "no-pidfile" ] ; then
  echo "$$" > "$pidfile"
fi
trap true USR1

while update; do sleep 1 & wait $!; done
