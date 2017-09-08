#!/bin/bash

disco="/dev/$1"

echo "[ Creating GPT partition table for $disco ... ]"
#GPT partition table for > 2TB size
parted $disco mklabel gpt
parted $disco mkpart primary xfs 0 ­-0
parted $disco print

echo "[ Creating XFS filesystem for $disco ... ]"
mkfs.xfs $disco
