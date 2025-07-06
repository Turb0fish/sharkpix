#pragma once

#include <stdint.h>
#include <stdbool.h>

unsigned char* loadImage_WebP(const char* path, int* width, int* height);
unsigned char* loadImage_HeifAvif(const char* path, int* width, int* height);
unsigned char* loadImage_Tiff(const char* path, int* width, int* height);
unsigned char* loadImage_Jxl(const char* path, int* width, int* height);
unsigned char* loadImage_SPNG(const char* path, int* width, int* height);
unsigned char* loadImage_JpegTurbo(const char* path, int* width, int* height);
