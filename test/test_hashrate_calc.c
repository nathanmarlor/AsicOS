#include <stdio.h>
#include <math.h>

int main() {
    /* Test: at 500MHz with 2040 cores, 2 chips:
     * Expected: ~2040 GH/s total
     * At difficulty 256: nonce rate = 2040e9 / (256 * 4294967296) = ~1.855 nonces/sec
     * Over 10 seconds: ~18.55 nonces
     */
    double nonces = 18.55;
    double dt_sec = 10.0;
    double asic_diff = 256.0;
    double total_hashes = nonces * asic_diff * 4294967296.0;
    double ghs = total_hashes / dt_sec / 1e9;

    printf("Nonces: %.1f in %.1fs\n", nonces, dt_sec);
    printf("Hashrate: %.2f GH/s (expected ~2040)\n", ghs);

    if (fabs(ghs - 2040.0) < 200.0) {
        printf("PASS: hashrate within expected range\n");
        return 0;
    } else {
        printf("FAIL: hashrate %.2f not near expected 2040\n", ghs);
        return 1;
    }
}
