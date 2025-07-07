#pragma once
#include <stdbool.h>
#include "glad.h"
#include "main_structs.h"

void ImageMetadata_setFileSize(ImageMetadata* meta, uint64_t bytes);
float ImageMetadata_getFileSizeMB(const ImageMetadata* meta);
void updateProjectionMatrix(void);
void updateModelMatrix(void);
GLuint compileShader(GLenum type, const char* source);
void resetView(bool fitToWindow);
void renderFrame(void);
void updateWindowTitle(void);
void loader_request_load(int index);
void setCurrentImage(int newIndex);
void handleEvents(void);
