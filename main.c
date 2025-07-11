#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

#include <stb/stb_image.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "modules/glad.h"
#include "modules/main_structs.h"
#include "modules/image_loaders.h"
#include "modules/render.h"

AppState g_appState;

void ImageList_init(ImageList* list) {
	list->items = NULL; list->size = 0; list->capacity = 0;
}

void ImageList_add(ImageList* list, ImageMetadata item) {
	if (list->size >= list->capacity) {
		size_t n = list->capacity == 0 ? 8 : list->capacity * 2;
		ImageMetadata* i = (ImageMetadata*)realloc(list->items, n * sizeof(ImageMetadata));
		if (!i) { SDL_Log("Realloc fail"); return; }
		list->items = i; list->capacity = n;
	}
	list->items[list->size++] = item;
}
void ImageList_free(ImageList* list) {
	free(list->items); ImageList_init(list);
}

void LoadResultQueue_init(LoadResultQueue* queue) {
	queue->head = NULL; queue->tail = NULL; queue->mutex = SDL_CreateMutex();
}

void LoadResultQueue_free(LoadResultQueue* queue) {
	LoadResultNode* c = queue->head;
	while (c != NULL) {
		LoadResultNode* n = c->next; free(c); c = n;
	}
	SDL_DestroyMutex(queue->mutex);
}

void LoadResultQueue_enqueue(LoadResultQueue* queue, LoadResult result) {
	LoadResultNode* n = (LoadResultNode*)malloc(sizeof(LoadResultNode));
	if (!n) return;
	n->value = result; n->next = NULL;
	SDL_LockMutex(queue->mutex);
	if (queue->tail == NULL) queue->head = queue->tail = n;
	else {
		queue->tail->next = n; queue->tail = n;
	}
	SDL_UnlockMutex(queue->mutex);
}

bool LoadResultQueue_dequeue(LoadResultQueue* queue, LoadResult* result) {
	SDL_LockMutex(queue->mutex);
	if (queue->head == NULL) {
		SDL_UnlockMutex(queue->mutex); return false;
	}
	LoadResultNode* t = queue->head;
	*result = t->value;
	queue->head = queue->head->next;
	if (queue->head == NULL) queue->tail = NULL;
	free(t);
	SDL_UnlockMutex(queue->mutex);
	return true;
}

int loader_thread_func(void* data);

int compareImages(const void* a, const void* b) {
	const ImageMetadata* metaA = (const ImageMetadata*)a;
	const ImageMetadata* metaB = (const ImageMetadata*)b;
	return strverscmp(metaA->path_utf8, metaB->path_utf8);
}



void unloadTexture(ImageMetadata* img) {
	if (img->textureID != 0) {
		glDeleteTextures(1, &img->textureID);
		img->textureID = 0;
	}
	if (img->gif_animation) {
		IMG_FreeAnimation(img->gif_animation);
		img->gif_animation = NULL;
	}
	if (img->state == IMAGE_STATE_LOADED) {
		img->state = IMAGE_STATE_UNLOADED;
	}
	img->full_width = 0;
	img->full_height = 0;
}

void unloadAllTexturesExcept(int exceptionIndex) {
	for (size_t i = 0; i < g_appState.images.size; ++i) {
	if ((int)i == exceptionIndex) continue;
		unloadTexture(&g_appState.images.items[i]);
	}
}

void loader_start() {
	atomic_store(&g_appState.loader_running, true);
	atomic_store(&g_appState.loader_nextImageToLoad, -1);
	g_appState.loader_mutex = SDL_CreateMutex();
	g_appState.loader_cv = SDL_CreateCondition();
	LoadResultQueue_init(&g_appState.loader_results);
	g_appState.loader_thread = SDL_CreateThread(loader_thread_func, "ImageLoader", NULL);
}

void loader_stop() {
	if (atomic_exchange(&g_appState.loader_running, false)) {
		SDL_LockMutex(g_appState.loader_mutex);
		SDL_SignalCondition(g_appState.loader_cv);
		SDL_UnlockMutex(g_appState.loader_mutex);
		SDL_WaitThread(g_appState.loader_thread, NULL);
		SDL_DestroyMutex(g_appState.loader_mutex);
		SDL_DestroyCondition(g_appState.loader_cv);
		LoadResultQueue_free(&g_appState.loader_results);
	}
}

typedef unsigned char* (*ImageLoader)(const char*, int*, int*);

static unsigned char* stbi_load_simple(const char* path, int* width, int* height) {
	int channels;
	return stbi_load(path, width, height, &channels, 4); //all return 4 channels
}

