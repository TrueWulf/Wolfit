CXX ?= c++
TARGET := wolfit
SOURCE := main.cpp editor.cpp tabs.cpp detect.cpp theme.cpp
MUSL_PREFIX ?= $(CURDIR)/third_party/fltk-musl
MUSL_CXX ?= /opt/cross/x86_64-linux-musl-cross/bin/x86_64-linux-musl-g++

CXXFLAGS := -std=c++17 -Os -DNDEBUG -fno-rtti -fno-exceptions -ffunction-sections -fdata-sections -Wall -Wextra
LDFLAGS := -Wl,--gc-sections
FLTK_CXXFLAGS := $(filter-out -fexceptions,$(shell fltk-config --cxxflags 2>/dev/null))
FLTK_LDFLAGS := $(shell fltk-config --ldflags 2>/dev/null)
MUSL_CXXFLAGS := $(CXXFLAGS) -I$(MUSL_PREFIX)/include
MUSL_LDFLAGS := -static -Wl,--gc-sections -L$(MUSL_PREFIX)/lib -lfltk -lwayland-client -lwayland-cursor -lxkbcommon -ldl -lpthread -lm

$(TARGET): $(SOURCE) editor.h tabs.h detect.h theme.h version.h
	$(CXX) $(CXXFLAGS) $(FLTK_CXXFLAGS) $(SOURCE) -o $@ $(LDFLAGS) $(FLTK_LDFLAGS)

musl: $(SOURCE)
	@test -f $(MUSL_PREFIX)/lib/libfltk.a || { printf '%s\n' 'Missing musl FLTK. Build it with scripts/build-fltk-musl-wayland.sh using a musl dependency sysroot.' >&2; exit 1; }
	$(MUSL_CXX) $(MUSL_CXXFLAGS) $(SOURCE) -o $(TARGET)-musl $(MUSL_LDFLAGS)
	strip --strip-unneeded $(TARGET)-musl
	file $(TARGET)-musl

clean:
	rm -f $(TARGET)

.PHONY: clean musl
