#include "render.h"
#include "main_structs.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>

extern AppState g_appState;

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

void renderFrame() {
	glClear(GL_COLOR_BUFFER_BIT);
	if (g_appState.activeTextureIndex < 0) return;
	ImageMetadata* img = &g_appState.images.items[g_appState.activeTextureIndex];
	if (img->state != IMAGE_STATE_LOADED || img->textureID == 0) return;
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
