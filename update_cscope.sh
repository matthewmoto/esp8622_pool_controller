#!/bin/bash

find * -type f |grep -v build |grep cpp$ >cscope.files
find * -type f |grep -v build |grep h$ >>cscope.files
#find . -type f |grep -v build |grep portal |grep java$ >cscope.files
cscope -b
