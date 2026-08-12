include Makefile.libretro

.PHONY: test-dual-contract
test-dual-contract:
	@test -n "$(ROM_A)" -a -n "$(ROM_B)" || { \
		echo "usage: make test-dual-contract ROM_A=console-a.gb ROM_B=console-b.gb"; \
		exit 2; \
	}
	@$(CC) -std=gnu99 -O2 -Ilibgambatte/libretro-common/include \
		-Ilibgambatte/libretro tests/dual_contract.c -ldl \
		-o /tmp/gambatte-dual-contract
	@/tmp/gambatte-dual-contract ./gambatte_libretro.so "$(ROM_A)" "$(ROM_B)"
