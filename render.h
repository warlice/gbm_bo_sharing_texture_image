#pragma once

#include <stddef.h>

#include <epoxy/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>

void initialize_egl(Display *x11_display, Window x11_window, EGLDisplay *egl_display, EGLContext *egl_context, EGLSurface *egl_surface)
{
   printf("EGL Client APIs: %s", eglQueryString(egl_display, EGL_CLIENT_APIS));
    // Set OpenGL rendering API
    eglBindAPI(EGL_OPENGL_ES_API);
    //printf(" %x kjkjkj \n", eglGetError());

    // get an EGL display connection
    EGLDisplay display = eglGetDisplay(x11_display);

    // initialize the EGL display connection
    eglInitialize(display, NULL, NULL);

    // get an appropriate EGL frame buffer configuration
    EGLConfig config;
    EGLint num_config;
    EGLint const attribute_list_config[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE};
    eglChooseConfig(display, attribute_list_config, &config, 1, &num_config);

    // create an EGL rendering context
    EGLint const attrib_list[] = {
        EGL_CONTEXT_MAJOR_VERSION, 2,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrib_list);

    // create an EGL window surface
    EGLSurface surface = eglCreateWindowSurface(display, config, x11_window, NULL);

    // connect the context to the surface
    eglMakeCurrent(display, surface, surface, context);

    // Return
    *egl_display = display;
    *egl_context = context;
    *egl_surface = surface;
}

void gl_setup_scene()
{
    // Shader source that draws a textures quad
  /*  const char *vertex_shader_source = "#version 330 core\n"
                                       "layout (location = 0) in vec3 aPos;\n"
                                       "layout (location = 1) in vec2 aTexCoords;\n"

                                       "out vec2 TexCoords;\n"

                                       "void main()\n"
                                       "{\n"
                                       "   TexCoords = aTexCoords;\n"
                                       "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
                                       "}\0";
    const char *fragment_shader_source = "#version 330 core\n"
                                         "out vec4 FragColor;\n"

                                         "in vec2 TexCoords;\n"

                                         "uniform sampler2D Texture1;\n"

                                         "void main()\n"
                                         "{\n"
                                         "   FragColor = texture(Texture1, TexCoords);\n"
                                         "}\0";
					 */

     const char *vertex_shader_source = "   precision mediump float;"
      "\n "
      "\n "
      "\n // Model-view-projection transformation matrix"
      "\n //uniform mat4 mvp_matrix;"
      "\n "
      "\n // Incoming"
      "\n attribute vec3 a_position;      // vertex position in 2d Model Space"
      "\n attribute vec3 a_color;         // color of the rectangular segment this vertex is part of"
      "\n attribute vec2 aTexCoords;"
      "\n "
      "\n // Outgoing interpolated values for the fragment shader"
      "\n varying vec3 color;"
      "\n varying vec2 TexCoords;"
      "\n "
      "\n void main() {"
      "\n   gl_Position = vec4(a_position.x, a_position.y, a_position.z,1.0);"
      "\n   TexCoords = aTexCoords;"
      "\n   color = a_color;"
      "\n }\0";
   const char *fragment_shader_source = "   precision mediump float;"
      "\n "
      "\n varying vec2 TexCoords;"
      "\n varying vec4 FragColor;"
      "\n uniform sampler2D Texture1;"

      "\n varying vec3 color;"
      "\n "
      "\n void main() {"
      "\n   gl_FragColor = texture2D(Texture1,TexCoords);"
      "\n   //gl_FragColor = vec4(color, 1.0);"
      "\n }\0";
 

    // vertex shader
    int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    // fragment shader
    int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    // link shaders
    int shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    // delete shaders
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    // quad
    float vertices[] = {
        0.5f, 0.5f, 0.0f, 1.0f, 0.0f,   // top right
        0.5f, -0.5f, 0.0f, 1.0f, 1.0f,  // bottom right
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, // bottom left
        -0.5f, 0.5f, 0.0f, 0.0f, 0.0f   // top left
    };
    unsigned int indices[] = {
        0, 1, 3, // first Triangle
        1, 2, 3  // second Triangle
    };

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    // Prebind needed stuff for drawing
    glUseProgram(shader_program);
    printf("%x use program \n",eglGetError());
    glBindVertexArray(VAO);
}

void gl_draw_scene(GLuint texture)
{
	GLint err ;
    // clear
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // draw quad
    // VAO and shader program are already bound from the call to gl_setup_scene
    glActiveTexture(GL_TEXTURE0);
    err = glGetError();
    if (err != GL_NO_ERROR) {
	printf("%x active \n", glGetError());
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    err = glGetError();
    if (err != GL_NO_ERROR) {
	printf("%x bind\n", glGetError());
    }
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    err = glGetError();
    if (err != GL_NO_ERROR) {
	printf("%x draw\n", glGetError());
    }
}
