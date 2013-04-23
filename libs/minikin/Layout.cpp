/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <vector>
#include <fstream>
#include <iostream>  // for debugging

#include <minikin/Layout.h>

using std::string;
using std::vector;

namespace android {

// TODO: globals are not cool, move to a factory-ish object
hb_buffer_t* buffer = 0;

Bitmap::Bitmap(int width, int height) : width(width), height(height) {
    buf = new uint8_t[width * height]();
}

Bitmap::~Bitmap() {
    delete[] buf;
}

void Bitmap::writePnm(std::ofstream &o) const {
    o << "P5" << std::endl;
    o << width << " " << height << std::endl;
    o << "255" << std::endl;
    o.write((const char *)buf, width * height);
    o.close();
}

void Bitmap::drawGlyph(const FT_Bitmap& bitmap, int x, int y) {
    int bmw = bitmap.width;
    int bmh = bitmap.rows;
    int x0 = std::max(0, x);
    int x1 = std::min(width, x + bmw);
    int y0 = std::max(0, y);
    int y1 = std::min(height, y + bmh);
    const unsigned char* src = bitmap.buffer + (y0 - y) * bmw + (x0 - x);
    uint8_t* dst = buf + y0 * width;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            int pixel = (int)dst[xx] + (int)src[xx - x];
            pixel = pixel > 0xff ? 0xff : pixel;
            dst[xx] = pixel;
        }
        src += bmw;
        dst += width;
    }
}

void Layout::init() {
    buffer = hb_buffer_create();
}

void Layout::setFontCollection(const FontCollection *collection) {
    mCollection = collection;
}

hb_blob_t* referenceTable(hb_face_t* face, hb_tag_t tag, void* userData)  {
    FT_Face ftFace = reinterpret_cast<FT_Face>(userData);
    FT_ULong length = 0;
    FT_Error error = FT_Load_Sfnt_Table(ftFace, tag, 0, NULL, &length);
    if (error) {
        return 0;
    }
    char *buffer = reinterpret_cast<char*>(malloc(length));
    if (!buffer) {
        return 0;
    }
    error = FT_Load_Sfnt_Table(ftFace, tag, 0,
        reinterpret_cast<FT_Byte*>(buffer), &length);
    if (error) {
        free(buffer);
        return 0;
    }
    return hb_blob_create(const_cast<char*>(buffer), length,
        HB_MEMORY_MODE_WRITABLE, buffer, free);
}

static hb_bool_t harfbuzzGetGlyph(hb_font_t* hbFont, void* fontData, hb_codepoint_t unicode, hb_codepoint_t variationSelector, hb_codepoint_t* glyph, void* userData)
{
    FT_Face ftFace = reinterpret_cast<FT_Face>(fontData);
    FT_UInt glyph_index = FT_Get_Char_Index(ftFace, unicode);
    *glyph = glyph_index;
    return !!*glyph;
}

static hb_position_t ft_pos_to_hb(FT_Pos pos) {
    return pos << 2;
}

static hb_position_t harfbuzzGetGlyphHorizontalAdvance(hb_font_t* hbFont, void* fontData, hb_codepoint_t glyph, void* userData)
{
    FT_Face ftFace = reinterpret_cast<FT_Face>(fontData);
    hb_position_t advance = 0;

    FT_Load_Glyph(ftFace, glyph, FT_LOAD_DEFAULT);
    return ft_pos_to_hb(ftFace->glyph->advance.x);
}

static hb_bool_t harfbuzzGetGlyphHorizontalOrigin(hb_font_t* hbFont, void* fontData, hb_codepoint_t glyph, hb_position_t* x, hb_position_t* y, void* userData)
{
    // Just return true, following the way that Harfbuzz-FreeType
    // implementation does.
    return true;
}

hb_font_funcs_t* getHbFontFuncs() {
    static hb_font_funcs_t* hbFontFuncs = 0;

    if (hbFontFuncs == 0) {
        hbFontFuncs = hb_font_funcs_create();
        hb_font_funcs_set_glyph_func(hbFontFuncs, harfbuzzGetGlyph, 0, 0);
        hb_font_funcs_set_glyph_h_advance_func(hbFontFuncs, harfbuzzGetGlyphHorizontalAdvance, 0, 0);
        hb_font_funcs_set_glyph_h_origin_func(hbFontFuncs, harfbuzzGetGlyphHorizontalOrigin, 0, 0);
        hb_font_funcs_make_immutable(hbFontFuncs);
    }
    return hbFontFuncs;
}

hb_font_t* create_hb_font(FT_Face ftFace) {
    hb_face_t* face = hb_face_create_for_tables(referenceTable, ftFace, NULL);
    hb_font_t* font = hb_font_create(face);
    hb_font_set_funcs(font, getHbFontFuncs(), ftFace, 0);
    // TODO: manage ownership of face
    return font;
}

