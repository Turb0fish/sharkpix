gcc -Imodules main.c modules/glad.c modules/image_loaders.c modules/render.c -o SharkPix -std=c11 -Wall -g \
    -lSDL3 -lGL \
    -lwebp -lheif -ltiff -ljpeg -ljxl -lspng \
    -lpthread -lm -latomic
