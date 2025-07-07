gcc -Imodules main.c modules/glad.c modules/image_loaders.c modules/render.c -o SharkPix -std=c11 \
	-lSDL3 -lSDL3_image -lGL \
	-lwebp -lheif -ltiff -ljpeg -ljxl -lspng \
	-lpthread -lm -latomic
