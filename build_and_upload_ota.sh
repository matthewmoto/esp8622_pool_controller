#!/bin/bash

ESP_IP_ADDR="$1"
pio run --target=upload --upload-port=$1 
