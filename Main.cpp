#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <android-base/logging.h>

// EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGLUtils.h>

// GLES
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES2/gl2platform.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <GLES3/gl3platform.h>

#include <WindowSurface.h>
#include <math/mat4.h>
#include <png.h>

using namespace android;

// Vertex shader
static const GLchar *Recvertex_shader_source =
    "#version 300 es\n"
    "layout(location = 0) in vec4 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "out vec2 v_Texcoord;\n"
    "void main() {\n"
    "   gl_Position = a_position;\n"
    "   v_Texcoord = a_texcoord;\n"
    "}\n";

// pixel shader
static const GLchar *Recfragment_shader_source =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_Texture;\n"
    "in vec2 v_Texcoord;\n"
    "out vec4 vOutColor;\n"
    "void main() {\n"
    "   vec4 texel = texture(u_Texture, v_Texcoord);\n"
    "   vOutColor = texel;\n"
    "}\n";

static const GLfloat vertsCarPos[] = {
    -0.5, 0.8, 0.0f,  // Top-left in window space
    0.5, 0.8, 0.0f,   // Top-right
    -0.5, -0.8, 0.0f, // Bottom-left
    0.5, -0.8, 0.0f   // Bottom-right
};

static const GLfloat vertsCarTex[] = {
    0.0f, 0.0f, // Top-left in window space
    1.0f, 0.0f, // Top-right
    0.0f, 1.0f, // Bottom-left
    1.0f, 1.0f  // Bottom-right
};

GLint common_get_shader_program(const char *vertex_shader_source, const char *fragment_shader_source)
{
    enum Consts
    {
        INFOLOG_LEN = 512
    };
    GLchar infoLog[INFOLOG_LEN];
    GLint fragment_shader;
    GLint shader_program;
    GLint success;
    GLint vertex_shader;

    /* Vertex shader */
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertex_shader, INFOLOG_LEN, NULL, infoLog);
        printf("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n%s\n", infoLog);
    }

    /* Fragment shader */
    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragment_shader, INFOLOG_LEN, NULL, infoLog);
        printf("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n%s\n", infoLog);
    }

    /* Link shaders */
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shader_program, INFOLOG_LEN, NULL, infoLog);
        printf("ERROR::SHADER::PROGRAM::LINKING_FAILED\n%s\n", infoLog);
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return shader_program;
}

bool initTexture(const char *filename, GLint &TWidth,
                 GLint &THeight, GLuint &TId)
{
    // Open the PNG file
    FILE *inputFile = fopen(filename, "rb");
    if (inputFile == 0)
    {
        perror(filename);
        return false;
    }

    // Read the file header and validate that it is a PNG
    static const int kSigSize = 8;
    png_byte header[kSigSize] = {0};
    fread(header, 1, kSigSize, inputFile);
    if (png_sig_cmp(header, 0, kSigSize))
    {
        LOG(ERROR) << filename << " is not a PNG.\n";
        fclose(inputFile);
        return false;
    }

    // Set up our control structure
    png_structp pngControl = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!pngControl)
    {
        LOG(ERROR) << "png_create_read_struct failed.\n";
        fclose(inputFile);
        return false;
    }

    // Set up our image info structure
    png_infop pngInfo = png_create_info_struct(pngControl);
    if (!pngInfo)
    {
        LOG(ERROR) << "error: png_create_info_struct returned 0.\n";
        png_destroy_read_struct(&pngControl, nullptr, nullptr);
        fclose(inputFile);
        return false;
    }

    // Install an error handler
    if (setjmp(png_jmpbuf(pngControl)))
    {
        LOG(ERROR) << "libpng reported an error\n";
        png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
        fclose(inputFile);
        return false;
    }

    // Set up the png reader and fetch the remaining bits of the header
    png_init_io(pngControl, inputFile);
    png_set_sig_bytes(pngControl, kSigSize);
    png_read_info(pngControl, pngInfo);

    // Get basic information about the PNG we're reading
    int bitDepth;
    int colorFormat;
    png_uint_32 width;
    png_uint_32 height;
    png_get_IHDR(pngControl, pngInfo,
                 &width, &height,
                 &bitDepth, &colorFormat,
                 NULL, NULL, NULL);
    LOG(INFO) << "Image Width: " << width << ", Height: " << height;
    GLint format;
    switch (colorFormat)
    {
    case PNG_COLOR_TYPE_RGB:
        format = GL_RGB;
        break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
        format = GL_RGBA;
        break;
    default:
        LOG(ERROR) << filename << " Unknown libpng color format" << colorFormat;
        return false;
    }

    // Refresh the values in the png info struct in case any transformation shave been applied.
    png_read_update_info(pngControl, pngInfo);
    int stride = png_get_rowbytes(pngControl, pngInfo);
    stride += 3 - ((stride - 1) % 4); // glTexImage2d requires rows to be 4-byte aligned

    // Allocate storage for the pixel data
    png_byte *buffer = (png_byte *)malloc(stride * height);
    if (buffer == NULL)
    {
        LOG(ERROR) << "error: could not allocate memory for PNG image data\n";
        png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
        fclose(inputFile);
        return false;
    }

    // libpng needs an array of pointers into the image data for each row
    png_byte **rowPointers = (png_byte **)malloc(height * sizeof(png_byte *));
    if (rowPointers == NULL)
    {
        LOG(ERROR) << "Failed to allocate temporary row pointers\n";
        png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
        free(buffer);
        fclose(inputFile);
        return false;
    }
    for (unsigned int r = 0; r < height; r++)
    {
        rowPointers[r] = buffer + r * stride;
    }

    // Read in the actual image bytes
    png_read_image(pngControl, rowPointers);
    png_read_end(pngControl, nullptr);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, buffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    TWidth = width;
    THeight = height;
    TId = texture;

    png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
    free(buffer);
    free(rowPointers);
    fclose(inputFile);
    return true;
}

int main(void)
{
    GLint TextureWidth;
    GLint TextureHeight;
    GLuint TextureId;

    GLuint shader_program;
    EGLSurface surface;
    EGLContext context;
    EGLint majorVersion = 0;
    EGLint minorVersion = 0;
    WindowSurface windowSurface;
    EGLNativeWindowType window;
    EGLDisplay display;

    EGLConfig config;
    int w, h;

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE};

    window = windowSurface.getSurface();
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, &majorVersion, &minorVersion);

    status_t err = EGLUtils::selectConfigForNativeWindow(
        display, configAttribs, window, &config);
    if (err)
    {
        LOG(ERROR) << "couldn't find an EGLConfig matching the screen format\n";
        return -1;
    }

    surface = eglCreateWindowSurface(display, config, window, NULL);
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    eglMakeCurrent(display, surface, surface, context);

    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));

    // Create the shader program for our simple pipeline
    shader_program = common_get_shader_program(Recvertex_shader_source, Recfragment_shader_source);
    glUseProgram(shader_program);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, w, h);

    // Bind the texture and assign it to the shader's sampler
    GLint sampler = glGetUniformLocation(shader_program, "u_Texture");
    glUniform1i(sampler, 0);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertsCarPos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vertsCarTex);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // Load Texture
    if (initTexture("/system/etc/Car_Image/Car_Image.png", TextureWidth, TextureHeight, TextureId))
    {
        printf("Texture init done\n");
    }

    while (true)
    {
        // Clear the screen to black
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Draw a rectangle from the 2 triangles using 4 indices
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        // Swap buffers
        eglSwapBuffers(display, surface);
    }
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    glDeleteProgram(shader_program);
    eglTerminate(display);

    return EXIT_SUCCESS;
}
