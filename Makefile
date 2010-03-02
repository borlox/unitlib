##
# Makefile for unitlib
##

MSVC_COMPAT = -mno-cygwin -mms-bitfields

CC = gcc
CFLAGS = -O2 -std=c99 -Wall -Wextra $(MSVC_COMPAT)

AR = ar
RANLIB = ranlib

TARGET = libunit.a

DLL = libunit.dll
IMPLIB = libunit.lib

LUADLL = unitlib.dll
LUA_VERSION = lua51
LUA_INCLUDE = "$(LUA_DEV)/include"
LUA_LIBDIR  = "$(LUA_DEV)/lib"

LUA_LIBS = -l$(LUA_VERSION) -lmsvcr80

HEADER = unitlib-config.h unitlib.h

SRCFILES = unitlib.c parser.c format.c
HDRFILES = unitlib.h intern.h unitlib-config.h

DLL_INSTALL = /c/Windows
LIB_INSTALL = /g/Programmieren/lib
HDR_INSTALL = /g/Programmieren/include

OBJFILES = unitlib.o parser.o format.o

TESTPROG = _test.exe
SMASHPROG = _smash.exe

UNITTEST = ultest

.PHONY: test clean allclean

all: $(TARGET)

dll: $(DLL)

lua: $(LUADLL)

install-dll: dll
	cp $(DLL) $(DLL_INSTALL)
	cp $(IMPLIB) $(LIB_INSTALL)
	cp $(HEADER) $(HDR_INSTALL)

test: $(TESTPROG)
	@./$(TESTPROG)
	
utest: $(UNITTEST)
	@./$(UNITTEST)
	
smash: $(SMASHPROG)
	@./$(SMASHPROG)
	
$(TARGET): $(OBJFILES)
	@$(AR) rc $(TARGET) $(OBJFILES)
	@$(RANLIB) $(TARGET)
	
$(DLL): $(SRCFILES) $(HDRFILES)
	@$(CC) $(CFLAGS) -shared -o $(DLL) $(SRCFILES) -Wl,--out-implib,$(IMPLIB)
	
$(LUADLL): luabinding.o $(OBJFILES) $(HDRFILES)
	@$(CC) $(CFLAGS) -L$(LUA_LIBDIR) -shared -o $(LUADLL) luabinding.o $(OBJFILES) $(LUA_LIBS)
	
unitlib.o: unitlib.c $(HDRFILES)
	@$(CC) $(CFLAGS) -o unitlib.o -c unitlib.c
	
parser.o: parser.c $(HDRFILES)
	@$(CC) $(CFLAGS) -o parser.o -c parser.c
	
format.o: format.c $(HDRFILES)
	@$(CC) $(CFLAGS) -o format.o -c format.c
	
luabinding.o: luabinding.c $(HDRFILES)
	@$(CC) $(CFLAGS) -I$(LUA_INCLUDE) -o luabinding.o -c luabinding.c
	
$(TESTPROG): $(TARGET) _test.c
	@$(CC) -o $(TESTPROG) -g -L. _test.c -lunit
	
$(SMASHPROG): $(TARGET) _test.c
	@$(CC) -o $(SMASHPROG) -L. -DSMASH _test.c -lunit
	
$(UNITTEST): $(TARGET) unittest.c
	@$(CC) -std=gnu99 -o $(UNITTEST) -L. unittest.c -lunit
	
clean:
	@rm -f $(OBJFILES)
	@rm -f $(TESTPROG)
	@rm -f $(SMASHPROG)
	@rm -f $(UNITTEST)
	@rm -f debug.log
	
allclean: clean
	@rm -f $(TARGET)
	@rm -f $(DLL)
	@rm -f $(IMPLIB)
