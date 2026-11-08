ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
PYTHON ?= python3

ifeq ($(OS),Windows_NT)
CONSOLE := StudentManagement.exe
GUI := StudentManagementGUI.exe
RUN := .\\$(CONSOLE)
CLEAN := -del /Q StudentManagement.exe StudentManagementGUI.exe gui-resource.o 2>NUL
else
CONSOLE := StudentManagement
GUI := StudentManagementGUI.exe
RUN := ./$(CONSOLE)
CLEAN := rm -f StudentManagement StudentManagement.exe StudentManagementGUI.exe; rm -rf build
endif

.PHONY: all run test clean windows windows-cross
all: $(CONSOLE)

$(CONSOLE): main.c src/student_core.c src/student_core.h
	$(CC) $(CFLAGS) main.c src/student_core.c -o $(CONSOLE)

gui-resource.o: gui/app.rc gui/app.manifest
	windres gui/app.rc -O coff -o gui-resource.o

$(GUI): gui/win32_main.c src/student_core.c src/student_core.h gui-resource.o
	$(CC) $(CFLAGS) -D_WIN32_WINNT=0x0601 -I src -mwindows gui/win32_main.c src/student_core.c gui-resource.o -o $(GUI) -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -lole32

windows: $(CONSOLE) $(GUI)

windows-cross:
	sh scripts/build_windows.sh

run: $(CONSOLE)
	$(RUN)

test: $(CONSOLE)
	STUDENT_BINARY=$(CONSOLE) $(PYTHON) -m unittest discover -s tests -v

clean:
	$(CLEAN)
