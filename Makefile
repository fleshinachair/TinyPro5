# TinyPro5 - thin wrapper; the scripts hold the logic so `make` and CI/Windows shells behave identically.
#   make test      native unit tests        (scripts/test.sh)
#   make arm       AArch64 dsp.so + package (scripts/build.sh)  -> TinyPro5.tar.gz
#   make package   alias of `arm`
#   make clean

.PHONY: all test arm package clean

all: test arm

test:
	./scripts/test.sh

arm:
	./scripts/build.sh

package: arm

clean:
	rm -rf build dist TinyPro5.tar.gz
