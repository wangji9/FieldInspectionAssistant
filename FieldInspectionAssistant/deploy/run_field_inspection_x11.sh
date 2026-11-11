#!/bin/sh
# 现场巡检助手 - RK3588 板端启动脚本（X11/XWayland 备用，便于远程抓屏验证）
export QT_QPA_PLATFORM=xcb
export XDG_RUNTIME_DIR=/run/user/1000
export DISPLAY=:0
export WAYLAND_DISPLAY=wayland-0
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    DBUS_SESSION_BUS_ADDRESS=$(/usr/bin/python3 /home/ztl/FieldInspectionAssistant/deploy/open_field_inspection.py --session-bus)
    export DBUS_SESSION_BUS_ADDRESS
fi
export XAUTHORITY=/home/ztl/.Xauthority
if [ -z "${QT_QUICK_BACKEND:-}" ]; then
    if command -v eglinfo >/dev/null 2>&1 && eglinfo -B 2>/dev/null | awk '
        /^X11 platform:/ { x11 = 1; next }
        / platform:$/ { x11 = 0 }
        x11 && /OpenGL ES profile renderer: Mali-/ { hardware = 1 }
        END { exit !hardware }'
    then
        export QT_QUICK_BACKEND=rhi
        export QSG_RHI_BACKEND=opengl
        export QT_XCB_GL_INTEGRATION=xcb_egl
    else
        export QT_QUICK_BACKEND=software
    fi
fi
# Keep the desktop's resolution and use the window manager's native controls.
exec /home/ztl/FieldInspectionAssistant/build/field_inspection_assistant
