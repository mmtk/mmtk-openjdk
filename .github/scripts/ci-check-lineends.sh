#!/bin/bash

FN=$1

ANY_ERRORS=0

LAST_CHAR="$(tail -c 1 $FN | od -A n -t x1)"
if test "$LAST_CHAR" != " 0a"; then
    echo "File does not end with newline:" $FN
    ANY_ERRORS=1
fi

LINEEND="$(dos2unix -i $FN | awk '{print $1 " " $3}')"
if test "$LINEEND" != "0 0"; then
    echo "File contains non-UNIX line endings:" $FN
    ANY_ERRORS=1
fi

if [[ $ANY_ERRORS -ne 0 ]]; then
    exit 1
fi
