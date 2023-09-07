#!/bin/bash

dedup_name=$1
outfile=$2
sudo dmsetup status ${dedup_name} >> ${outfile}