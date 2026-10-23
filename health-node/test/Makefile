# Host build of the portable DSP + test harness. No ESP-IDF required.
#
#   make        -> generate WAVs, build, run the tests
#   make build  -> just compile
#   make clean
#
CC      ?= cc
DSP      = ../main/dsp
CFLAGS   = -std=c11 -O2 -Wall -Wextra -I$(DSP) -I.
LDFLAGS  = -lm

SRCS = test_dsp.c wav.c \
       $(DSP)/fft.c $(DSP)/features.c $(DSP)/baseline.c \
       $(DSP)/scoring.c $(DSP)/tonal.c

BIN = test_dsp
WAVDIR = wav

.PHONY: all build wavs run clean
all: run

$(BIN): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(BIN) $(LDFLAGS)

build: $(BIN)

wavs:
	python3 ../tools/gen_test_wavs.py -o $(WAVDIR)

run: $(BIN) wavs
	./$(BIN) $(WAVDIR)

clean:
	rm -f $(BIN)
	rm -rf $(WAVDIR)
