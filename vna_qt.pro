
QT += core gui
QT += widgets

TARGET = VNA_Qt
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
# For Visual Studio Compiler
DEFINES += _CRT_SECURE_NO_WARNINGS

CONFIG += c++11

SOURCES += \
        main.cpp \
        mainwindow.cpp \
        progress.cpp

HEADERS += \
        mainwindow.h \
        progress.h \
        typedefs.h \
        version.h

FORMS += \
        mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Path for VISA after installation of KeySight IOLibSuite_18_1_24130.exe (https://www.keysight.com/en/pd-1985909/io-libraries-suite)
# Used with Keysight 82357B USB/GPIB Interface USB 2.0
contains(QT_ARCH, i386) {
    message("32-bit")
    LIBS += "C:\Program Files (x86)\IVI Foundation\VISA\WinNT\lib\msc\visa32.lib"
    INCLUDEPATH += "C:\Program Files (x86)\IVI Foundation\VISA\WinNT\Include"
} else {
    message("64-bit")
    LIBS += "C:\Program Files\IVI Foundation\VISA\Win64\Lib_x64\msc\visa64.lib"
    INCLUDEPATH += "C:\Program Files\IVI Foundation\VISA\Win64\Include"
}

RESOURCES += \
    vna_qt.qrc

RC_FILE = resources.rc
