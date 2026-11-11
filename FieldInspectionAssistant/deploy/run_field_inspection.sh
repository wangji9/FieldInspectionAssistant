#!/bin/sh
# 现场巡检助手，使用板端系统 Qt 6 与当前 X11 桌面。
export FIELDOPS_PAGE=${FIELDOPS_PAGE:-ai}
exec /home/ztl/FieldInspectionAssistant/deploy/run_field_inspection_x11.sh "$@"
