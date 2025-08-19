/* bmp_tool.c : Lee un BMP 24bpp, hace escala de grises o convolución 3x3 y guarda otro BMP.
   Compilar: gcc -std=c11 -Wall -Wextra -O2 bmp_tool.c -o bmp_tool
   Ejecutar: ./bmp_tool
*/

#include <stdio.h>  // fopen, fread, fwrite, printf, scanf
#include <stdlib.h> // malloc, free, exit
#include <stdint.h> // uint8_t, uint16_t, uint32_t
#include <string.h> // memset

// --- Estructuras de cabecera BMP ---
#pragma pack(push, 1)
typedef struct
{
    uint16_t bfType;      // Debe ser 0x4D42 = 'BM'
    uint32_t bfSize;      // Tamaño total del archivo en bytes
    uint16_t bfReserved1; // 0
    uint16_t bfReserved2; // 0
    uint32_t bfOffBits;   // Offset donde comienzan los datos de píxeles
} BMPHeader;

typedef struct
{
    uint32_t biSize;         // Tamaño de este header (40 bytes para BITMAPINFOHEADER)
    int32_t biWidth;         // Ancho en píxeles
    int32_t biHeight;        // Alto en píxeles (positivo = bottom-up)
    uint16_t biPlanes;       // Debe ser 1
    uint16_t biBitCount;     // Bits por píxel (esperamos 24)
    uint32_t biCompression;  // 0 = BI_RGB (sin compresión)
    uint32_t biSizeImage;    // Tamaño de la imagen (puede ser 0 para BI_RGB)
    int32_t biXPelsPerMeter; // Resolución X
    int32_t biYPelsPerMeter; // Resolución Y
    uint32_t biClrUsed;      // Colores usados (0 = todos)
    uint32_t biClrImportant; // Colores importantes (0 = todos)
} BMPInfoHeader;
#pragma pack(pop)

// Estructura de un píxel BGR de 24 bits
typedef struct
{
    uint8_t b, g, r;
} Pixel24;

// Función para recortar un entero a [0,255]
static inline uint8_t clamp_int_to_u8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

