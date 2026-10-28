#include <stdio.h>
#include <string.h>
#include <tlsh-wrapper.h>

int main () {

    int showvers = 1;
    Tlsh* t1 = Tlsh_new();
    Tlsh* t2 = Tlsh_new();

    const char *str1 = "This is a test for Lili Diao. This is a string. Hello Hello Hello ";
    const char *str2 = "This is a test for Jon Oliver. This is a string. Hello Hello Hello ";
    int len1 = strlen(str1);
    int len2 = strlen(str2);

    char minSizeBuffer1[512];
    for (int i = 0; i < 511; i++) {
        minSizeBuffer1[i] = i % 26 + 'A';
    }
    minSizeBuffer1[511] = 0;
    strncpy(minSizeBuffer1, str1, len1);
    Tlsh_final(t1, (const unsigned char*) minSizeBuffer1, 512, 0);

    char minSizeBuffer2[1024];
    for (int i = 0; i < 1023; i++) {
        minSizeBuffer2[i] = i % 26 + 'A';
    }
    minSizeBuffer2[1023] = 0;
    strncpy(minSizeBuffer2, str2, len2);
    Tlsh_final(t2, (const unsigned char*) minSizeBuffer2, 1024, 0);

    printf("str1 = '%s'\n", minSizeBuffer1 );
    printf("str2 = '%s'\n", minSizeBuffer2 );

    printf("hash1 = %s\n", Tlsh_get_hash(t1, showvers) );
    printf("hash2 = %s\n", Tlsh_get_hash(t2, showvers) );

    printf("difference (same strings) = %d\n", Tlsh_total_diff(t1, t1, 1) );
    printf("difference (with len) = %d\n", Tlsh_total_diff(t1, t2, 1) );
    printf("difference (without len) = %d\n", Tlsh_total_diff(t1, t2, 0) );

    Tlsh_delete(t1);
    Tlsh_delete(t2);
}
