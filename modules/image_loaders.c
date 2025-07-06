#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "image_loaders.h"

#include <webp/decode.h>
#include <libheif/heif.h>
#include <tiffio.h>
#include <jxl/decode.h>
#include <spng.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

static void my_error_exit(j_common_ptr cinfo) {
	struct my_error_mgr* myerr = (struct my_error_mgr*)cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}

unsigned char* loadImage_WebP(const char* path, int* width, int* height) {
	size_t data_size = 0; 
	unsigned char* file_data = NULL; 
	unsigned char* rgba_data = NULL; 
	unsigned char* output_buffer = NULL; 
	
	FILE* f = fopen(path, "rb"); 
	if (!f) return NULL;
	
	fseek(f, 0, SEEK_END);
	data_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	file_data = (unsigned char*)malloc(data_size);
	if (!file_data || fread(file_data, 1, data_size, f) != data_size) { 
		if (file_data) free(file_data); fclose(f); return NULL; 
	} 
	fclose(f);
	
	rgba_data = WebPDecodeRGBA(file_data, data_size, width, height);
	free(file_data);
	if (rgba_data) {
		size_t image_size = (*width) * (*height) * 4;
		output_buffer = (unsigned char*)malloc(image_size);
		if (output_buffer) memcpy(output_buffer, rgba_data, image_size);
		WebPFree(rgba_data);
	} 	
	return output_buffer;
}

unsigned char* loadImage_HeifAvif(const char* path, int* width, int* height) {
	struct heif_context* ctx = heif_context_alloc();
	if (!ctx) return NULL;
	struct heif_error err = heif_context_read_from_file(ctx, path, NULL);
	if (err.code) { 
		heif_context_free(ctx); return NULL;
	}
	struct heif_image_handle* handle;
	err = heif_context_get_primary_image_handle(ctx, &handle);
	if (err.code) {
		heif_context_free(ctx); return NULL;
	}
	struct heif_image* img;
	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code) {
		heif_image_handle_release(handle); heif_context_free(ctx); return NULL;
	} 
	*width = heif_image_get_width(img, heif_channel_interleaved);
	*height = heif_image_get_height(img, heif_channel_interleaved);
	int stride;
	const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
	unsigned char* output_buffer = NULL;
	if (data) {
		size_t tight_size = (*width) * (*height) * 4;
		output_buffer = (unsigned char*)malloc(tight_size);
		if (output_buffer) {
			const uint8_t* src_ptr = data;uint8_t* dest_ptr = output_buffer;
			for (int y = 0; y < *height; ++y) {
				memcpy(dest_ptr, src_ptr, (*width) * 4);
				src_ptr += stride; dest_ptr += (*width) * 4; 
			}
		}
	}
	heif_image_release(img);
	heif_image_handle_release(handle);
	heif_context_free(ctx);
	return output_buffer;
}

unsigned char* loadImage_Tiff(const char* path, int* width, int* height) {
	TIFF* tif = TIFFOpen(path, "r");
	if (!tif) return NULL;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, height);
	uint32_t npixels = (*width) * (*height);
	uint32_t* raster = (uint32_t*)_TIFFmalloc(npixels * sizeof(uint32_t));
	if (!raster) {
		TIFFClose(tif); return NULL;
	}
	unsigned char* output_buffer = NULL;
	if (TIFFReadRGBAImage(tif, *width, *height, raster, 0)) {
		output_buffer = (unsigned char*)malloc(npixels * 4);
		if (output_buffer) {
			uint8_t* out_ptr = output_buffer;
			for (uint32_t i = 0; i < npixels; i++) {
				uint32_t p = raster[i];
				*out_ptr++ = (uint8_t)((p >> 0) & 0xFF);
				*out_ptr++ = (uint8_t)((p >> 8) & 0xFF);
				*out_ptr++ = (uint8_t)((p >> 16) & 0xFF);
				*out_ptr++ = (uint8_t)((p >> 24) & 0xFF);
			}
		}
	}
	_TIFFfree(raster);
	TIFFClose(tif);
	return output_buffer;
}