// Carga BMP 24bpp sin compresión, altura > 0.
// Devuelve un bloque de Pixel24 de tamaño width*height (ordenado de arriba a abajo, izquierda a derecha).
int load_bmp24(const char *filename,
               BMPHeader *out_fh, BMPInfoHeader *out_ih,
               Pixel24 **out_pixels)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        perror("No se pudo abrir el archivo");
        return 0;
    }

    BMPHeader fh;
    BMPInfoHeader ih;
    if (fread(&fh, sizeof(fh), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }
    if (fread(&ih, sizeof(ih), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    // Validaciones mínimas de formato
    if (fh.bfType != 0x4D42)
    { // 'BM'
        fprintf(stderr, "No es un BMP valido (firma BM).\n");
        fclose(f);
        return 0;
    }
    if (ih.biBitCount != 24 || ih.biCompression != 0)
    {
        fprintf(stderr, "Solo se soporta BMP 24-bpp sin compresion.\n");
        fclose(f);
        return 0; // También devuelve headers por si quieres reusarlos.
    }
    if (ih.biWidth <= 0 || ih.biHeight <= 0)
    {
        fprintf(stderr, "Solo se soportan dimensiones positivas.\n");
        fclose(f);
        return 0;
    }

    // Vamos al inicio de los datos de pixeles
    fseek(f, (long)fh.bfOffBits, SEEK_SET);

    int width = ih.biWidth;
    int height = ih.biHeight;

    // Padding por fila (múltiplo de 4 bytes)
    int row_bytes = width * 3;
    int padding = (4 - (row_bytes % 4)) % 4;

    // Reserva memoria para la imagen ordenada de ARRIBA hacia ABAJO (forma natural de trabajar)
    Pixel24 *pixels = (Pixel24 *)malloc(sizeof(Pixel24) * (size_t)width * (size_t)height);
    if (!pixels)
    {
        fclose(f);
        return 0;
    }

    // El BMP viene de ABAJO hacia ARRIBA: leemos fila por fila y la colocamos invertida verticalmente
    for (int y = 0; y < height; ++y)
    {
        int dest_y = height - 1 - y; // invertimos el orden vertical
        Pixel24 *row = &pixels[dest_y * width];
        // Leemos los pixeles de la fila
        if (fread(row, 3, (size_t)width, f) != (size_t)width)
        {
            fprintf(stderr, "Lectura de fila incompleta.\n");
            free(pixels);
            fclose(f);
            return 0;
        }
        // Saltamos el padding
        if (padding)
            fseek(f, padding, SEEK_CUR);
    }

    fclose(f);
    *out_fh = fh;
    *out_ih = ih;
    *out_pixels = pixels;
    return 1;
}

// Guarda BMP 24bpp sin compresion con los pixeles en arreglo de ARRIBA hacia ABAJO.
int save_bmp24(const char *filename,
               const BMPInfoHeader *src_ih, const Pixel24 *pixels)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
    {
        perror("No se pudo crear el archivo");
        return 0;
    }

    int width = src_ih->biWidth;
    int height = src_ih->biHeight;

    int row_bytes = width * 3;
    int padding = (4 - (row_bytes % 4)) % 4;
    uint32_t image_size = (row_bytes + padding) * (uint32_t)height;

    BMPHeader fh;
    BMPInfoHeader ih = *src_ih; // copiamos la info base

    // Ajustamos campos de tamaño
    ih.biSize = sizeof(BMPInfoHeader);
    ih.biCompression = 0;
    ih.biBitCount = 24;
    ih.biPlanes = 1;
    ih.biSizeImage = image_size;

    fh.bfType = 0x4D42; // 'BM'
    fh.bfOffBits = sizeof(BMPHeader) + sizeof(BMPInfoHeader);
    fh.bfSize = fh.bfOffBits + image_size;
    fh.bfReserved1 = 0;
    fh.bfReserved2 = 0;

    // Escribimos cabeceras
    if (fwrite(&fh, sizeof(fh), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }
    if (fwrite(&ih, sizeof(ih), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    // Escribimos filas de ABAJO hacia ARRIBA (formato BMP)
    uint8_t pad[3] = {0, 0, 0};
    for (int y = height - 1; y >= 0; --y)
    {
        const Pixel24 *row = &pixels[y * width];
        if (fwrite(row, 3, (size_t)width, f) != (size_t)width)
        {
            fclose(f);
            return 0;
        }
        if (padding)
            fwrite(pad, 1, (size_t)padding, f);
    }

    fclose(f);
    return 1;
}

// Convierte en el mismo arreglo a escala de grises.
void to_grayscale(Pixel24 *pixels, int width, int height)
{
    for (int i = 0; i < width * height; ++i)
    {
        int r = pixels[i].r;
        int g = pixels[i].g;
        int b = pixels[i].b;
        int gray = (int)(0.299 * r + 0.587 * g + 0.114 * b + 0.5); // +0.5 para redondear
        uint8_t g8 = clamp_int_to_u8(gray);
        pixels[i].r = pixels[i].g = pixels[i].b = g8;
    }
}

// Aplica convolución 3x3 sobre la imagen (asumiendo GRAYSCALE ya).
// Copiamos bordes sin cambio para simplificar.
void convolve3x3(Pixel24 *pixels, int width, int height, const float k[3][3])
{
    // Creamos una copia en escala de grises de un canal (como uint8_t)
    uint8_t *src = (uint8_t *)malloc((size_t)width * (size_t)height);
    uint8_t *dst = (uint8_t *)malloc((size_t)width * (size_t)height);
    if (!src || !dst)
    {
        free(src);
        free(dst);
        return;
    }

    for (int i = 0; i < width * height; ++i)
        src[i] = pixels[i].r; // r=g=b en gris

    // Suma del kernel para normalizar (si no es 0)
    float sumk = 0.f;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            sumk += k[j][i];
    if (sumk == 0.f)
        sumk = 1.f;

    // Procesamos interior (evitamos bordes)
    for (int y = 1; y < height - 1; ++y)
    {
        for (int x = 1; x < width - 1; ++x)
        {
            float acc = 0.f;
            // Ventana 3x3 centrada en (x,y)
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int xx = x + dx;
                    int yy = y + dy;
                    uint8_t pix = src[yy * width + xx];
                    acc += pix * k[dy + 1][dx + 1];
                }
            }
            int val = (int)(acc / sumk + 0.5f);
            dst[y * width + x] = clamp_int_to_u8(val);
        }
    }

    // Bordes: copiamos sin cambios
    for (int x = 0; x < width; ++x)
    {
        dst[0 * width + x] = src[0 * width + x];
        dst[(height - 1) * width + x] = src[(height - 1) * width + x];
    }
    for (int y = 0; y < height; ++y)
    {
        dst[y * width + 0] = src[y * width + 0];
        dst[y * width + (width - 1)] = src[y * width + (width - 1)];
    }

    // Escribimos resultado en los tres canales
    for (int i = 0; i < width * height; ++i)
    {
        pixels[i].r = pixels[i].g = pixels[i].b = dst[i];
    }

    free(src);
    free(dst);
}

int main(void)
{
    char in_name[256];
    printf("Ingrese la ruta del BMP de entrada (24bpp, sin compresion): ");
    if (!fgets(in_name, sizeof(in_name), stdin))
        return 0;
    // Quitar salto de linea
    size_t ln = strlen(in_name);
    if (ln && in_name[ln - 1] == '\n')
        in_name[--ln] = '\0';

    BMPHeader fh;
    BMPInfoHeader ih;
    Pixel24 *img = NULL;

    if (!load_bmp24(in_name, &fh, &ih, &img))
    {
        fprintf(stderr, "Error cargando BMP.\n");
        return 1;
    }
    int W = ih.biWidth, H = ih.biHeight;

    printf("\nMENU\n");
    printf("1) Escala de grises\n");
    printf("2) Convolucion 3x3 (ingresar kernel)\n");
    printf("Seleccione opcion: ");
    int op = 0;
    if (scanf("%d", &op) != 1)
    {
        free(img);
        return 0;
    }

    // Consumir el salto de linea que queda en stdin
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
    {
    }

    if (op == 1)
    {
        to_grayscale(img, W, H);
        char out_name[256];
        printf("Nombre del BMP de salida (ej: salida_gray.bmp): ");
        if (!fgets(out_name, sizeof(out_name), stdin))
        {
            free(img);
            return 0;
        }
        size_t l2 = strlen(out_name);
        if (l2 && out_name[l2 - 1] == '\n')
            out_name[--l2] = '\0';

        if (!save_bmp24(out_name, &ih, img))
        {
            fprintf(stderr, "Error guardando BMP.\n");
        }
        else
        {
            printf("Guardado OK: %s\n", out_name);
        }
    }
    else if (op == 2)
    {

        printf("\nSeleccione un kernel:\n");
        printf("1) Sobel X (bordes verticales)\n");
        printf("2) Sobel Y (bordes horizontales)\n");
        printf("3) Laplaciano (bordes en todas direcciones)\n");
        printf("4) Personalizado (ingresar 9 valores)\n");
        printf("Opcion: ");

        int kernel_op = 0;
        scanf("%d", &kernel_op);

        // Declaramos el kernel 3x3
        float k[3][3];

        switch (kernel_op)
        {
        case 1:
        {
            float sobelX[3][3] = {
                {-1, 0, 1},
                {-2, 0, 2},
                {-1, 0, 1}};
            memcpy(k, sobelX, sizeof(k));
            break;
        }

        case 2:
        {
            float sobelY[3][3] = {
                {-1, -2, -1},
                {0, 0, 0},
                {1, 2, 1}};
            memcpy(k, sobelY, sizeof(k));
            break;
        }

        case 3:
        {
            float laplacian[3][3] = {
                {0, -1, 0},
                {-1, 4, -1},
                {0, -1, 0}};
            memcpy(k, laplacian, sizeof(k));
            break;
        }

        case 4:
            printf("Ingrese los 9 valores del kernel:\n");
            for (int j = 0; j < 3; j++)
                for (int i = 0; i < 3; i++)
                    scanf("%f", &k[j][i]);
            break;

        default:
        {
            float defaultK[3][3] = {
                {-1, 0, 1},
                {-2, 0, 2},
                {-1, 0, 1}};
            memcpy(k, defaultK, sizeof(k));
            break;
        }
        }

        // Consumir fin de linea
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }

        // Aseguramos gris antes de convolucion (mas simple para explicar)
        to_grayscale(img, W, H);
        convolve3x3(img, W, H, k);

        char out_name[256];
        printf("Nombre del BMP de salida (ej: salida_conv.bmp): ");
        if (!fgets(out_name, sizeof(out_name), stdin))
        {
            free(img);
            return 0;
        }
        size_t l2 = strlen(out_name);
        if (l2 && out_name[l2 - 1] == '\n')
            out_name[--l2] = '\0';

        if (!save_bmp24(out_name, &ih, img))
        {
            fprintf(stderr, "Error guardando BMP.\n");
        }
        else
        {
            printf("Guardado OK: %s\n", out_name);
        }
    }
    else
    {
        printf("Opcion no valida.\n");
    }

    free(img);
    return 0;
}
