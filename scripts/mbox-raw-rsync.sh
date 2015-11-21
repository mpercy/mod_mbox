#!/bin/sh

/usr/local/bin/rsync \
  -arz --delete people.apache.org::public-arch /x1/mail-archives/raw/

