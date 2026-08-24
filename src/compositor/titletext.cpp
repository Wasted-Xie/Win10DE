#include "compositor/titletext.h"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <cstring>
#include <drm_fourcc.h>

namespace w10de {

namespace {

void titleTextBufferDestroy(struct wlr_buffer* base) {
    TitleTextBuffer* b = wl_container_of(base, b, base);
    wlr_buffer_finish(base);
    delete[] b->pixels;
    delete b;
}

bool titleTextBeginDataPtrAccess(struct wlr_buffer* base, uint32_t /*flags*/,
                                 void** data, uint32_t* format, size_t* stride) {
    TitleTextBuffer* b = wl_container_of(base, b, base);
    *data = b->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = b->stride;
    return true;
}

void titleTextEndDataPtrAccess(struct wlr_buffer* /*base*/) {}

const struct wlr_buffer_impl kTitleTextBufferImpl = {
    .destroy = titleTextBufferDestroy,
    .begin_data_ptr_access = titleTextBeginDataPtrAccess,
    .end_data_ptr_access = titleTextEndDataPtrAccess,
};

}  // namespace

TitleTextBuffer* renderTitleText(const char* text, int width, int height,
                                 const float textColor[3]) {
    if (text == nullptr || *text == '\0' || width <= 0 || height <= 0) {
        return nullptr;
    }
    auto* buf = new TitleTextBuffer();
    buf->stride = static_cast<size_t>(width) * 4;
    buf->pixels = new uint8_t[buf->stride * static_cast<size_t>(height)];

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        buf->pixels, CAIRO_FORMAT_ARGB32, width, height,
        static_cast<int>(buf->stride));
    cairo_t* cr = cairo_create(surface);

    // 透明背景（标题栏底色由 scene 的 titleBarRect_ 绘制）。
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    // pango：单行、靠左、垂直居中、超长省略。
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_new();
    pango_font_description_set_family(font, "sans");
    pango_font_description_set_size(font, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    // 必须设置布局宽度才会触发 ellipsize（否则 pango 不省略、文字溢出
    // buffer 右缘被 cairo 硬裁剪，无 "…" 省略号）。
    pango_layout_set_width(layout, width * PANGO_SCALE);
    // 单段落模式：标题内的换行符不换行（防画出 buffer 下缘）。
    pango_layout_set_single_paragraph_mode(layout, true);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    PangoRectangle ink, logical;
    pango_layout_get_extents(layout, &ink, &logical);
    const int textX = 0;
    int textY = (height - logical.height / PANGO_SCALE) / 2;
    if (textY < 0) {
        textY = 0;
    }

    // ARGB32 预乘：不透明文字颜色经预乘不变。
    cairo_set_source_rgb(cr, textColor[0], textColor[1], textColor[2]);
    cairo_move_to(cr, textX, textY);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    pango_font_description_free(font);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wlr_buffer_init(&buf->base, &kTitleTextBufferImpl, width, height);
    return buf;
}

}  // namespace w10de
