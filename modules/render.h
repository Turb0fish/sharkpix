#pragma once

#include "glad.h"
#include <stdbool.h>

void updateProjectionMatrix(void);
void updateModelMatrix(void);
void resetView(bool fitToWindow);
void renderFrame(void);
