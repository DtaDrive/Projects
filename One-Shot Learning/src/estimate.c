#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

void printMatrix(double** result, int rows, int columns);
void transpose(int rows, int columns, double** m1, double** result);
void multiply(int rows, int colsFirst, int colsSecond, double** m1, double** m2, double** result );
void invert(int size, double** m1, double**iMatrix);


int main(int argc, char **argv){
    FILE *file = fopen(argv[1], "r");
    if(file == NULL){
        return EXIT_FAILURE;
    }

    int rows;
    int cols;
    char train[100];
    fscanf(file, "%s", train);
    fscanf(file, "%d", &cols);
    fscanf(file, "%d", &rows);
    cols++;
    
    double** X = (double**)malloc(rows*sizeof(double*));
    double** Xtranspose = (double**)malloc(cols*sizeof(double*));
    double** Xresult = (double**)malloc(cols*sizeof(double*));
    double** Y = (double**)malloc(rows*sizeof(double*));
    double** priceVector = (double**)malloc(cols*sizeof(double*));
    double** iMatrix = (double**)malloc(rows*sizeof(double*));
    double** invTrans = (double**)malloc(cols*sizeof(double*));
    
    

    for(int i =0; i<rows; i++){
        X[i] = (double*)malloc(cols*sizeof(double));
        iMatrix[i] = (double*)malloc(rows*sizeof(double));
        Y[i] = (double*)malloc(1*sizeof(double));

    }
    for(int i = 0; i<cols; i++){
        Xtranspose[i] = (double*)malloc(rows*sizeof(double));
        Xresult[i] = (double*)malloc(cols*sizeof(double));
        invTrans[i] = (double*)malloc(rows*sizeof(double));
        priceVector[i] = (double*)malloc(1*sizeof(double));
    }


    //Column 1 should consist of all 1's

    for(int i = 0; i<rows; i++){
        for(int j = 0; j<cols; j++){
            if(j==0) {
                X[i][j] = 1.0;
                
            }else{
            fscanf(file, "%lf", &X[i][j]);
            if (j == (cols-1)) fscanf(file, "%lf", &Y[i][0]);
            }
        }
    }


    
    transpose(rows, cols, X, Xtranspose);
    
    multiply(cols, rows, cols, Xtranspose, X, Xresult);


    invert(cols ,Xresult, iMatrix);

    multiply(cols, cols, rows, iMatrix, Xtranspose, invTrans);

    multiply(cols, rows, 1, invTrans, Y, priceVector);


    FILE *file2 = fopen(argv[2], "r");

    int checkRows;
    int checkCols;
    char data[50];

    fscanf(file2, "%s", data);
    fscanf(file2, "%d", &checkCols);
    fscanf(file2, "%d", &checkRows);
    checkCols++;



    double** check = (double**)malloc(checkRows*sizeof(double*));
    double** final = (double**)malloc(checkRows*sizeof(double*));
    for (int i = 0; i<checkRows; i++){
        check[i] = (double*)malloc(checkCols*sizeof(double));
        final[i] = (double*)malloc(1*sizeof(double));
    }

    for(int i = 0; i<checkRows; i++){
        for(int j = 0; j<checkCols; j++){
            if(j==0) {
                check[i][j] = 1.0;
                
            }else{
            fscanf(file2, "%lf", &check[i][j]);
            }
        }
    }

    multiply(checkRows, checkCols, 1, check, priceVector, final);

    for(int i = 0; i<checkRows; i++){
        printf("%.0f\n", final[i][0]);
    }


    for(int i = 0; i<rows; i++){
        free(X[i]);
        free(Y[i]);
        free(iMatrix[i]);
        
    }

    for(int i = 0; i<cols; i++){
        free(Xtranspose[i]);
        free(Xresult[i]);
        free(priceVector[i]);
        free(invTrans[i]);
    }

    for(int i = 0; i<checkRows; i++){
        free(check[i]);
        free(final[i]);
    }
    

    free(X);
    free(Xtranspose);
    free(Xresult);
    free(Y);
    free(priceVector);
    free(check);
    free(final);
    free(iMatrix);
    free(invTrans);

    return EXIT_SUCCESS;

}


void printMatrix(double** result, int rows, int columns){
    for (int i =0; i < rows; i++){
        for (int j = 0; j < columns; j++){
             printf("%.6f ", result[i][j]);
        }
        printf("\n");
    }
}



void transpose(int rows, int columns, double** m1, double** result){

    for(int i = 0; i<rows; i++){
        for(int j =0; j<columns; j++){
            result[j][i] = m1[i][j];
        }
    }
}

void multiply(int rows, int cols, int colsSecond, double** m1, double** m2, double** result) {
    double** tempMatrix = (double**)malloc(rows*sizeof(double*));
    if(!tempMatrix) return;
    for(int i = 0; i<rows; i++){
        tempMatrix[i] = (double*)malloc(colsSecond*sizeof(double));
        if(!tempMatrix[i]) return;
    }

    for (int i = 0; i<rows; i++){
        for(int j = 0; j<colsSecond; j++){
            tempMatrix[i][j]=0.0;
            for (int k = 0; k<cols; k++){
                tempMatrix[i][j] += (m1[i][k] * m2[k][j]);
            }
        }
    }

    for(int i = 0; i<rows; i++){
        for(int j = 0; j<colsSecond; j++){

            result[i][j] = tempMatrix[i][j];
        }
    }

    for(int i = 0; i<rows; i++){
        free(tempMatrix[i]);
    }
    free(tempMatrix);
}

void invert(int size, double** m1, double**iMatrix){

    for(int i = 0; i<size; i++){
        for (int j = 0; j<size; j++){
            if(i==j) iMatrix[i][j] = 1;
            else iMatrix[i][j] = 0;
        }
    
    }

    for(int p = 0; p<size; p++){
        double f = m1[p][p];
        for(int r = 0; r<size; r++){
            m1[p][r] = m1[p][r]/f;
            iMatrix[p][r] = iMatrix[p][r]/f;
        }
        for(int i = p+1; i<size; i++){
            f = m1[i][p];
            for(int r = 0; r<size; r++){
                m1[i][r] -= (m1[p][r] * f);
                iMatrix[i][r] -= (iMatrix[p][r] * f);
            }
        }
    }
    for(int p = size-1; p>=0; p--){
        for(int i = p-1; i>=0; i--){
            double f = m1[i][p];
            for(int r = 0; r<size; r++){
                m1[i][r] -= (m1[p][r]*f);
                iMatrix[i][r] -= (iMatrix[p][r] * f);
            }
        }
    }
}