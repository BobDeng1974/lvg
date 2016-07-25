#include <stdio.h>
#include <string.h>
#include <float.h>
#include <zip.h>
#define GL_GLEXT_PROTOTYPES
#ifdef EMSCRIPTEN
#include <emscripten.h>
#define GLFW_INCLUDE_ES2
#endif
#include <GLFW/glfw3.h>

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#ifndef EMSCRIPTEN
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

#include "nanovg.h"
#ifdef EMSCRIPTEN
#define NANOVG_GLES2_IMPLEMENTATION
#else
#include <libtcc.h>
#define NANOVG_GL2_IMPLEMENTATION
#endif
#include "nanovg_gl.h"

struct zip *g_zip;
NVGcontext *vg = NULL;
GLFWwindow *window;
int winWidth, winHeight;
int width = 0, height = 0;
double mx = 0, my = 0;
double g_time;
int mkeys = 0;
const char *g_main_script;

#ifndef EMSCRIPTEN
void (*onInit)();
void (*onFrame)();
#else
extern void onInit();
extern void onFrame();
#endif

int get_zip_contents(const char *fname, char **buf)
{
    struct zip_stat st;
    struct zip_file *zf;
    zip_int64_t idx;
    if ((idx = zip_name_locate(g_zip, fname, 0)) < 0)
	return -1;
    if (!(zf = zip_fopen_index(g_zip, idx, 0)))
        return -1;
    zip_stat_index(g_zip, idx, 0, &st);
    *buf = malloc(st.size + 1);
    (*buf)[st.size] = 0;
    zip_fread(zf, *buf, st.size);
    zip_fclose(zf);
    return st.size;
}

NSVGimage *lvgLoadSVG(const char *file)
{
    char *buf;
    double time = glfwGetTime();
    if (get_zip_contents(file, &buf) < 0)
    {
        printf("error: could not open SVG image.\n");
        return 0;
    }
    double time2 = glfwGetTime();
    printf("zip time: %fs\n", time2 - time);
    NSVGimage *image = nsvgParse(buf, "px", 96.0f);
    free(buf);
    time = glfwGetTime();
    printf("svg load time: %fs\n", time - time2);
    return image;
}

static inline NVGcolor nvgColorU32(uint32_t c)
{
    return nvgRGBA(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255);
}

static void nvgSVGLinearGrad(struct NVGcontext *vg, struct NSVGshape *shape, int is_fill)
{
    struct NSVGgradient *grad = shape->fill.gradient;
    float sx = shape->bounds[0];
    float sy = shape->bounds[1];
    float ex = shape->bounds[2];
    float ey = shape->bounds[3];
    NVGcolor cs = nvgColorU32(grad->stops[0].color);
    NVGcolor ce = nvgColorU32(grad->stops[1].color);

    NVGpaint p = nvgLinearGradient(vg, sx, sy, ex, ey, nvgColorU32(grad->stops[0].color), ce);
    if (is_fill)
        nvgFillPaint(vg, p);
    else
        nvgStrokePaint(vg, p);
}

static void nvgSVGRadialGrad(struct NVGcontext *vg, struct NSVGshape *shape, int is_fill)
{
    struct NSVGgradient *grad = shape->fill.gradient;
    float cx = (shape->bounds[0] + shape->bounds[2])/2.0;
    float cy = (shape->bounds[1] + shape->bounds[3])/2.0;
    NVGcolor cs = nvgColorU32(grad->stops[0].color);
    NVGcolor ce = nvgColorU32(grad->stops[1].color);

    NVGpaint p = nvgRadialGradient(vg, cx, cy, 0, cx, cs, ce);
    if (is_fill)
        nvgFillPaint(vg, p);
    else
        nvgStrokePaint(vg, p);
}

void lvgDrawSVG(NSVGimage *image)
{
    NSVGshape *shape;
    NSVGpath *path;

    int i;
    for (shape = image->shapes; shape != NULL; shape = shape->next)
    {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE))
            continue;
    
        for (path = shape->paths; path != NULL; path = path->next)
        {
            nvgBeginPath(vg);
            nvgMoveTo(vg, path->pts[0], path->pts[1]);
            for (i = 0; i < path->npts - 1; i += 3)
            {
                float *p = &path->pts[i*2];
                nvgBezierTo(vg, p[2], p[3], p[4], p[5], p[6], p[7]);
            }
            if (path->closed)
            {
                nvgLineTo(vg, path->pts[0], path->pts[1]);
                if (NSVG_PAINT_COLOR == shape->fill.type)
                    nvgFillColor(vg, nvgColorU32(shape->fill.color));
                else if (NSVG_PAINT_LINEAR_GRADIENT == shape->fill.type)
                    nvgSVGLinearGrad(vg, shape, 1);
                else if (NSVG_PAINT_RADIAL_GRADIENT == shape->fill.type)
                    nvgSVGRadialGrad(vg, shape, 1);
                if (NSVG_PAINT_NONE != shape->fill.type)
                    nvgFill(vg);
            }
            if (NSVG_PAINT_NONE != shape->stroke.type)
            {
                if (NSVG_PAINT_COLOR == shape->stroke.type)
                    nvgStrokeColor(vg, nvgColorU32(shape->stroke.color));
                else if (NSVG_PAINT_LINEAR_GRADIENT == shape->fill.type)
                    nvgSVGLinearGrad(vg, shape, 0);
                else if (NSVG_PAINT_RADIAL_GRADIENT == shape->fill.type)
                    nvgSVGRadialGrad(vg, shape, 0);
                nvgStrokeWidth(vg, shape->strokeWidth);
                nvgStroke(vg);
            }
        }
    }
}

