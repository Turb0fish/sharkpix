#include "render.h"
#include "main_structs.h"

#define MAX_PATH_DISPLAY 512
#define STR(x) #x
#define XSTR(x) STR(x)

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

extern AppState g_appState;

void ImageMetadata_setFileSize(ImageMetadata* meta, uint64_t bytes) {
	meta->fileSizeKB = (uint32_t)(bytes / 1024);
}

float ImageMetadata_getFileSizeMB(const ImageMetadata* meta) {
	return meta->fileSizeKB / 1024.0f;
}

void updateProjectionMatrix() {
	memset(g_appState.projectionMatrix, 0, sizeof(g_appState.projectionMatrix));
	g_appState.projectionMatrix[0] = 2.0f / g_appState.windowWidth;
	g_appState.projectionMatrix[5] = -2.0f / g_appState.windowHeight;
	g_appState.projectionMatrix[10] = -1.0f;
	g_appState.projectionMatrix[12] = -1.0f;
	g_appState.projectionMatrix[13] = 1.0f;
	g_appState.projectionMatrix[15] = 1.0f;
	g_appState.projectionDirty = false;
}

void updateModelMatrix() {
	if (g_appState.activeTextureIndex < 0) return;
	ImageMetadata* img = &g_appState.images.items[g_appState.activeTextureIndex];
	float scaleX = img->full_width * g_appState.zoom;
	float scaleY = img->full_height * g_appState.zoom;
	memset(g_appState.modelMatrix, 0, sizeof(g_appState.modelMatrix));
	g_appState.modelMatrix[0] = scaleX;
	g_appState.modelMatrix[5] = scaleY;
	g_appState.modelMatrix[10] = 1.0f;
	g_appState.modelMatrix[15] = 1.0f;
	g_appState.modelMatrix[12] = g_appState.offsetX;
	g_appState.modelMatrix[13] = g_appState.offsetY;
	g_appState.modelDirty = false;
}

void resetView(bool fitToWindow) {
	if (g_appState.currentIndex < 0) return;
	ImageMetadata* img = &g_appState.images.items[g_appState.currentIndex];
	if (img->full_width == 0 || img->full_height == 0) return;
	SDL_GetWindowSize(g_appState.window, &g_appState.windowWidth, &g_appState.windowHeight);
	if (fitToWindow) {
		float scaleX = (float)g_appState.windowWidth / img->full_width;
		float scaleY = (float)g_appState.windowHeight / img->full_height;
		g_appState.zoom = (scaleX < scaleY) ? scaleX : scaleY;
	} else {
		g_appState.zoom = 1.0f;
	}
	g_appState.offsetX = (g_appState.windowWidth - img->full_width * g_appState.zoom) * 0.5f;
	g_appState.offsetY = (g_appState.windowHeight - img->full_height * g_appState.zoom) * 0.5f;
	g_appState.modelDirty = true;
}

void updateGifAnimation(ImageMetadata* img) {
	if (!img->gif_animation || img->gif_animation->count <= 1) return;
	Uint32 now = SDL_GetTicks();
	if (now >= img->gif_next_frame_time) {
		img->gif_current_frame = (img->gif_current_frame + 1) % img->gif_animation->count;
		img->gif_next_frame_time = now + img->gif_animation->delays[img->gif_current_frame];
		glBindTexture(GL_TEXTURE_2D, img->textureID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 
                       img->full_width, img->full_height,
                       GL_RGBA, GL_UNSIGNED_BYTE, 
                       img->gif_animation->frames[img->gif_current_frame]->pixels);
	}
}

