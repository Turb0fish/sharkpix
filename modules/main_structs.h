#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "glad.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

typedef enum { IMAGE_STATE_UNLOADED, IMAGE_STATE_LOADING, IMAGE_STATE_LOADED, IMAGE_STATE_FAILED } ImageState;
typedef struct { char path_utf8[4096]; int32_t full_width, full_height; uint32_t fileSizeKB; ImageState state; GLuint textureID; } ImageMetadata;
typedef struct { ImageMetadata* items; size_t size, capacity; } ImageList;
typedef struct { int index; unsigned char* data; int width, height; bool success; } LoadResult;
typedef struct LoadResultNode { LoadResult value; struct LoadResultNode* next; } LoadResultNode;
typedef struct { LoadResultNode* head; LoadResultNode* tail; SDL_Mutex* mutex; } LoadResultQueue;
typedef struct { SDL_Window* window; SDL_GLContext glContext; int windowWidth, windowHeight; bool isFullscreen; GLuint shaderProgram, vao, vbo, ebo; GLint modelLoc, projLoc; float zoom, offsetX, offsetY; float projectionMatrix[16], modelMatrix[16]; bool modelDirty, projectionDirty; ImageList images; int currentIndex, activeTextureIndex; bool isDragging; SDL_Thread* loader_thread; LoadResultQueue loader_results; SDL_Condition* loader_cv; SDL_Mutex* loader_mutex; atomic_bool loader_running; atomic_int loader_nextImageToLoad; } AppState;

extern AppState g_appState;
