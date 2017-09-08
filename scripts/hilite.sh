#!/bin/bash

color_to_num () {
    case $1 in
        black)  echo 0;;
        red)    echo 1;;
        green)  echo 2;;
        yellow) echo 3;;
        blue)   echo 4;;
        purple) echo 5;;
        cyan)   echo 6;;
        white)  echo 7;;
        *)      echo 0;;
    esac
}

# default values for foreground and background colors
bg=
fg=

while getopts f:b: option; do
    case "$option" in
        f) fg="$OPTARG";;
        b) bg="$OPTARG";;
    esac
done

shift $(($OPTIND - 1))

pattern="$*"

if [ -n "$fg" ]; then
    fg=$(tput setaf $(color_to_num $fg))
fi
if [ -n "$bg" ]; then
    bg=$(tput setab $(color_to_num $bg))
fi

if [ -z "$fg$bg" ]; then
    fg=$(tput smso)
fi

sed "s/$pattern/$(tput bold)${fg}${bg}&$(tput sgr0)/g"