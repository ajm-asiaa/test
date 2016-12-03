#!/bin/sh
cd ..
export CARTABUILDHOME=`pwd` 

mkdir -p $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/
cp $CARTABUILDHOME/build/cpp/core/libcore.1.dylib $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/
cp $CARTABUILDHOME/build/cpp/CartaLib/libCartaLib.1.dylib $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/

install_name_tool -change qwt.framework/Versions/6/qwt $CARTABUILDHOME/ThirdParty/qwt-6.1.2/lib/qwt.framework/Versions/6/qwt $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/MacOS/desktop
install_name_tool -change qwt.framework/Versions/6/qwt $CARTABUILDHOME/ThirdParty/qwt-6.1.2/lib/qwt.framework/Versions/6/qwt $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libcore.1.dylib
install_name_tool -change libplugin.dylib $CARTABUILDHOME/build/cpp/plugins/CasaImageLoader/libplugin.dylib $CARTABUILDHOME/build/cpp/plugins/ImageStatistics/libplugin.dylib
install_name_tool -change libcore.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libcore.1.dylib $CARTABUILDHOME/build/cpp/plugins/ImageStatistics/libplugin.dylib

install_name_tool -change libCartaLib.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libCartaLib.1.dylib $CARTABUILDHOME/build/cpp/plugins/ImageStatistics/libplugin.dylib
install_name_tool -change libcore.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libcore.1.dylib $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/MacOS/desktop
install_name_tool -change libCartaLib.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libCartaLib.1.dylib $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/MacOS/desktop
install_name_tool -change libCartaLib.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libCartaLib.1.dylib $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libcore.1.dylib

for f in `find . -name libplugin.dylib`; do install_name_tool -change libcore.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libcore.1.dylib $f; done
for f in `find . -name libplugin.dylib`; do install_name_tool -change libCartaLib.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libCartaLib.1.dylib $f; done
for f in `find . -name "*.dylib"`; do install_name_tool -change libwcs.5.15.dylib  $CARTABUILDHOME/ThirdParty/wcslib/lib/libwcs.5.15.dylib $f; echo $f; done
for f in `find . -name libplugin.dylib`; do install_name_tool -change libCartaLib.1.dylib  $CARTABUILDHOME/build/cpp/desktop/desktop.app/Contents/Frameworks/libCartaLib.1.dylib $f; done