unsigned char* loadImage_Jxl(const char* path, int* width, int* height) {
	uint8_t* file_data = NULL;
	size_t file_size = 0;
	FILE* f = fopen(path, "rb");
	if(!f) return NULL;
	fseek(f, 0, SEEK_END);
	file_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	file_data = (uint8_t*)malloc(file_size);
	if(!file_data || fread(file_data, 1, file_size, f) != file_size) {
		if(file_data) free(file_data); fclose(f); return NULL;
	}
	fclose(f);
	JxlDecoder* dec = JxlDecoderCreate(NULL);
	if (!dec) {
		free(file_data); return NULL;
	}
	unsigned char* output_buffer = NULL;
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) == JXL_DEC_SUCCESS) {
		JxlBasicInfo info;
		JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
		JxlDecoderSetInput(dec, file_data, file_size);
		JxlDecoderCloseInput(dec);
		for (;;) {
			JxlDecoderStatus status = JxlDecoderProcessInput(dec);
			if (status == JXL_DEC_ERROR) break;
			else if (status == JXL_DEC_SUCCESS) {
				if(output_buffer) {
					*width = info.xsize; *height = info.ysize;
				}
				break;
			}
			else if (status == JXL_DEC_BASIC_INFO) {
				if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) break;
			}
			else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
				size_t buffer_size;
				if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) break;
				output_buffer = (unsigned char*)malloc(buffer_size);
				if (!output_buffer) break;
				if (JxlDecoderSetImageOutBuffer(dec, &format, output_buffer, buffer_size) != JXL_DEC_SUCCESS) {
					free(output_buffer);
					output_buffer = NULL;
					break;
				} 
			}
			else if (status == JXL_DEC_FULL_IMAGE) {} else break;
		}
	}
	JxlDecoderDestroy(dec);
	free(file_data);
	return output_buffer;
}

unsigned char* loadImage_SPNG(const char* path, int* width, int* height) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	spng_ctx* ctx = spng_ctx_new(0);
	if (!ctx) {
		fclose(f); return NULL;
	}
	spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
	spng_set_png_file(ctx, f);
	struct spng_ihdr ihdr;
	if (spng_get_ihdr(ctx, &ihdr)) {
		spng_ctx_free(ctx);
		fclose(f);
		return NULL;
	}
	size_t image_size;
	if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &image_size)) {
		spng_ctx_free(ctx); fclose(f); return NULL;
	}
	unsigned char* output_buffer = (unsigned char*)malloc(image_size);
	if (!output_buffer) {
		spng_ctx_free(ctx); fclose(f); return NULL;
	}
	if (spng_decode_image(ctx, output_buffer, image_size, SPNG_FMT_RGBA8, 0)) {
		free(output_buffer); output_buffer = NULL;
	} else {
		*width = ihdr.width; *height = ihdr.height;
	}
	spng_ctx_free(ctx); fclose(f);
	return output_buffer;
}

unsigned char* loadImage_JpegTurbo(const char* path, int* width, int* height) {
	unsigned char* file_data = NULL;
	size_t data_size = 0;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	data_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	file_data = (unsigned char*)malloc(data_size);
	if (!file_data || fread(file_data, 1, data_size, f) != data_size) {
		if(file_data) free(file_data); fclose(f); return NULL;
	}
	fclose(f);
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo); free(file_data); return NULL;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, file_data, data_size);
	jpeg_read_header(&cinfo, TRUE);
	cinfo.out_color_space = JCS_EXT_RGBA;
	jpeg_start_decompress(&cinfo);
	*width = cinfo.output_width;
	*height = cinfo.output_height;
	int row_stride = (*width) * cinfo.output_components;
	unsigned char* output_buffer = (unsigned char*)malloc((*height) * row_stride);
	if (!output_buffer) {
		jpeg_destroy_decompress(&cinfo); free(file_data); return NULL;
	}
	JSAMPROW row_pointer[1];
	while (cinfo.output_scanline < cinfo.output_height) {
		row_pointer[0] = &output_buffer[cinfo.output_scanline * row_stride];
		jpeg_read_scanlines(&cinfo, row_pointer, 1);
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	free(file_data);
	return output_buffer;
}
