#!/bin/sh
set -eu

plasmashell &
konsole --separate -e sh -c 'echo "M4 Plasma 6 GPU validation"; sleep 25' &
sleep 10
qdbus6 org.kde.KWin /KWin supportInformation || true
wait