int loader_thread_func(void* data) {
	(void)data;
	while (atomic_load(&g_appState.loader_running)) {
		SDL_LockMutex(g_appState.loader_mutex);
		while (atomic_load(&g_appState.loader_nextImageToLoad) == -1 && 
               atomic_load(&g_appState.loader_running)) {
			SDL_WaitCondition(g_appState.loader_cv, g_appState.loader_mutex);
		}
		if (!atomic_load(&g_appState.loader_running)) {
			SDL_UnlockMutex(g_appState.loader_mutex);
			break;
		}
		int indexToLoad = atomic_exchange(&g_appState.loader_nextImageToLoad, -1);
		SDL_UnlockMutex(g_appState.loader_mutex);
		if (indexToLoad == -1) continue;

		ImageMetadata* meta = &g_appState.images.items[indexToLoad];
		LoadResult result = {0};
		
		struct stat fileStat;
		if (stat(meta->path_utf8, &fileStat) == 0) {
			ImageMetadata_setFileSize(meta, fileStat.st_size);
		}

		const char* ext = strrchr(meta->path_utf8, '.');
		if (ext && strcasecmp(ext, ".gif") == 0) {
			meta->gif_animation = IMG_LoadAnimation(meta->path_utf8);
			if (meta->gif_animation) {
				for (int i = 0; i < meta->gif_animation->count; i++) {
					SDL_Surface* originalFrame = meta->gif_animation->frames[i];
					SDL_Surface* convertedFrame = SDL_ConvertSurface(originalFrame, SDL_PIXELFORMAT_ABGR8888);
					if (convertedFrame) {
						SDL_DestroySurface(originalFrame);
						meta->gif_animation->frames[i] = convertedFrame;
					}
				}
				
				meta->gif_current_frame = 0;
				meta->gif_next_frame_time = SDL_GetTicks() + meta->gif_animation->delays[0];
				result.width = meta->gif_animation->w;
				result.height = meta->gif_animation->h;
				SDL_Surface* firstFrame = meta->gif_animation->frames[0];
				size_t dataSize = firstFrame->w * firstFrame->h * 4; // RGBA
				result.data = (unsigned char*)malloc(dataSize);
				if (result.data) {
					SDL_LockSurface(firstFrame);
					memcpy(result.data, firstFrame->pixels, dataSize);
					SDL_UnlockSurface(firstFrame);
					result.success = true;
				} else {
					IMG_FreeAnimation(meta->gif_animation);
					meta->gif_animation = NULL;
					result.success = false;
				}
				
				result.index = indexToLoad;
				LoadResultQueue_enqueue(&g_appState.loader_results, result);
				continue;
			}
		}
		ImageLoader loader = stbi_load_simple;
		if (ext) {
			static const struct {
				const char* ext;
				ImageLoader loader;
			} loaders[] = {
				{".png",  loadImage_SPNG},
				{".jpg",  loadImage_JpegTurbo},
				{".jpeg", loadImage_JpegTurbo},
				{".webp", loadImage_WebP},
				{".heif", loadImage_HeifAvif},
				{".heic", loadImage_HeifAvif},
				{".avif", loadImage_HeifAvif},
				{".tiff", loadImage_Tiff},
				{".tif",  loadImage_Tiff},
				{".jxl",  loadImage_Jxl}
			};
			for (size_t i = 0; i < sizeof(loaders)/sizeof(loaders[0]); ++i) {
				if (strcasecmp(ext, loaders[i].ext) == 0) {
					loader = loaders[i].loader;
					break;
				}
			}
		}

		int width = 0, height = 0;
		unsigned char* img_data = loader(meta->path_utf8, &width, &height);
		result = (LoadResult){
			.index = indexToLoad,
			.data = img_data,
			.width = width,
			.height = height,
			.success = (img_data != NULL)
		};
		LoadResultQueue_enqueue(&g_appState.loader_results, result);
	}
	return 0;
}

void processLoaderResults() {
	LoadResult result;
	while(LoadResultQueue_dequeue(&g_appState.loader_results, &result)) {
		ImageMetadata* img = &g_appState.images.items[result.index];
		if (result.index != g_appState.currentIndex) {
			if (result.success && result.data) {
				free(result.data);
			}
			if (img->state == IMAGE_STATE_LOADING) img->state = IMAGE_STATE_UNLOADED;
			continue;
		}
		if (result.success) {
			img->full_width = result.width;
			img->full_height = result.height;
			glGenTextures(1, &img->textureID);
			glBindTexture(GL_TEXTURE_2D, img->textureID);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			
			if (img->gif_animation) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			} else {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			}
           
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, result.data);
			glGenerateMipmap(GL_TEXTURE_2D);
			free(result.data);
			img->state = IMAGE_STATE_LOADED;
			g_appState.activeTextureIndex = result.index;
			unloadAllTexturesExcept(g_appState.activeTextureIndex);
			updateWindowTitle();
			resetView(true);
		} else {
			img->state = IMAGE_STATE_FAILED;
			if (g_appState.activeTextureIndex == result.index) {
				g_appState.activeTextureIndex = -1;
			}
			updateWindowTitle();
		}
	}
}

