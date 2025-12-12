#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

void int_sort(int* array, size_t count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (array[j] > array[j+1]) {
                int temp = array[j];
                array[j] = array[j+1];
                array[j+1] = temp;
            }
        }
    }
}

int main() {
    int* array;
    array = malloc(5 * sizeof(int));
    int n = 5;
    for (int i = 0; i < n; i++) {
        array[i] = 5 - i; 
        printf("%d", array[i]);
    }
    int_sort(array, n);
    for (int i = 0; i < n; i++) {
        printf("%d", array[i]);
    }

    return 0;
}
