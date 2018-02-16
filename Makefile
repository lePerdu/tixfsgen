#
# Makefile
#

PREFIX ?= /usr/local

SRC = src
BIN = bin
BUILD = build

SOURCES := $(addprefix $(SRC)/, tixfsgen.c ihex.c id_map.c)
OBJECTS := $(SOURCES:$(SRC)/%.c=$(BUILD)/%.o)
DEPS := $(SOURCES:$(SRC)/%.c=$(BUILD)/%.d)

TARGET := $(BIN)/tixfsgen

CFLAGS += -g
LDFLAGS +=

all: $(TARGET)

debug: $(TARGET)

clean:
	rm -rf $(BUILD) $(BIN)

install:
	install -m 755 $(TARGET) $(PREFIX)/bin

$(BUILD):
	@mkdir -p $@

$(BIN):
	@mkdir -p $@

$(TARGET): $(OBJECTS) | $(BIN)
	$(CC) $(LDFLAGS) -o $@ $^

-include $(DEPS)

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

.PHONY: all debug clean install
