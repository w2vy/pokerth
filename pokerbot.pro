# QMake pro-file for the PokerTH TD Bot

isEmpty( PREFIX ){
	PREFIX =/usr
}

TEMPLATE = app
CODECFORSRC = UTF-8

CONFIG += thread console embed_manifest_exe exceptions rtti stl warn_on

UI_DIR = uics
TARGET = bin/pokerbot
MOC_DIR = mocs
OBJECTS_DIR = obj
DEFINES += PREFIX=\"$${PREFIX}\"
QT -= core gui
#PRECOMPILED_HEADER = src/pch_lib.h

INCLUDEPATH += . \
		src
DEPENDPATH += . \
		src

INCLUDEPATH += /usr/include/fmt
INCLUDEPATH += /usr/local/include

# Input
HEADERS += \
		src/game_defs.h \
		src/net/netpacket.h \
		src/third_party/protobuf/pokerth.pb.h

SOURCES += \
		src/pokerbot.cpp \
		src/net/common/netpacket.cpp \
		src/third_party/protobuf/pokerth.pb.cc

unix : !mac {

	##### My release static build options
	#QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections
	#QMAKE_LFLAGS += -Wl,--gc-sections

	QMAKE_LIBDIR += lib $${PREFIX}/lib /opt/gsasl/lib
	INCLUDEPATH += $${PREFIX}/include
	LIB_DIRS = $${PREFIX}/lib $${PREFIX}/lib64 $$system($$QMAKE_QMAKE -query QT_INSTALL_LIBS)
	BOOST_PROGRAM_OPTIONS = boost_program_options boost_program_options-mt
	BOOST_SYS = boost_system boost_system-mt


	#
	# searching in $PREFIX/lib, $PREFIX/lib64 and $$system($$QMAKE_QMAKE -query QT_INSTALL_LIBS)
	# to override the default '/usr' pass PREFIX
	# variable to qmake.
	#
	for(dir, LIB_DIRS){
		exists($$dir){
			for(lib, BOOST_PROGRAM_OPTIONS):exists($${dir}/lib$${lib}.so*) {
				message("Found $$lib")
				BOOST_PROGRAM_OPTIONS = -l$$lib
			}
			for(lib, BOOST_PROGRAM_OPTIONS):exists($${dir}/lib$${lib}.a) {
				message("Found $$lib")
				BOOST_PROGRAM_OPTIONS = -l$$lib
			}
			for(lib, BOOST_SYS):exists($${dir}/lib$${lib}.so*) {
				message("Found $$lib")
				BOOST_SYS = -l$$lib
			}
			for(lib, BOOST_SYS):exists($${dir}/lib$${lib}.a) {
				message("Found $$lib")
				BOOST_SYS = -l$$lib
			}
		}
	}
	BOOST_LIBS = $$BOOST_PROGRAM_OPTIONS $$BOOST_SYS
	!count(BOOST_LIBS, 2){
		error("Unable to find boost libraries in PREFIX=$${PREFIX}")
	}

	UNAME = $$system(uname -s)
	BSD = $$find(UNAME, "BSD")
	kFreeBSD = $$find(UNAME, "kFreeBSD")

	LIBS += $$BOOST_LIBS
	LIBS += -lprotobuf -lgsasl -lgcrypt -lidn
	LIBS += -lssl -lcrypto
	LIBS += -lboost_json -lfmt
	LIB_DIRS += /usr/local/lib

	# Enable C++17 (required for Boost.JSON)
	#CONFIG += c++17

	LIBS += -lfluxsign
	LIB_DIRS += ~/Git/fluxd/src

	#### INSTALL ####

	binary.path += $${PREFIX}/bin/
	binary.files += pokerbot

	INSTALLS += binary
}

# install boost_1_72 at /opt/boost_1_72
#
# git checkout 83fb1bcef49b1c12ef349f62d90bfcc83f0f7398
# ./autogen.sh
# ./configure --prefix=/usr/local --enable-module-recovery --enable-experimental
# make
# sudo make install
#
# checkout bitcoin-system ver 2.0.10
#./autogen.sh
#
#./configure \
#  --prefix=/usr/local \
#  --with-boost=/opt/boost_1_72 \
#  --with-boost-libdir=/opt/boost_1_72/lib \
#  CXXFLAGS="-I/opt/boost_1_72/include -I/opt/openssl-1.0.2/include" \
#  LDFLAGS="-L/opt/boost_1_72/lib -L/opt/openssl-1.0.2/lib -Wl,-rpath=/opt/openssl-1.0.2/lib"