static float HBFixedToFloat(hb_position_t v)
{
    return scalbnf (v, -8);
}

static hb_position_t HBFloatToFixed(float v)
{
    return scalbnf (v, +8);
}

void Layout::dump() const {
    for (size_t i = 0; i < mGlyphs.size(); i++) {
        const LayoutGlyph& glyph = mGlyphs[i];
        std::cout << glyph.glyph_id << ": " << glyph.x << ", " << glyph.y << std::endl;
    }
}

// A couple of things probably need to change:
// 1. Deal with multiple sizes in a layout
// 2. We'll probably store FT_Face as primary and then use a cache
// for the hb fonts
int Layout::findFace(FT_Face face) {
    unsigned int ix;
    for (ix = 0; ix < mFaces.size(); ix++) {
        if (mFaces[ix] == face) {
            return ix;
        }
    }
    double size = mProps.value(fontSize).getFloatValue();
    FT_Error error = FT_Set_Pixel_Sizes(face, 0, size);
    mFaces.push_back(face);
    hb_font_t *font = create_hb_font(face);
    hb_font_set_ppem(font, size, size);
    hb_font_set_scale(font, HBFloatToFixed(size), HBFloatToFixed(size));
    mHbFonts.push_back(font);
    return ix;
}

static FontStyle styleFromCss(const CssProperties &props) {
    int weight = 4;
    if (props.hasTag(fontWeight)) {
        weight = props.value(fontWeight).getIntValue() / 100;
    }
    bool italic = false;
    if (props.hasTag(fontStyle)) {
        italic = props.value(fontStyle).getIntValue() != 0;
    }
    // TODO: italic property from CSS
    return FontStyle(weight, italic);
}

// TODO: API should probably take context
void Layout::doLayout(const uint16_t* buf, size_t nchars) {
    FT_Error error;

    vector<FontCollection::Run> items;
    FontStyle style = styleFromCss(mProps);
    mCollection->itemize(buf, nchars, style, &items);

    mGlyphs.clear();
    mFaces.clear();
    mHbFonts.clear();
    float x = 0;
    float y = 0;
    for (size_t run_ix = 0; run_ix < items.size(); run_ix++) {
        FontCollection::Run &run = items[run_ix];
        int font_ix = findFace(run.font);
        hb_font_t *hbFont = mHbFonts[font_ix];
#ifdef VERBOSE
        std::cout << "Run " << run_ix << ", font " << font_ix <<
            " [" << run.start << ":" << run.end << "]" << std::endl;
#endif

        hb_buffer_reset(buffer);
        hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
        hb_buffer_add_utf16(buffer, buf, nchars, run.start, run.end - run.start);
        hb_shape(hbFont, buffer, NULL, 0);
        unsigned int numGlyphs;
        hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &numGlyphs);
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, NULL);
        for (unsigned int i = 0; i < numGlyphs; i++) {
#ifdef VERBOSE
            std::cout << positions[i].x_advance << " " << positions[i].y_advance << " " << positions[i].x_offset << " " << positions[i].y_offset << std::endl;            std::cout << "DoLayout " << info[i].codepoint <<
            ": " << HBFixedToFloat(positions[i].x_advance) << "; " << positions[i].x_offset << ", " << positions[i].y_offset << std::endl;
#endif
            hb_codepoint_t glyph_ix = info[i].codepoint;
            float xoff = HBFixedToFloat(positions[i].x_offset);
            float yoff = HBFixedToFloat(positions[i].y_offset);
            LayoutGlyph glyph = {font_ix, glyph_ix, x + xoff, y + yoff};
            mGlyphs.push_back(glyph);
            x += HBFixedToFloat(positions[i].x_advance);
        }
    }
}

void Layout::draw(Bitmap* surface, int x0, int y0) const {
    FT_Error error;
    FT_Int32 load_flags = FT_LOAD_DEFAULT;
    if (mProps.hasTag(minikinHinting)) {
        int hintflags = mProps.value(minikinHinting).getIntValue();
        if (hintflags & 1) load_flags |= FT_LOAD_NO_HINTING;
        if (hintflags & 2) load_flags |= FT_LOAD_NO_AUTOHINT;
    }
    for (size_t i = 0; i < mGlyphs.size(); i++) {
        const LayoutGlyph& glyph = mGlyphs[i];
        FT_Face face = mFaces[glyph.font_ix];
        error = FT_Load_Glyph(face, glyph.glyph_id, load_flags);
        error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        surface->drawGlyph(face->glyph->bitmap,
            x0 + int(floor(glyph.x + 0.5)) + face->glyph->bitmap_left,
            y0 + int(floor(glyph.y + 0.5)) - face->glyph->bitmap_top);
    }
}

void Layout::setProperties(string css) {
    mProps.parse(css);
}

}  // namespace android