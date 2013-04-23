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

#ifndef MINIKIN_LAYOUT_H
#define MINIKIN_LAYOUT_H

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

#include <vector>

#include <minikin/CssParse.h>
#include <minikin/FontCollection.h>

namespace android {

// The Bitmap class is for debugging. We'll probably move it out
// of here into a separate lightweight software rendering module
// (optional, as we'd hope most clients would do their own)
class Bitmap {
public:
    Bitmap(int width, int height);
    ~Bitmap();
    void writePnm(std::ofstream& o) const;
    void drawGlyph(const FT_Bitmap& bitmap, int x, int y);
private:
    int width;
    int height;
    uint8_t* buf;
};

struct LayoutGlyph {
    // index into mFaces and mHbFonts vectors. We could imagine
    // moving this into a run length representation, because it's
    // more efficient for long strings, and we'll probably need
    // something like that for paint attributes (color, underline,
    // fake b/i, etc), as having those per-glyph is bloated.
    int font_ix;

    unsigned int glyph_id;
    float x;
    float y;
};

class Layout {
public:
    void dump() const;
    void setFontCollection(const FontCollection *collection);
    void doLayout(const uint16_t* buf, size_t nchars);
    void draw(Bitmap*, int x0, int y0) const;
    void setProperties(const std::string css);

    // This must be called before any invocations.
	// TODO: probably have a factory instead
    static void init();
private:
    // Find a face in the mFaces vector, or create a new entry
    int findFace(FT_Face face);

    CssProperties mProps;  // TODO: want spans
    std::vector<LayoutGlyph> mGlyphs;

    // In future, this will be some kind of mapping from the
    // identifier used to represent font-family to a font collection.
    // But for the time being, it should be ok to have just one
    // per layout.
    const FontCollection *mCollection;
    std::vector<FT_Face> mFaces;
    std::vector<hb_font_t *> mHbFonts;
};

}  // namespace android

#endif  // MINIKIN_LAYOUT_H