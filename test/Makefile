CC = gcc
CFLAGS = -Wall -Wextra -I../components/mining/include -I../components/asic_driver/include -I../components/stratum/include -I../components/power/include

# OpenSSL paths (Homebrew on macOS)
OPENSSL_CFLAGS = -I/opt/homebrew/opt/openssl@3/include
OPENSSL_LDFLAGS = -L/opt/homebrew/opt/openssl@3/lib

LDFLAGS = $(OPENSSL_LDFLAGS) -lssl -lcrypto -lm

all: test_crc test_mining test_pid test_stratum test_nonce_verify test_hashrate_hw

test_crc: test_crc.c ../components/asic_driver/asic_packet.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_mining: test_mining.c
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ $^ $(LDFLAGS)

test_pid: test_pid.c ../components/power/pid.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_stratum: test_stratum.c
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ $^ $(LDFLAGS)

test_nonce_verify: test_nonce_verify.c
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -o $@ $^ $(LDFLAGS)

test_hashrate_hw: test_hashrate_hw.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	@echo ""; echo "=== test_crc ==="; ./test_crc
	@echo ""; echo "=== test_mining ==="; ./test_mining
	@echo ""; echo "=== test_pid ==="; ./test_pid
	@echo ""; echo "=== test_stratum ==="; ./test_stratum
	@echo ""; echo "=== test_nonce_verify ==="; ./test_nonce_verify
	@echo ""; echo "=== test_hashrate_hw ==="; ./test_hashrate_hw

clean:
	rm -f test_crc test_mining test_pid test_stratum test_nonce_verify test_hashrate_hw

.PHONY: all run clean
