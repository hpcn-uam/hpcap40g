#!/bin/bash

#http://www.linux-archive.org/fedora-user/334882-ot-generate-graphical-tree-svg-png-directory-structure.html

function recurse ()
{
	SUBS=$(find $1 -mindepth 1 -maxdepth 1 -type d -exec basename {} \; )
	for a in $SUBS
	do
		echo ""$2" -> "$a";" >> $DOTFILE
		recurse "$1"/"$a" "$a"
	done
}

DOTFILE=$(mktemp)
echo $DOTFILE

export IFS=$'
'

echo "digraph unix {" > $DOTFILE
echo "node [color=lightblue2, style=filled];" >> $DOTFILE
recurse $1 $1
echo "}" >> $DOTFILE
dot -Tpng $DOTFILE > dirs.png
eog dirs.png &