void findImagesInDirectory() {
	const char* extensions[] = { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp", ".heif", ".heic", ".avif", ".tiff", ".tif", ".jxl" };
	size_t num_extensions = sizeof(extensions) / sizeof(extensions[0]);
	DIR* d;
	struct dirent* dir;
	d = opendir(".");
	if (d) {
		while ((dir = readdir(d)) != NULL) {
			if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
			struct stat st;
			if (stat(dir->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
				const char* ext = strrchr(dir->d_name, '.');
				if (ext) {
					for (size_t i = 0; i < num_extensions; ++i) {
						if (strcasecmp(ext, extensions[i]) == 0) {
							ImageMetadata meta = {0};
							strncpy(meta.path_utf8, dir->d_name, PATH_MAX - 1);
							ImageList_add(&g_appState.images, meta);
							break;
						}
					}
				}
			}
		}
		closedir(d);
	}
	qsort(g_appState.images.items, g_appState.images.size, sizeof(ImageMetadata), compareImages);
}

void init_app_state() {
	memset(&g_appState, 0, sizeof(AppState));
	g_appState.windowWidth = 1280;
	g_appState.windowHeight = 720;
	g_appState.zoom = 1.0f;
	g_appState.modelDirty = true;
	g_appState.projectionDirty = true;
	g_appState.currentIndex = -1;
	g_appState.activeTextureIndex = -1;
	ImageList_init(&g_appState.images);
}

const char* vertexShaderSource =
	"#version 330 core\n"
	"layout(location=0) in vec2 aPos;\n"
	"layout(location=1) in vec2 aTexCoord;\n"
	"out vec2 TexCoord;\n"
	"uniform mat4 model;\n"
	"uniform mat4 projection;\n"
	"void main() {\n"
	"gl_Position = projection * model * vec4(aPos, 0.0, 1.0);\n"
	"TexCoord = aTexCoord;\n"
	"}\n";

const char* fragmentShaderSource =
	"#version 330 core\n"
	"out vec4 FragColor;\n"
	"in vec2 TexCoord;\n"
	"uniform sampler2D ourTexture;\n"
	"void main() {\n"
	"FragColor = texture(ourTexture, TexCoord);\n"
	"}\n";

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	init_app_state();
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	g_appState.window = SDL_CreateWindow("SharkPix", g_appState.windowWidth, g_appState.windowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!g_appState.window) return -1;
	g_appState.glContext = SDL_GL_CreateContext(g_appState.window);
	if (!g_appState.glContext) return -1;
	SDL_GL_SetSwapInterval(1);
	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return -1;
	glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
	GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
	g_appState.shaderProgram = glCreateProgram();
	glAttachShader(g_appState.shaderProgram, vs);
	glAttachShader(g_appState.shaderProgram, fs);
	glLinkProgram(g_appState.shaderProgram);
	glDeleteShader(vs);
	glDeleteShader(fs);
	g_appState.modelLoc = glGetUniformLocation(g_appState.shaderProgram, "model");
	g_appState.projLoc = glGetUniformLocation(g_appState.shaderProgram, "projection");
	glUseProgram(g_appState.shaderProgram);
	glUniform1i(glGetUniformLocation(g_appState.shaderProgram, "ourTexture"), 0);
	float vertices[] = {
		1.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f
	};
	unsigned int indices[] = { 0, 1, 2, 0, 2, 3 };
	glGenVertexArrays(1, &g_appState.vao);
	glGenBuffers(1, &g_appState.vbo);
	glGenBuffers(1, &g_appState.ebo);
	glBindVertexArray(g_appState.vao);
	glBindBuffer(GL_ARRAY_BUFFER, g_appState.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_appState.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);

	loader_start();
	findImagesInDirectory();
	if (g_appState.images.size > 0) setCurrentImage(0);
	updateProjectionMatrix();
	glUniformMatrix4fv(g_appState.projLoc, 1, GL_FALSE, g_appState.projectionMatrix);
	while (atomic_load(&g_appState.loader_running)) {
		handleEvents();
		processLoaderResults();
		renderFrame();
		SDL_GL_SwapWindow(g_appState.window);
	}
	loader_stop();
	
	for (size_t i = 0; i < g_appState.images.size; ++i) {
		if (g_appState.images.items[i].textureID != 0) {
			glDeleteTextures(1, &g_appState.images.items[i].textureID);
		}
		if (g_appState.images.items[i].gif_animation) {
			IMG_FreeAnimation(g_appState.images.items[i].gif_animation);
		}
	}
	ImageList_free(&g_appState.images);
	glDeleteVertexArrays(1, &g_appState.vao);
	glDeleteBuffers(1, &g_appState.vbo);
	glDeleteBuffers(1, &g_appState.ebo);
	glDeleteProgram(g_appState.shaderProgram);
	SDL_GL_DestroyContext(g_appState.glContext);
	SDL_DestroyWindow(g_appState.window);
	SDL_Quit();
	return 0;
}