void drawframe()
{
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwGetWindowSize(window, &winWidth, &winHeight);
    glfwGetFramebufferSize(window, &width, &height);
    glfwGetCursorPos(window, &mx, &my);
    mkeys = 0;
    mkeys |= glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ? 1 : 0;
    mkeys |= glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS ? 2 : 0;
    mkeys |= glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ? 4 : 0;

    glViewport(0, 0, width, height);
    glClearColor(22.0f/255.0f, 22.0f/255.0f, 22.0f/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(vg, winWidth, winHeight, (float)width / (float)winWidth);
    //nvgSave(vg);

    g_time = glfwGetTime();
#ifdef EMSCRIPTEN
    if (g_main_script)
#else
    if (onFrame)
#endif
        onFrame();

    //nvgRestore(vg);
    nvgEndFrame(vg);
    glfwSwapBuffers(window);
    glfwPollEvents();
}

#ifndef EMSCRIPTEN
void resizecb(GLFWwindow* window, int width, int height)
{
    // Update and render
    NSVG_NOTUSED(width);
    NSVG_NOTUSED(height);
    drawframe();
}

const char g_header[] =
    "#include <math.h>\n"
    "#include \"nanovg.h\"\n"
    "#define NANOSVG_ALL_COLOR_KEYWORDS\n"
    "#include \"nanosvg.h\"\n"
    "\n"
    "void lvgDrawSVG(NSVGimage *image);\n"
    "NSVGimage *lvgLoadSVG(const char *file);\n"
    "\n"
    "extern NVGcontext *vg;\n"
    "extern int winWidth;\n"
    "extern int winHeight;\n"
    "extern int width;\n"
    "extern int height;\n"
    "extern int mkeys;\n"
    "extern double mx;\n"
    "extern double my;\n"
    "extern double g_time;\n"
    "\n";

struct SYM
{
    const char *m_name;
    void *m_sym;
};

const struct SYM g_syms[] = {
    { "lvgDrawSVG", lvgDrawSVG },
    { "lvgLoadSVG", lvgLoadSVG },
    { "sin", sin },
    { "floor", floor },

    { "nvgScale", nvgScale },
    { "nvgSave", nvgSave },
    { "nvgBeginPath", nvgBeginPath },
    { "nvgRoundedRect", nvgRoundedRect },
    { "nvgRGBA", nvgRGBA },
    { "nvgFillColor", nvgFillColor },
    { "nvgFill", nvgFill },
    { "nvgBoxGradient", nvgBoxGradient },
    { "nvgRect", nvgRect },
    { "nvgPathWinding", nvgPathWinding },
    { "nvgFillPaint", nvgFillPaint },
    { "nvgLinearGradient", nvgLinearGradient },
    { "nvgMoveTo", nvgMoveTo },
    { "nvgLineTo", nvgLineTo },
    { "nvgStrokeColor", nvgStrokeColor },
    { "nvgStroke", nvgStroke },
    { "nvgFontSize", nvgFontSize },
    { "nvgFontFace", nvgFontFace },
    { "nvgTextAlign", nvgTextAlign },
    { "nvgFontBlur", nvgFontBlur },
    { "nvgText", nvgText },
    { "nvgRestore", nvgRestore },
    { "nvgTextBounds", nvgTextBounds },
    { "nvgRadialGradient", nvgRadialGradient },
    { "nvgCircle", nvgCircle },
    { "nvgEllipse", nvgEllipse },
    { "nvgBezierTo", nvgBezierTo },
    { "nvgStrokeWidth", nvgStrokeWidth },
    { "nvgArc", nvgArc },
    { "nvgClosePath", nvgClosePath },
    { "nvgScissor", nvgScissor },
    { "nvgTranslate", nvgTranslate },
    { "nvgImageSize", nvgImageSize },
    { "nvgImagePattern", nvgImagePattern },
    { "nvgHSLA", nvgHSLA },
    { "nvgRotate", nvgRotate },
    { "nvgLineCap", nvgLineCap },
    { "nvgLineJoin", nvgLineJoin },
    { "nvgCreateImage", nvgCreateImage },
    { "nvgCreateFont", nvgCreateFont },
    { "nvgDeleteImage", nvgDeleteImage },
    { "nvgTextMetrics", nvgTextMetrics },
    { "nvgTextBreakLines", nvgTextBreakLines },
    { "nvgTextGlyphPositions", nvgTextGlyphPositions },
    { "nvgTextLineHeight", nvgTextLineHeight },
    { "nvgTextBoxBounds", nvgTextBoxBounds },
    { "nvgGlobalAlpha", nvgGlobalAlpha },
    { "nvgTextBox", nvgTextBox },
    { "nvgDegToRad", nvgDegToRad },
    { "nvgResetScissor", nvgResetScissor },
    { "nvgIntersectScissor", nvgIntersectScissor },

    { "vg", &vg },
    { "winWidth", &winWidth },
    { "winHeight", &winHeight },
    { "width", &width },
    { "height", &height },
    { "mkeys", &mkeys },
    { "mx", &mx },
    { "my", &my },
    { "g_time", &g_time }
};

void (tcc_error_func)(void *opaque, const char *msg)
{
    printf("%s\n", msg);
    fflush(stdout);
}

int loadScript()
{
    TCCState *s;
    char *buf, *source;
    int size, i;

    if (get_zip_contents("main.c", &buf) < 0)
    {
        printf("error: could not open C script.\n");
        return -1;
    }
    source = malloc(strlen(g_header) + strlen(buf) + 1);
    source[0] = 0;
    strcat(source, g_header);
    strcat(source, buf);
    free(buf);

    s = tcc_new();
    tcc_set_error_func(s, 0, tcc_error_func);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    tcc_add_include_path(s, ".");
    tcc_add_include_path(s, "./include");
    tcc_set_lib_path(s, "./lib");

    if (tcc_compile_string(s, source) == -1)
        goto error;

    for (i = 0; i < sizeof(g_syms)/sizeof(g_syms[0]); i++)
        tcc_add_symbol(s, g_syms[i].m_name, g_syms[i].m_sym);

    size = tcc_relocate(s, TCC_RELOCATE_AUTO);
    if (size == -1)
        goto error;

    onInit  = tcc_get_symbol(s, "onInit");
    onFrame = tcc_get_symbol(s, "onFrame");
    free(source);
    return 0;
error:
    free(source);
    return -1;
}
#endif

int open_lvg(const char *file)
{
    int err;
    if ((g_zip = zip_open(file, 0, &err)) == NULL)
        return -1;
    g_main_script = 0;
    char *buf;
#ifdef EMSCRIPTEN
    if (get_zip_contents("main.js", &buf) < 0)
        printf("error: could not open JS script.\n");
    else
        g_main_script = buf;
#else
    loadScript();
#endif
    return 0;
}

int main(int argc, char **argv)
{
    if (!glfwInit() || open_lvg(argc > 1 ? argv[1] : "test.lvg"))
    {
        printf("error");
        return -1;
    }

#ifdef EMSCRIPTEN
    glfwWindowHint(GLFW_RESIZABLE , 1);
    window = glfwCreateWindow(1024, 1024, "LVG Player", NULL, NULL);
#else
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    window = glfwCreateWindow(mode->width - 40, mode->height - 80, "LVG Player", NULL, NULL);
#endif
    if (!window)
    {
        printf("Could not open window\n");
        glfwTerminate();
        return -1;
    }

#ifndef EMSCRIPTEN
    glfwSetFramebufferSizeCallback(window, resizecb);
    glfwSwapInterval(1);
#endif
    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, 1);

#ifndef EMSCRIPTEN
    if (0)
    {
        /*int w = (int)g_image->width;
        int h = (int)g_image->height;
        NSVGrasterizer *rast = nsvgCreateRasterizer();
        if (rast == NULL) {
            printf("Could not init rasterizer.\n");
        }
        unsigned char *img = malloc(w*h*4);
        if (img == NULL) {
            printf("Could not alloc image buffer.\n");
        }
        nsvgRasterize(rast, g_image, 0, 0, 1, img, w, h, w*4);
        stbi_write_png("svg.png", w, h, 4, img, w*4);
        nsvgDeleteRasterizer(rast);*/
    }
#endif

#ifdef EMSCRIPTEN
    vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
#else
    vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
#endif

#ifdef EMSCRIPTEN
    if (g_main_script)
    {
        EM_ASM_({
            var src = Pointer_stringify($0);
            Runtime.loadDynamicLibrarySrc(src);
        }, g_main_script);
    }
    if (g_main_script)
        onInit();
    glfwSetTime(0);
    emscripten_set_main_loop(drawframe, 60, 1);
#else
    if (onInit)
        onInit();
    glfwSetTime(0);
    while (!glfwWindowShouldClose(window))
    {
        drawframe();
    }
#endif
#ifdef EMSCRIPTEN
    nvgDeleteGLES2(vg);
#else
    nvgDeleteGL2(vg);
#endif
    zip_close(g_zip);

    glfwTerminate();
    return 0;
}
