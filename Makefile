ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
PYTHON ?= python3

ifeq ($(OS),Windows_NT)
TARGET := StudentManagement.exe
RUN := .\$(TARGET)
CLEAN := -del /Q $(TARGET) 2>NUL
else
TARGET := StudentManagement
RUN := ./$(TARGET)
CLEAN := rm -f $(TARGET)
endif

.PHONY: all run test clean
all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET)

run: $(TARGET)
	$(RUN)

test: $(TARGET)
	$(PYTHON) -m unittest discover -s tests -v

clean:
	$(CLEAN)
