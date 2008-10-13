#!/bin/bash
# simplistic script to run after checkout

aclocal && \
autoheader && \
autoconf && \
automake -a -c
