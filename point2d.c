#include <stdio.h>
#include <math.h>

typedef struct {
    float x;
    float y;
} point;

float length(point p) {
    float temp = p.x * p.x + p.y * p.y;
    return sqrt(temp);
}

int main(int argc, char** argv) {
    point p;
    p.x = 3;
    p.y = 4;
    printf("%f\n", length(p));
    return 0;
}
