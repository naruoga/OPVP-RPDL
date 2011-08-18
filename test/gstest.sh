#!/bin/sh

date

gs -r600 -sDEVICE=opvp -sDriver=libopvpnull.so -sModel=model_name -sJobInfo="job_info" -q -dBATCH -dSAFER -dQUIET -dNOPAUSE -sOutputFile=- $1

date
