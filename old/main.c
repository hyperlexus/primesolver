#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// prime check, prime 1000-9999
bool is_prime(int n) {
    if (n % 2 == 0 || n % 3 == 0 || n % 5 == 0) return false;

    for (int i = 5; i * i <= n; i += 6) {
        int rem1, rem2;

        __asm__ (
            "mov %[n], %%eax\n\t"
            "cdq\n\t"
            "idiv %[i1]\n\t"
            "mov %%edx, %[r1]\n\t"
            "mov %[n], %%eax\n\t"
            "cdq\n\t"
            "idiv %[i2]\n\t"
            "mov %%edx, %[r2]"
            : [r1] "=r" (rem1), [r2] "=r" (rem2)
            : [n] "r" (n), [i1] "r" (i), [i2] "r" (i + 2)
            : "eax", "edx"
        );

        if (rem1 == 0 || rem2 == 0) return false;
    }
    return true;
}

// colour to (0..80)
int colours_to_code(const char *colours) {
    int code = 0;
    for (int i = 0; i < 4; i++) {
        code *= 3;
        if (colours[i] == 'y') code += 1;
        else if (colours[i] == 'g') code += 2;
    }
    return code;
}

void evaluate_guess(const char *guess, const char *t_in, char *r) {
    char t[5];
    memcpy(t, t_in, 5);
    memset(r, 'n', 4);
    r[4] = '\0';

    for (int i = 0; i < 4; i++) {
        if (guess[i] == t[i]) {
            r[i] = 'g';
            t[i] = '*';
        }
    }

    for (int i = 0; i < 4; i++) {
        if (r[i] == 'n') {
            for (int j = 0; j < 4; j++) {
                if (guess[i] == t[j]) {
                    r[i] = 'y';
                    t[j] = '*';
                    break;
                }
            }
        }
    }
}

// min maxxing
int choose_best_guess(char A[][5], int A_size, const int *P_indices, int P_size) {
    if (P_size <= 2) return P_indices[0];

    int best_guess_idx = P_indices[0];
    int min_max_group = 999999;

    for (int i = 0; i < A_size; i++) {
        int counts[81] = {0};
        char res[5];

        for (int j = 0; j < P_size; j++) {
            evaluate_guess(A[i], A[P_indices[j]], res);
            counts[colours_to_code(res)]++;
        }

        int max_group = 0;
        for (int k = 0; k < 81; k++) {
            if (counts[k] > max_group) max_group = counts[k];
        }

        // if tied, take an item from P
        bool in_P = false;
        for (int j = 0; j < P_size; j++) {
            if (P_indices[j] == i) { in_P = true; break; }
        }

        if (max_group < min_max_group || (max_group == min_max_group && in_P)) {
            min_max_group = max_group;
            best_guess_idx = i;
        }
    }
    return best_guess_idx;
}

int main(int argc, char *argv[]) {
    char A[1061][5];
    int A_size = 0;
    for (int i = 1000; i < 10000; i++) {
        if (is_prime(i)) sprintf(A[A_size++], "%d", i);
    }

    int P_indices[1061];
    int P_size = A_size;
    for (int i = 0; i < A_size; i++) P_indices[i] = i;

    char input_buffer[16];
    while (P_size > 1) {
        int g_idx = choose_best_guess(A, A_size, P_indices, P_size);
        printf("guess: %s ", A[g_idx]);
        fflush(stdout);

        if (!fgets(input_buffer, sizeof(input_buffer), stdin)) break;
        input_buffer[strcspn(input_buffer, "\r\n")] = '\0';

        if (strcmp(input_buffer, "gggg") == 0) break;

        int new_size = 0;
        char res[5];
        for (int i = 0; i < P_size; i++) {
            evaluate_guess(A[g_idx], A[P_indices[i]], res);
            if (strcmp(res, input_buffer) == 0) {
                P_indices[new_size++] = P_indices[i];
            }
        }
        P_size = new_size;
    }

    if (P_size == 1) printf("solution: %s\n", A[P_indices[0]]);
    return 0;
}