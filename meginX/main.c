/* 
 * File:   main.c
 * Author: Beck Xu
 * 一个不相干的算法...
 * Created on 2014年5月12日, 下午5:11
 */

/**
1 2 4 7 
3 5 8 11 
6 9 12 14 
10 13 15 16
 */
#include <stdio.h>
#include <stdlib.h>

const int N=1010;
int q[N][N];

/*
 * 
 */
int main(int argc, char** argv) {
    int n =4;
    int i = 1,j=1;
    for (int k = 1; k <= n*n; k++) {
        q[i][j] = k;
        if (i == n) {
            i = 1+j;
            j = n;
        } else if (j==1) {
            j = i+1;
            i = 1;
        } else {
            i++;
            j--;
        }
    }
    
    for (int i=1;i<=n;i++,puts(""))
        for (int j =1;j<=n;j++)
            printf("%d ", q[i][j]);
    return 0;
}

