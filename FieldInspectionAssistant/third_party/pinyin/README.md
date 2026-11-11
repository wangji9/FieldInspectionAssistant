This directory contains the Pinyin input method and bundled decoder/dictionary
from Qt Virtual Keyboard v6.4.2, matching Ubuntu 24.04's Qt runtime.

Source: https://github.com/qt/qtvirtualkeyboard/tree/v6.4.2/src/plugins/pinyin
Layouts: https://github.com/qt/qtvirtualkeyboard/tree/v6.4.2/src/layouts/zh_CN

Original copyright notices and applicable licenses are retained in the source
files and LICENSES directory. The only layout change imports PinyinInputMethod
from the application's FieldOps.Input module; the decoder itself is unchanged.
