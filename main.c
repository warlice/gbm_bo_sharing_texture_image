
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <math.h>

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

void parse_arguments(int argc, char **argv, int *is_server);
int* create_data(size_t size);
void rotate_data(int* data, size_t size);

int main(int argc, char **argv)
{
    // Parse arguments
    int is_server;
    parse_arguments(argc, argv, &is_server);

    // Create X11 window
    Display *x11_display;
    Window x11_window;
    create_x11_window(is_server, &x11_display, &x11_window);

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

    int fd= open("/dev/dri/renderD128", O_RDWR);

    struct gbm_device* gbm = gbm_create_device(fd);
    if (gbm == NULL) {
        perror("create gbm device failed\n");
        return -1;
    }
    printf("create gbm\n");
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay gbm_display = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA, gbm, NULL);
    eglInitialize(gbm_display, NULL, NULL);

    struct gbm_bo * bo ;
    int* texture_data = create_data(TEXTURE_DATA_SIZE);

    // -----------------------------
    // --- Texture sharing start ---
    // -----------------------------

    // Socket paths for sending/receiving file descriptor and image storage data
    const char *SERVER_FILE = "/tmp/test_server";
    const char *CLIENT_FILE = "/tmp/test_client";
    // Custom image storage data description to transfer over socket
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

    // The next `if` block contains server code in the `true` branch and client code in the `false` branch. The `true` branch is always executed first and the `false` branch after it (in a different process). This is because the server loops at the end of the branch until it can send a message to the client and the client blocks at the start of the branch until it has a message to read. This way the whole `if` block from top to bottom represents the order of events as they happen.
    if (is_server)
    {
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
        memcpy(addr,texture_data,sizeof(texture_data));
        gbm_bo_unmap(bo,addr);

        // GL: Create and populate the texture
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         GLint format;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        printf("format %x\n",format);

	EGLint attribute_list = EGL_NONE; 
       EGLImage image = eglCreateImageKHR(gbm_display,EGL_NO_CONTEXT,EGL_NATIVE_PIXMAP_KHR,bo,&attribute_list);
       EGLint err = eglGetError();
       if (err != EGL_SUCCESS) {
	       printf("create image failed %x\n",err);
	       return -1;
       } 
        // EGL: Create EGL image from the GL texture
        /*EGLImage image = eglCreateImage(egl_display,
                                        egl_context,
                                        EGL_GL_TEXTURE_2D,
                                        (EGLClientBuffer)(uint64_t)texture,
                                        NULL);
        assert(image != EGL_NO_IMAGE);
	*/
        int bofd = gbm_bo_get_fd(bo);
        if (bofd == 0 ){
            perror("get fd failed \n");
            return -1;
        }
        // The next line works around an issue in radeonsi driver (fixed in master at the time of writing). If you are
        // having problems with texture rendering until the first texture update you can uncomment this line
        // glFlush();

        // EGL (extension: EGL_MESA_image_dma_buf_export): Get file descriptor (texture_dmabuf_fd) for the EGL image and get its
        // storage data (texture_storage_metadata)
        int texture_dmabuf_fd;
        struct texture_storage_metadata_t texture_storage_metadata;

        /*int num_planes;
        PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA =
            (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
        EGLBoolean queried = eglExportDMABUFImageQueryMESA(egl_display,
                                                           image,
                                                           &texture_storage_metadata.fourcc,
                                                           &num_planes,
                                                           &texture_storage_metadata.modifiers);
        assert(queried);
        assert(num_planes == 1);
        PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA =
            (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");
        EGLBoolean exported = eglExportDMABUFImageMESA(egl_display,
                                                       image,
                                                       &texture_dmabuf_fd,
                                                       &texture_storage_metadata.stride,
                                                       &texture_storage_metadata.offset);
        assert(exported);
	*/
        texture_dmabuf_fd = bofd;
        texture_storage_metadata.offset = gbm_bo_get_offset(bo,0);
        texture_storage_metadata.fourcc = gbm_bo_get_format(bo);
        texture_storage_metadata.stride = gbm_bo_get_stride(bo);


        // Unix Domain Socket: Send file descriptor (texture_dmabuf_fd) and texture storage data (texture_storage_metadata)
        int sock = create_socket(SERVER_FILE);
        while (connect_socket(sock, CLIENT_FILE) != 0)
            ;
        write_fd(sock, texture_dmabuf_fd, &texture_storage_metadata, sizeof(texture_storage_metadata));
        close(sock);
        close(texture_dmabuf_fd);
    }
    else
    {
        // Unix Domain Socket: Receive file descriptor (texture_dmabuf_fd) and texture storage data (texture_storage_metadata)
        int texture_dmabuf_fd;
        struct texture_storage_metadata_t texture_storage_metadata;

        int sock = create_socket(CLIENT_FILE);
        read_fd(sock, &texture_dmabuf_fd, &texture_storage_metadata, sizeof(texture_storage_metadata));
        close(sock);
        struct gbm_import_fd_data data;
        data.fd = texture_dmabuf_fd;
        data.width  = TEXTURE_DATA_WIDTH;
        data.height = TEXTURE_DATA_HEIGHT;
        data.stride = texture_storage_metadata.stride;
        data.format = texture_storage_metadata.fourcc;
        bo = gbm_bo_import(gbm,GBM_BO_IMPORT_FD,&data,GBM_BO_USE_RENDERING);
        if (bo == NULL) {
            perror("import fd failed\n");
            return -1;
        }
       EGLImage image = eglCreateImage(gbm_display,NULL,EGL_NATIVE_PIXMAP_KHR,bo,NULL);
       EGLint err = eglGetError();
       if (err != EGL_SUCCESS) {
	       printf("create image failed %x\n",err);
	       return -1;
       } 
	     
        // EGL (extension: EGL_EXT_image_dma_buf_import): Create EGL image from file descriptor (texture_dmabuf_fd) and storage
        // // data (texture_storage_metadata)
        // EGLAttrib const attribute_list[] = {
        //     EGL_WIDTH, TEXTURE_DATA_WIDTH,
        //     EGL_HEIGHT, TEXTURE_DATA_HEIGHT,
        //     EGL_LINUX_DRM_FOURCC_EXT, texture_storage_metadata.fourcc,
        //     EGL_DMA_BUF_PLANE0_FD_EXT, texture_dmabuf_fd,
        //     EGL_DMA_BUF_PLANE0_OFFSET_EXT, texture_storage_metadata.offset,
        //     EGL_DMA_BUF_PLANE0_PITCH_EXT, texture_storage_metadata.stride,
        //     EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (uint32_t)(texture_storage_metadata.modifiers & ((((uint64_t)1) << 33) - 1)),
        //     EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (uint32_t)((texture_storage_metadata.modifiers>>32) & ((((uint64_t)1) << 33) - 1)),
        //     EGL_NONE};
        // EGLImage image = eglCreateImage(egl_display,
        //                                 NULL,
        //                                 EGL_LINUX_DMA_BUF_EXT,
        //                                 (EGLClientBuffer)NULL,
        //                                 attribute_list);
        assert(image != EGL_NO_IMAGE);
        //close(texture_dmabuf_fd);

        // GLES (extension: GL_OES_EGL_image_external): Create GL texture from EGL image
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl_draw_scene(texture);
	eglSwapBuffers(egl_display, egl_surface);
}

    // -----------------------------
    // --- Texture sharing end ---
    // -----------------------------

	void *addr ;
	EGLImage serverImage;
    time_t last_time = time(NULL);
    if (0){
	    int stride;
	void * mapdata;
	 addr = gbm_bo_map(bo,0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GBM_BO_TRANSFER_WRITE,&stride,&mapdata);
    /*   serverImage = eglCreateImage(gbm_display,NULL,EGL_NATIVE_PIXMAP_KHR,bo,NULL);
       EGLint err = eglGetError();
       if (err != EGL_SUCCESS) {
	       printf("create image failed %x\n",err);
	       return -1;
       } 
       */
    }
	       serverImage = eglCreateImage(gbm_display,NULL,EGL_NATIVE_PIXMAP_KHR,bo,NULL);
	       EGLint err = eglGetError();
	       if (err != EGL_SUCCESS) {
		       printf("create image failed %x\n",err);
		       return -1;
	       }
    while (1)
    {
        // Draw scene (uses shared texture)

	gl_draw_scene(texture);
	eglSwapBuffers(egl_display, egl_surface);
        // Update texture data each second to see that the client didn't just copy the texture and is indeed referencing
        // the same texture data.
        if (is_server)
        {
            time_t cur_time = time(NULL);
            if (last_time < cur_time)
            {
                last_time = cur_time;
                rotate_data(texture_data, TEXTURE_DATA_SIZE);
		glBindTexture(GL_TEXTURE_2D,0);
                glBindTexture(GL_TEXTURE_2D, texture);
	        int stride;
		void * mapdata;
		addr = gbm_bo_map(bo,0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GBM_BO_TRANSFER_WRITE,&stride,&mapdata);
		memcpy(addr,texture_data,TEXTURE_DATA_WIDTH*TEXTURE_DATA_HEIGHT*8);
		printf("addr %d\n",*(int*)addr);
       		gbm_bo_unmap(bo,addr);
                //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, addr);
        	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, serverImage);
		GLuint binding_fbo;
		GLuint fbo;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,(GLint *)&binding_fbo);
		glGenFramebuffers(1,&fbo);
		glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
		unsigned char *pixels = malloc(sizeof(char) * TEXTURE_DATA_WIDTH*TEXTURE_DATA_HEIGHT);
		gl_draw_scene(texture);
		eglSwapBuffers(egl_display, egl_surface);
		

		memset(pixels,0,sizeof(char) *TEXTURE_DATA_SIZE);
		glReadPixels(0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,pixels);	
		printf("ge %d\n",glGetError());
		glBindFramebuffer(GL_READ_FRAMEBUFFER,binding_fbo);
		glDeleteFramebuffers(1,&fbo);
		printf("pixels %d\n",*(int*)pixels);

            }
        }else {
	    int stride;
		 sleep(1);
		void * mapdata;
		if (bo == NULL){
			printf("import bo is null\n");
		}
		 void *maddr = gbm_bo_map(bo,0,0,TEXTURE_DATA_WIDTH,TEXTURE_DATA_HEIGHT,GBM_BO_TRANSFER_READ,&stride,&mapdata);
		 if (maddr == NULL) {
			 perror("why\n");
			 continue;
		 }
		printf("maddr client %d\n",*(int*)maddr);

		gbm_bo_unmap(bo,addr);
	}


        // Check for errors
        assert(glGetError() == GL_NO_ERROR);
        assert(eglGetError() == EGL_SUCCESS);
    }

    gbm_bo_unmap(bo,addr);
    return 0;
}

void help()
{
    printf("USAGE:\n"
           "    dmabufshare server\n"
           "    dmabufshare client\n");
}

void parse_arguments(int argc, char **argv, int *is_server)
{
    if (2 == argc)
    {
        if (strcmp(argv[1], "server") == 0)
        {
            *is_server = 1;
        }
        else if (strcmp(argv[1], "client") == 0)
        {
            *is_server = 0;
        }
        else if (strcmp(argv[1], "--help") == 0)
        {
            help();
            exit(0);
        }
        else
        {
            help();
            exit(-1);
        }
    }
    else
    {
        help();
        exit(-1);
    }
}

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
