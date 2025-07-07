#include "image_loaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <webp/decode.h>
#include <libheif/heif.h>
#include <tiffio.h>
#include <jxl/decode.h>
#include <spng.h>
#include <jpeglib.h>

struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

static void my_error_exit(j_common_ptr cinfo) {
	struct my_error_mgr* myerr = (struct my_error_mgr*)cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}

static uint8_t* readFileToBuffer(const char* path, size_t* size) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		// perror("readFileToBuffer: fopen"); // for debug
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long file_size_long = ftell(f);
	if (file_size_long < 0) {
		fclose(f);
		return NULL;
	}
	*size = (size_t)file_size_long;
	fseek(f, 0, SEEK_SET);
	uint8_t* buffer = (uint8_t*)malloc(*size);
	if (!buffer) {
		fclose(f);
		return NULL;
	}
	if (fread(buffer, 1, *size, f) != *size) {
		free(buffer);
		fclose(f);
		return NULL;
	}
	fclose(f);
	return buffer;
}

unsigned char* loadImage_WebP(const char* path, int* width, int* height) {
	size_t data_size = 0;
	uint8_t* file_data = readFileToBuffer(path, &data_size);
	if (!file_data) {
		return NULL;
	}
	if (!WebPGetInfo(file_data, data_size, width, height)) {
		free(file_data);
		return NULL;
	}
	size_t image_size = (size_t)(*width) * (size_t)(*height) * 4;
	uint8_t* output_buffer = (uint8_t*)malloc(image_size);
	if (!output_buffer) {
		free(file_data);
		return NULL;
	}
	if (!WebPDecodeRGBAInto(file_data, data_size, output_buffer, image_size, (*width) * 4)) {
		free(output_buffer);
		output_buffer = NULL; // if error
	}
	free(file_data);
	return output_buffer;
}

unsigned char* loadImage_HeifAvif(const char* path, int* width, int* height) {
	struct heif_context* ctx = heif_context_alloc();
	if (!ctx) return NULL;
	struct heif_image_handle* handle = NULL;
	struct heif_image* img = NULL;
	uint8_t* output_buffer = NULL;
	struct heif_error err;

	err = heif_context_read_from_file(ctx, path, NULL);
	if (err.code) goto cleanup;

	err = heif_context_get_primary_image_handle(ctx, &handle);
	if (err.code) goto cleanup;

	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code) goto cleanup;

	*width = heif_image_get_width(img, heif_channel_interleaved);
	*height = heif_image_get_height(img, heif_channel_interleaved);
	int stride;
	const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
	if (!data) goto cleanup;

	size_t tight_size = (size_t)(*width) * (size_t)(*height) * 4;
	output_buffer = (uint8_t*)malloc(tight_size);
	if (!output_buffer) goto cleanup;

	// If the stride is equal to the width in bytes,
	// all data can be copied with a single memcpy call
	if (stride == (*width) * 4) {
		memcpy(output_buffer, data, tight_size);
	} else {
		// Otherwise, copy line by line, as before
		const uint8_t* src_ptr = data;
		uint8_t* dest_ptr = output_buffer;
		for (int y = 0; y < *height; ++y) {
			memcpy(dest_ptr, src_ptr, (size_t)(*width) * 4);
			src_ptr += stride;
			dest_ptr += (*width) * 4;
		}
	}

cleanup:
	if (img) heif_image_release(img);
	if (handle) heif_image_handle_release(handle);
	if (ctx) heif_context_free(ctx);

	return output_buffer;
}

unsigned char* loadImage_Tiff(const char* path, int* width, int* height) {
	TIFF* tif = TIFFOpen(path, "r");
	if (!tif) return NULL;

	uint32_t* raster = NULL;
	uint8_t* output_buffer = NULL;

	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, height);

	uint32_t npixels = (uint32_t)(*width) * (uint32_t)(*height);

	raster = (uint32_t*)_TIFFmalloc(npixels * sizeof(uint32_t));
	if (!raster) goto cleanup;

	if (!TIFFReadRGBAImage(tif, *width, *height, raster, 0)) {
		goto cleanup;
	}

	size_t image_size = (size_t)npixels * 4;
	output_buffer = (uint8_t*)malloc(image_size);
	if (!output_buffer) goto cleanup;
	const size_t row_bytes = (size_t)(*width) * 4;
	uint8_t* src_row = (uint8_t*)raster + image_size - row_bytes;
	uint8_t* dest_row = output_buffer;

	for (int y = 0; y < *height; ++y) {
		memcpy(dest_row, src_row, row_bytes);
		dest_row += row_bytes;
		src_row -= row_bytes;
	}

