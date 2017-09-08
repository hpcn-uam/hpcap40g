#!/bin/bash

ifconfig $1 | grep "RX p" | awk '{split($2,a,":");split($4,b,":");print a[2]/(a[2]+b[2]);}'
