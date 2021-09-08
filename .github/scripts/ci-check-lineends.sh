#!/bin/bash

FN=$1

LAST_CHAR="$(tail -c 1 $FN | od -A n -t x1)"
if test "$LAST_CHAR" != " 0a"; then
    echo "File does not end with newline:" $FN
fi

LINEEND="$(dos2unix -i $FN | awk '{print $1 " " $3}')"
if test "$LINEEND" != "0 0"; then
    echo "File contains non-UNIX line endings:" $FN
fi