cleanup:
	if (raster) _TIFFfree(raster);
	TIFFClose(tif);
	return output_buffer;
}

unsigned char* loadImage_Jxl(const char* path, int* width, int* height) {
	size_t file_size = 0;
	uint8_t* file_data = readFileToBuffer(path, &file_size);
	if (!file_data) return NULL;

	JxlDecoder* dec = JxlDecoderCreate(NULL);
	if (!dec) {
		free(file_data);
		return NULL;
	}
	
	uint8_t* output_buffer = NULL;
	
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		goto cleanup;
	}
	
	JxlDecoderSetInput(dec, file_data, file_size);
	JxlDecoderCloseInput(dec);

	for (;;) {
		JxlDecoderStatus status = JxlDecoderProcessInput(dec);
		switch (status) {
		case JXL_DEC_ERROR:
			free(output_buffer);
			output_buffer = NULL;
			goto cleanup;
		case JXL_DEC_SUCCESS:
			goto cleanup;
		case JXL_DEC_BASIC_INFO: {
			JxlBasicInfo info;
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				goto cleanup;
			}
			*width = info.xsize;
			*height = info.ysize;
			break;
		}
		case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
			size_t buffer_size;
			JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				goto cleanup;
			}
			output_buffer = (uint8_t*)malloc(buffer_size);
			if (!output_buffer) {
				goto cleanup;
			}
			if (JxlDecoderSetImageOutBuffer(dec, &format, output_buffer, buffer_size) != JXL_DEC_SUCCESS) {
				free(output_buffer);
				output_buffer = NULL;
				goto cleanup;
			}
			break;
		}
		case JXL_DEC_FULL_IMAGE:
			break;
		default:
			goto cleanup;
		}
	}

cleanup:
	JxlDecoderDestroy(dec);
	free(file_data);
	return output_buffer;
}

unsigned char* loadImage_SPNG(const char* path, int* width, int* height) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	spng_ctx* ctx = spng_ctx_new(0);
	uint8_t* output_buffer = NULL;

	if (!ctx) {
		fclose(f);
		return NULL;
	}
	
	spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
	spng_set_png_file(ctx, f);

	struct spng_ihdr ihdr;
	if (spng_get_ihdr(ctx, &ihdr)) goto cleanup;

	size_t image_size;
	if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &image_size)) goto cleanup;
	
	output_buffer = (uint8_t*)malloc(image_size);
	if (!output_buffer) goto cleanup;

	if (spng_decode_image(ctx, output_buffer, image_size, SPNG_FMT_RGBA8, 0)) {
		free(output_buffer);
		output_buffer = NULL;
	} else {
		*width = ihdr.width;
		*height = ihdr.height;
	}

cleanup:
	spng_ctx_free(ctx);
	fclose(f);
	return output_buffer;
}

unsigned char* loadImage_JpegTurbo(const char* path, int* width, int* height) {
	size_t data_size = 0;
	uint8_t* file_data = readFileToBuffer(path, &data_size);
	if (!file_data) return NULL;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	uint8_t* output_buffer = NULL;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		free(file_data);
		free(output_buffer); 
		return NULL;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, file_data, data_size);
	jpeg_read_header(&cinfo, TRUE);
	cinfo.out_color_space = JCS_EXT_RGBA;
	jpeg_start_decompress(&cinfo);
	*width = cinfo.output_width;
	*height = cinfo.output_height;
	int row_stride = (*width) * cinfo.output_components;
	size_t image_size = (size_t)(*height) * row_stride;
	output_buffer = (uint8_t*)malloc(image_size);
	
	if (!output_buffer) {
		longjmp(jerr.setjmp_buffer, 1);
	}
	
	while (cinfo.output_scanline < cinfo.output_height) {
		JSAMPROW row_pointer = &output_buffer[cinfo.output_scanline * row_stride];
		jpeg_read_scanlines(&cinfo, &row_pointer, 1);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	free(file_data);

	return output_buffer;
}
