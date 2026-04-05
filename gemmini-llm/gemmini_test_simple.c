/**
 * gemmini_test_simple.c - Simple Gemmini functional test
 *
 * Test 1: mvin/mvout (scratchpad read/write)
 * Test 2: tiled_matmul_auto (16x16 matrix multiplication)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "include/gemmini_params.h"
#include "include/gemmini.h"

static inline uint64_t get_time(void) {
    uint64_t time;
    asm volatile ("rdtime %0" : "=r" (time));
    return time;
}

//=============================================================================
// Test 1: mvin/mvout - verify scratchpad read/write
//=============================================================================
static int test_mvin_mvout(void) {
    printf("\n=== Test 1: mvin/mvout (scratchpad read/write) ===\n");

    static elem_t src[DIM][DIM] __attribute__((aligned(64)));
    static elem_t dst[DIM][DIM] __attribute__((aligned(64)));

    printf("Initializing source data...\n");
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            src[i][j] = i * DIM + j + 1;  // 1, 2, 3, ..., 64
        }
    }
    memset(dst, 0, sizeof(dst));

    printf("src[0][0..7] = ");
    for (int j = 0; j < DIM; j++) printf("%d ", src[0][j]);
    printf("\n");

    printf("Configuring Gemmini...\n");
    gemmini_flush(0);
    gemmini_config_ld(DIM * sizeof(elem_t));
    gemmini_config_st(DIM * sizeof(elem_t));

    printf("mvin: writing data to scratchpad...\n");
    gemmini_mvin(src, 0);
    gemmini_fence();

    printf("mvout: reading data from scratchpad...\n");
    gemmini_mvout(dst, 0);
    gemmini_fence();

    printf("dst[0][0..7] = ");
    for (int j = 0; j < DIM; j++) printf("%d ", dst[0][j]);
    printf("\n");

    int errors = 0;
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            if (src[i][j] != dst[i][j]) {
                if (errors < 5) {
                    printf("ERROR: src[%d][%d]=%d, dst[%d][%d]=%d\n",
                           i, j, src[i][j], i, j, dst[i][j]);
                }
                errors++;
            }
        }
    }

    if (errors == 0) {
        printf("PASS: mvin/mvout test passed!\n");
        return 1;
    } else {
        printf("FAIL: mvin/mvout test failed, %d errors\n", errors);
        return 0;
    }
}

//=============================================================================
// Test 2: tiled_matmul_auto (16x16 matrix multiplication)
//=============================================================================
static int test_tiled_matmul(void) {
    printf("\n=== Test 2: tiled_matmul_auto (16x16 matmul) ===\n");

    #define SIZE 16

    static elem_t A[SIZE][SIZE] __attribute__((aligned(64)));
    static elem_t B[SIZE][SIZE] __attribute__((aligned(64)));
    static elem_t C[SIZE][SIZE] __attribute__((aligned(64)));
    static elem_t C_gold[SIZE][SIZE];

    printf("Initializing %dx%d matrices...\n", SIZE, SIZE);
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            A[i][j] = (i + j) % 3;  // 0, 1, 2
            B[i][j] = (i * j) % 3;  // 0, 1, 2
        }
    }
    memset(C, 0, sizeof(C));

    // CPU golden reference (with int8 saturation)
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            int32_t sum = 0;
            for (int k = 0; k < SIZE; k++) {
                sum += A[i][k] * B[k][j];
            }
            if (sum > 127) sum = 127;
            if (sum < -128) sum = -128;
            C_gold[i][j] = sum;
        }
    }

    printf("Expected C_gold[0][0..7] = ");
    for (int j = 0; j < 8; j++) printf("%d ", C_gold[0][j]);
    printf("\n");

    printf("Running Gemmini tiled_matmul_auto...\n");
    gemmini_flush(0);

    uint64_t start = get_time();

    tiled_matmul_auto(
        SIZE, SIZE, SIZE,
        (elem_t*)A, (elem_t*)B,
        NULL, (elem_t*)C,
        SIZE, SIZE, SIZE, SIZE,
        MVIN_SCALE_IDENTITY,
        MVIN_SCALE_IDENTITY,
        MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false, false,
        false, false,
        0, WS
    );

    gemmini_fence();
    uint64_t end = get_time();

    printf("Gemmini elapsed: %lu cycles\n", end - start);

    printf("Gemmini  C[0][0..7] = ");
    for (int j = 0; j < 8; j++) printf("%d ", C[0][j]);
    printf("\n");

    int errors = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (C[i][j] != C_gold[i][j]) {
                if (errors < 5) {
                    printf("ERROR: C[%d][%d]=%d, expected=%d\n",
                           i, j, C[i][j], C_gold[i][j]);
                }
                errors++;
            }
        }
    }

    if (errors == 0) {
        printf("PASS: tiled_matmul_auto test passed!\n");
        return 1;
    } else {
        printf("FAIL: tiled_matmul_auto test failed, %d errors\n", errors);
        return 0;
    }

    #undef SIZE
}

//=============================================================================
// Main
//=============================================================================
int main(void) {
    printf("========================================\n");
    printf("  Gemmini Functional Test\n");
    printf("========================================\n");
    printf("DIM=%d, elem_t=int8, acc_t=int32\n", DIM);

    int passed = 0;
    int total = 2;

    if (test_mvin_mvout()) passed++;
    if (test_tiled_matmul()) passed++;

    printf("\n========================================\n");
    printf("  Result: %d / %d passed\n", passed, total);
    printf("========================================\n");

    if (passed == total) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("SOME TESTS FAILED\n");
        return 1;
    }
}