void renderFrame() {
	glClear(GL_COLOR_BUFFER_BIT);
	
	if (g_appState.activeTextureIndex < 0) return;
	
	ImageMetadata* img = &g_appState.images.items[g_appState.activeTextureIndex];
	if (img->state != IMAGE_STATE_LOADED || img->textureID == 0) return;
	if (img->gif_animation) {
		updateGifAnimation(img);
	}
	glUseProgram(g_appState.shaderProgram);
	if (g_appState.projectionDirty) {
		updateProjectionMatrix();
		glUniformMatrix4fv(g_appState.projLoc, 1, GL_FALSE, g_appState.projectionMatrix);
	}
	if (g_appState.modelDirty) {
		updateModelMatrix();
		glUniformMatrix4fv(g_appState.modelLoc, 1, GL_FALSE, g_appState.modelMatrix);
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img->textureID);
	glBindVertexArray(g_appState.vao);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void updateWindowTitle(void) {
	if (g_appState.currentIndex < 0) {
		SDL_SetWindowTitle(g_appState.window, "SharkPix");
		return;
	}
	ImageMetadata* img = &g_appState.images.items[g_appState.currentIndex];
	char title[1024];
	switch(img->state) {
		case IMAGE_STATE_LOADED:
			snprintf(title, sizeof(title),
				"[%d/%zu] %." XSTR(MAX_PATH_DISPLAY) "s | %dx%d | %.2f MB",
				g_appState.currentIndex + 1, g_appState.images.size,
				img->path_utf8, img->full_width, img->full_height,
				ImageMetadata_getFileSizeMB(img));
			break;
		case IMAGE_STATE_LOADING:
			snprintf(title, sizeof(title),
				"[%d/%zu] %." XSTR(MAX_PATH_DISPLAY) "s | Loading...",
				g_appState.currentIndex + 1, g_appState.images.size,
				img->path_utf8);
			break;
		default:
			snprintf(title, sizeof(title),
				"[%d/%zu] %." XSTR(MAX_PATH_DISPLAY) "s | Failed or Unloaded",
				g_appState.currentIndex + 1, g_appState.images.size,
				img->path_utf8);
	}
	SDL_SetWindowTitle(g_appState.window, title);
}

void loader_request_load(int index) {
	SDL_LockMutex(g_appState.loader_mutex);
	atomic_store(&g_appState.loader_nextImageToLoad, index);
	SDL_SignalCondition(g_appState.loader_cv);
	SDL_UnlockMutex(g_appState.loader_mutex);
}

void setCurrentImage(int newIndex) {
	if (g_appState.images.size == 0) return;
	if (newIndex >= (int)g_appState.images.size) newIndex = 0;
	else if (newIndex < 0) newIndex = (int)g_appState.images.size - 1;
	if (g_appState.currentIndex == newIndex) return;
	g_appState.currentIndex = newIndex;
	ImageMetadata* img = &g_appState.images.items[newIndex];
	if (img->state != IMAGE_STATE_LOADED) {
		img->state = IMAGE_STATE_LOADING;
		loader_request_load(newIndex);
	}
	updateWindowTitle();
}

GLuint compileShader(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char infoLog[512];
		glGetShaderInfoLog(shader, 512, NULL, infoLog);
		SDL_Log("Shader Compilation Failed:\n%s", infoLog);
	}
	return shader;
}

//controls
void handleEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				atomic_store(&g_appState.loader_running, false);
				break;
			case SDL_EVENT_WINDOW_RESIZED:
				SDL_GetWindowSize(g_appState.window, &g_appState.windowWidth, &g_appState.windowHeight);
				glViewport(0, 0, g_appState.windowWidth, g_appState.windowHeight);
				g_appState.projectionDirty = true;
				if (g_appState.currentIndex != -1) resetView(true);
				break;
			case SDL_EVENT_KEY_DOWN:
				switch (event.key.key) {
					case SDLK_ESCAPE:
						atomic_store(&g_appState.loader_running, false);
						break;
					case SDLK_RIGHT: case SDLK_KP_6:
						setCurrentImage(g_appState.currentIndex + 1);
						break;
					case SDLK_LEFT: case SDLK_KP_4:
						setCurrentImage(g_appState.currentIndex - 1);
						break;
					case SDLK_R:
						if (g_appState.currentIndex != -1) resetView(true);
						break;
					case SDLK_F:
						g_appState.isFullscreen = !g_appState.isFullscreen;
						SDL_SetWindowFullscreen(g_appState.window, g_appState.isFullscreen);
						break;
				}
					break;
				//zoom
				case SDL_EVENT_MOUSE_WHEEL: {
				const bool* state = SDL_GetKeyboardState(NULL);
				if (state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL]) {
					float mouseX, mouseY;
					SDL_GetMouseState(&mouseX, &mouseY);
					float oldZoom = g_appState.zoom;
					float zoomFactor = (event.wheel.y > 0) ? 1.2f : (1.0f / 1.2f);
					g_appState.zoom = (float)MAX(0.01, MIN(g_appState.zoom * zoomFactor, 100.0));
					g_appState.offsetX = mouseX - (mouseX - g_appState.offsetX) * (g_appState.zoom / oldZoom);
					g_appState.offsetY = mouseY - (mouseY - g_appState.offsetY) * (g_appState.zoom / oldZoom);
					g_appState.modelDirty = true;
					updateWindowTitle();
				} else {
					setCurrentImage(g_appState.currentIndex - (int)event.wheel.y);
				}
					break;
			}
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				if (event.button.button == SDL_BUTTON_LEFT) g_appState.isDragging = true;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				if (event.button.button == SDL_BUTTON_LEFT) g_appState.isDragging = false;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				if (g_appState.isDragging) {
					g_appState.offsetX += event.motion.xrel;
					g_appState.offsetY += event.motion.yrel;
					g_appState.modelDirty = true;
			}
		break;
		}
	}
}
