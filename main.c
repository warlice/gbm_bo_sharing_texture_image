
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <epoxy/egl.h>

#include <X11/Xlib.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "socket.h"
#include "window.h"
#include "render.h"
#include <fcntl.h>
#include  <drm/drm_fourcc.h>

#include <gbm.h>
#include <string.h>

int* create_data(size_t size);
void rotate_data(int* data, size_t size);
int* create_data(size_t size)
{
	size_t edge = sqrt(size);
	assert(edge * edge == size);
	size_t half_edge = edge / 2;

	int* data = malloc(size * sizeof(int));

	// Paint the texture like so:
	// RG
	// BW
	// where R - red, G - green, B - blue, W - white
	int red = 0x000000FF;
	int green = 0x0000FF00;
	int blue = 0X00FF0000;
	int white = 0x00FFFFFF;
	for (size_t i = 0; i < size; i++) {
		size_t x = i % edge;
		size_t y = i / edge;

		if (x < half_edge) {
			if (y < half_edge) {
				data[i] = red;
			} else {
				data[i] = blue;
			}
		} else {
			if (y < half_edge) {
				data[i] = green;
			} else {
				data[i] = white;
			}
		}
	}

	return data;
}

void rotate_data(int* data, size_t size)
{
	size_t edge = sqrt(size);
	assert(edge * edge == size);
	size_t half_edge = edge / 2;

	for (size_t i = 0; i < half_edge * half_edge; i++) {
		size_t x = i % half_edge;
		size_t y = i / half_edge;

		int temp = data[x + y * edge];
		data[x + y * edge] = data[(x + half_edge) + y * edge];
		data[(x + half_edge) + y * edge] = data[(x + half_edge) + (y + half_edge) * edge];
		data[(x + half_edge) + (y + half_edge) * edge] = data[x + (y + half_edge) * edge];
		data[x + (y + half_edge) * edge] = temp;
	}
}

int main(int argc, char **argv)
{
	// Create X11 window
	Display *x11_display;
	Window x11_window;
	create_x11_window(1, &x11_display, &x11_window);

	// Initialize EGL
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_surface;
	initialize_egl(x11_display, x11_window, &egl_display, &egl_context, &egl_surface);

	// Setup GL scene
	gl_setup_scene();

	// Server texture data
	const size_t TEXTURE_DATA_WIDTH = 256;
	const size_t TEXTURE_DATA_HEIGHT = TEXTURE_DATA_WIDTH;
	const size_t TEXTURE_DATA_SIZE = TEXTURE_DATA_WIDTH * TEXTURE_DATA_HEIGHT;

	int fd= open("/dev/dri/card0", O_RDWR);

	struct gbm_device* gbm = gbm_create_device(fd);
	if (gbm == NULL) {
		perror("create gbm device failed\n");
		return -1;
	}
	printf("create gbm\n");
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	//EGLDisplay gbm_display = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA, gbm, NULL);
	//eglInitialize(gbm_display, NULL, NULL);
	eglInitialize(egl_display, NULL, NULL);

	struct gbm_bo * bo ;
	int* texture_data = create_data(TEXTURE_DATA_SIZE);

	struct texture_storage_metadata_t
	{
		int fourcc;
		EGLuint64KHR modifiers;
		EGLint stride;
		EGLint offset;
	};

	// GL texture that will be shared
	GLuint texture;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES  = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR  = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	if (eglCreateImageKHR == NULL){
		printf("get proc failed\n");
		return -1;
	}

	bo  = gbm_bo_create(gbm,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,DRM_FORMAT_ARGB8888,GBM_BO_USE_RENDERING);
	if (bo == NULL) {
		perror("create bo failed\n");
		return -1;
	}
	void *mapdata;
	int stride;
	void *addr = gbm_bo_map(bo,0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GBM_BO_TRANSFER_READ_WRITE,&stride,&mapdata);
	if (addr == NULL) {
		perror("map bo failed\n");
		return -1;
	}
	memcpy(addr,texture_data,TEXTURE_DATA_SIZE*4);
	gbm_bo_unmap(bo,addr);

	// GL: Create and populate the texture
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	/*
	   GLint format;
	   glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
	   printf("format %x\n",format);
	 */

	int bofd = gbm_bo_get_fd(bo);
	if (bofd == 0 ){
		perror("get fd failed \n");
		return -1;
	}
	int texture_dmabuf_fd;
	struct texture_storage_metadata_t texture_storage_metadata;
	texture_dmabuf_fd = bofd;
	texture_storage_metadata.offset = gbm_bo_get_offset(bo,0);
	texture_storage_metadata.fourcc = gbm_bo_get_format(bo);
	texture_storage_metadata.stride = gbm_bo_get_stride(bo);
	texture_storage_metadata.modifiers = gbm_bo_get_modifier(bo);

	//get a new bo by importing the fd returned from gbm_bo_get
	struct gbm_import_fd_data data;
	data.fd = texture_dmabuf_fd;
	data.width  = TEXTURE_DATA_WIDTH;
	data.height = TEXTURE_DATA_HEIGHT;
	data.stride = texture_storage_metadata.stride;
	data.format = texture_storage_metadata.fourcc;
	struct gbm_bo *importbo = gbm_bo_import(gbm,GBM_BO_IMPORT_FD,&data,GBM_BO_USE_RENDERING);
	if (importbo == NULL) {
		perror("import fd failed\n");
		return -1;
	}
	//create image from the imported bo in the last step
	const EGLint imageAttributes[] = 
	{
		EGL_WIDTH, TEXTURE_DATA_WIDTH,
		EGL_HEIGHT ,TEXTURE_DATA_HEIGHT,
		EGL_LINUX_DRM_FOURCC_EXT, texture_storage_metadata.fourcc,
		EGL_DMA_BUF_PLANE0_FD_EXT, texture_dmabuf_fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, texture_storage_metadata.offset,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, texture_storage_metadata.stride,
		EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,(uint32_t)(texture_storage_metadata.modifiers & (((( uint64_t)1) << 33) -1)),
		EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,(uint32_t)((texture_storage_metadata.modifiers >> 32) & (((( uint64_t)1) << 33) -1)),
		EGL_NONE
	};
	EGLImage image = eglCreateImageKHR(egl_display,NULL,EGL_LINUX_DMA_BUF_EXT,NULL,imageAttributes);
	EGLint err = eglGetError();
	if (err != EGL_SUCCESS) {
		printf("create image failed %x\n",err);
		return -1;
	} 
	assert(image != EGL_NO_IMAGE);


	void *imageaddr;
	time_t last_time = time(NULL);

	/* import the image to print data of the image*/
	//struct gbm_bo *imagebo = gbm_bo_import(gbm,GBM_BO_IMPORT_EGL_IMAGE,image,GBM_BO_USE_RENDERING);
	unsigned char *buffer = (unsigned char*)malloc(TEXTURE_DATA_SIZE*4);
	//glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
	while (1)
	{
		// Draw scene (uses shared texture)
		gl_draw_scene(texture);
		eglSwapBuffers(egl_display, egl_surface);

		// Update texture data each second to see that the client didn't just copy the texture and is indeed referencing
		// the same texture data.
		time_t cur_time = time(NULL);
		if (last_time < cur_time)
		{
			last_time = cur_time;
			rotate_data(texture_data, TEXTURE_DATA_SIZE);
			glBindTexture(GL_TEXTURE_2D,0);
			glBindTexture(GL_TEXTURE_2D, texture);
			printf("%x while \n", glGetError());
			int stride;
			void * mapdata;
			void *addr = gbm_bo_map(importbo,0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GBM_BO_TRANSFER_WRITE,&stride,&mapdata);
			if (addr == NULL){
				perror("map importe bo failed\n");
				return -1; 
			}

			if (memcpy(addr,texture_data,TEXTURE_DATA_WIDTH*TEXTURE_DATA_HEIGHT*4) < 0 ){
				printf("memset failed\n");
				return -1;
			}
			printf("imported bo %x\n",*(int*)addr);

			/* import the image to print data of the image*/
			//void *imageaddr = gbm_bo_map(imagebo,0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GBM_BO_TRANSFER_WRITE,&stride,&mapdata);
			//printf("imaged bo %x\n",*(int*)imageaddr);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			printf("%x tex 2d while \n", glGetError());
			//	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, addr);
			//	printf("%x sub imagel \n", glGetError());
			glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
			printf("%x gl egl image target texture 2d oes \n", glGetError());
			/*
				glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,buffer);
				printf("image %x\n",*(int*)buffer);
				*/
			 
			//gbm_bo_unmap(imagebo,imageaddr);
			gbm_bo_unmap(importbo,addr);
		}

		// Check for errors
		assert(glGetError() == GL_NO_ERROR);
		assert(eglGetError() == EGL_SUCCESS);
	}
	free(buffer);
	//gbm_bo_destroy(imagebo);
	gbm_bo_destroy(importbo);
	gbm_bo_destroy(bo);
	return 0;
}




