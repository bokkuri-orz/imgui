/*
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 This permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/
#include "imguicodeeditor.h"

#define IMGUICODEEDITOR_USE_UTF8HELPER_H    // speed opt ?
#ifdef IMGUICODEEDITOR_USE_UTF8HELPER_H
#   include "utf8helper.h"         // not sure if it's necessary to count UTF8 chars
#endif //IMGUICODEEDITOR_USE_UTF8HELPER_H

#define IMGUI_NEW(type)         new (ImGui::MemAlloc(sizeof(type) ) ) type
#define IMGUI_DELETE(type, obj) reinterpret_cast<type*>(obj)->~type(), ImGui::MemFree(obj)


namespace ImGui {

// Extensions to ImDrawList:
static void ImDrawListPathFillAndStroke(ImDrawList* dl,const ImU32& fillColor,const ImU32& strokeColor,bool strokeClosed=false, float strokeThickness = 1.0f, bool antiAliased = true)    {
    if (!dl) return;
    if ((fillColor >> 24) != 0) dl->AddConvexPolyFilled(dl->_Path.Data, dl->_Path.Size, fillColor, antiAliased);
    if ((strokeColor>> 24)!= 0 && strokeThickness>0) dl->AddPolyline(dl->_Path.Data, dl->_Path.Size, strokeColor, strokeClosed, strokeThickness, antiAliased);
    dl->PathClear();
}
static void ImDrawListAddRect(ImDrawList* dl,const ImVec2& a, const ImVec2& b,const ImU32& fillColor,const ImU32& strokeColor,float rounding = 0.0f, int rounding_corners = 0x0F,float strokeThickness = 1.0f,bool antiAliased = true) {
    if (!dl || (((fillColor >> 24) == 0) && ((strokeColor >> 24) == 0)))  return;
    //dl->AddRectFilled(a,b,fillColor,rounding,rounding_corners);
    //dl->AddRect(a,b,strokeColor,rounding,rounding_corners);
    dl->PathRect(a, b, rounding, rounding_corners);
    ImDrawListPathFillAndStroke(dl,fillColor,strokeColor,true,strokeThickness,antiAliased);
}
static void ImDrawListPathArcTo(ImDrawList* dl,const ImVec2& centre,const ImVec2& radii, float amin, float amax, int num_segments = 10)  {
    if (!dl) return;
    if (radii.x == 0.0f || radii.y==0) dl->_Path.push_back(centre);
    dl->_Path.reserve(dl->_Path.Size + (num_segments + 1));
    for (int i = 0; i <= num_segments; i++)
    {
        const float a = amin + ((float)i / (float)num_segments) * (amax - amin);
        dl->_Path.push_back(ImVec2(centre.x + cosf(a) * radii.x, centre.y + sinf(a) * radii.y));
    }
}
static void ImDrawListAddEllipse(ImDrawList* dl,const ImVec2& centre, const ImVec2& radii,const ImU32& fillColor,const ImU32& strokeColor,int num_segments = 12,float strokeThickness = 1.f,bool antiAliased = true)   {
    if (!dl) return;
    const float a_max = IM_PI*2.0f * ((float)num_segments - 1.0f) / (float)num_segments;
    ImDrawListPathArcTo(dl,centre, radii, 0.0f, a_max, num_segments);
    ImDrawListPathFillAndStroke(dl,fillColor,strokeColor,true,strokeThickness,antiAliased);
}
static void ImDrawListAddCircle(ImDrawList* dl,const ImVec2& centre, float radius,const ImU32& fillColor,const ImU32& strokeColor,int num_segments = 12,float strokeThickness = 1.f,bool antiAliased = true)   {
    if (!dl) return;
    const ImVec2 radii(radius,radius);
    const float a_max = IM_PI*2.0f * ((float)num_segments - 1.0f) / (float)num_segments;
    ImDrawListPathArcTo(dl,centre, radii, 0.0f, a_max, num_segments);
    ImDrawListPathFillAndStroke(dl,fillColor,strokeColor,true,strokeThickness,antiAliased);
}


static inline int CountUTF8Chars(const char* text_begin, const char* text_end=NULL)   {
#   ifndef IMGUICODEEDITOR_USE_UTF8HELPER_H
    if (!text_end) text_end = text_begin + strlen(text_begin); // FIXME-OPT: Need to avoid this.
    int cnt = 0;const char* s = text_begin;unsigned int c = 0;
    while (s < text_end)    {
        // Decode and advance source
        c = (unsigned int)*s;
        if (c < 0x80)   {s += 1;++cnt;}
        else    {
            s += ImTextCharFromUtf8(&c, s, text_end);           // probably slower than UTF8Helper::decode(...)
            if (c == 0) break;  // Mmmh, not sure about this
            ++cnt;
        }
    }
    return cnt;
#   else // IMGUICODEEDITOR_USE_UTF8HELPER_H
    return UTF8Helper::CountUTF8Chars(text_begin,text_end); // This should be much faster (because UTF8Helper::decode() should be faster than ImTextCharFromUtf8()), but UTF8Helper also checks if a string is malformed, so I don't know.
#   endif //IMGUICODEEDITOR_USE_UTF8HELPER_H
}

static inline float MyCalcTextWidthA(ImFont* font,float size, const char* text_begin, const char* text_end, const char** remaining,int *pNumUTF8CharsOut=NULL,bool cancelOutCharacterSpacingForTheLastCharacterOfALine=false)  {
    // Warning: *pNumUTF8CharsOut must be set to zero by the caller
    const float scale = size / font->FontSize;
    float text_width = 0.f;int numUTF8Chars = 0;
//#   define NO_IMGUICODEEDITOR_USE_OPT_FOR_MONOSPACE_FONTS
#   ifdef NO_IMGUICODEEDITOR_USE_OPT_FOR_MONOSPACE_FONTS
    if (!text_end) text_end = text_begin + strlen(text_begin); // FIXME-OPT: Need to avoid this.
    const char* s = text_begin;
    while (s < text_end)    {
        // Decode and advance source
        unsigned int c = (unsigned int)*s;
        if (c < 0x80)   {s += 1;++numUTF8Chars;}
        else    {
            s += ImTextCharFromUtf8(&c, s, text_end);
            if (c == 0) break;
            ++numUTF8Chars;
        }

        text_width += ((int)c < font->IndexXAdvance.Size ? font->IndexXAdvance[(int)c] : font->FallbackXAdvance) * scale;
    }
    if (remaining)  *remaining = s;
#   else //NO_IMGUICODEEDITOR_USE_OPT_FOR_MONOSPACE_FONTS
#       ifndef IMGUICODEEDITOR_USE_UTF8HELPER_H
    // Actually this does not work corrently with TABS, because TABS have a different width even in MONOSPACE fonts (TO FIX)
    IM_ASSERT(remaining==NULL); // this arg is not currently supported by this opt
    numUTF8Chars = ImGui::CountUTF8Chars(text_begin,text_end);
    text_width = (font->FallbackXAdvance * scale) * numUTF8Chars;   // We use font->FallbackXAdvance for all (we could have used font->IndexXAdvance[0])
#       else //IMGUICODEEDITOR_USE_UTF8HELPER_H
    if (!text_end) text_end = text_begin + strlen(text_begin); // FIXME-OPT: Need to avoid this.
    const char* s = text_begin;int codepoint;int state = 0;const unsigned int tab = (unsigned int)'\t';
    int numTabs = 0;
    for (numUTF8Chars = 0; s!=text_end; ++s)    {
        if (UTF8Helper::decode(&state, &codepoint, *s)==UTF8Helper::UTF8_ACCEPT) {
            ++numUTF8Chars;
            if (codepoint == tab) ++numTabs;
        }
    }
    text_width = scale * (font->FallbackXAdvance * (numUTF8Chars-numTabs) + font->IndexXAdvance[tab] * numTabs);
    if (remaining)  *remaining = s;
#       endif //IMGUICODEEDITOR_USE_UTF8HELPER_H
#   endif //NO_IMGUICODEEDITOR_USE_OPT_FOR_MONOSPACE_FONTS
    if (pNumUTF8CharsOut) *pNumUTF8CharsOut+=numUTF8Chars;
    if (cancelOutCharacterSpacingForTheLastCharacterOfALine && text_width > 0.0f) text_width -= scale;
    return text_width;
}
static inline float MyCalcTextWidth(const char *text, const char *text_end=NULL, int *pNumUTF8CharsOut=NULL)    {
    // Warning: *pNumUTF8CharsOut must be set to zero by the caller
    return MyCalcTextWidthA(GImGui->Font,GImGui->FontSize,text,text_end,NULL,pNumUTF8CharsOut,true);
}


// Basically these 3 methods are similiar to the ones in ImFont or ImDrawList classes, but:
// -> specialized for text lines (no '\n' and '\r' chars).
// -> furthermore, now "pos" is taken by reference, because before I kept calling: font->AddText(...,pos,text,...);pos+=ImGui::CalcTextSize(text);
//      Now that is done in a single call.
// TODO:    remove all this garbage and just use plain ImGui methods and call ImGui::GetCursorPosX() instead of ImGui::CalcTextSize(...) every time.
//          [I won't get far in this addon if I keep adding useless code...]
// TODO: Do the same for utf8helper.h. Just remove it. I DON'T MIND if it will be slower, the code must be clean and ordered, not fast.
static inline void ImDrawListRenderTextLine(ImDrawList* draw_list,const ImFont* font,float size, ImVec2& pos, ImU32 col, const ImVec4& clip_rect, const char* text_begin, const char* text_end, bool cpu_fine_clip)
{
    if (!text_end) text_end = text_begin + strlen(text_begin);

    if ((int)pos.y > clip_rect.w) {
        pos.x+= (int)pos.x + MyCalcTextWidth(text_begin,text_end);
        return;
    }

    // Align to be pixel perfect
    pos.x = (float)(int)pos.x + font->DisplayOffset.x;
    pos.y = (float)(int)pos.y + font->DisplayOffset.y;
    float& x = pos.x;
    float& y = pos.y;

    const float scale = size / font->FontSize;
    const float line_height = font->FontSize * scale;

    ImDrawVert* vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx* idx_write = draw_list->_IdxWritePtr;
    unsigned int vtx_current_idx = draw_list->_VtxCurrentIdx;

    const char* s = text_begin;
    if (y + line_height < clip_rect.y) while (s < text_end && *s != '\n')  s++;// Fast-forward to next line

    while (s < text_end)
    {
        // Decode and advance source
        unsigned int c = (unsigned int)*s;
        if (c < 0x80)   {s += 1;}
        else    {
            s += ImTextCharFromUtf8(&c, s, text_end);
            if (c == 0) break;
        }

        float char_width = 0.0f;
        if (const ImFont::Glyph* glyph = font->FindGlyph((unsigned short)c))
        {
            char_width = glyph->XAdvance * scale;

            // Clipping on Y is more likely
            if (c != ' ' && c != '\t')
            {
                // We don't do a second finer clipping test on the Y axis (TODO: do some measurement see if it is worth it, probably not)
                float y1 = (float)(y + glyph->Y0 * scale);
                float y2 = (float)(y + glyph->Y1 * scale);

                float x1 = (float)(x + glyph->X0 * scale);
                float x2 = (float)(x + glyph->X1 * scale);
                if (x1 <= clip_rect.z && x2 >= clip_rect.x)
                {
                    // Render a character
                    float u1 = glyph->U0;
                    float v1 = glyph->V0;
                    float u2 = glyph->U1;
                    float v2 = glyph->V1;

                    // CPU side clipping used to fit text in their frame when the frame is too small. Only does clipping for axis aligned quads.
                    if (cpu_fine_clip)
                    {
                        if (x1 < clip_rect.x)
                        {
                            u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                            x1 = clip_rect.x;
                        }
                        if (y1 < clip_rect.y)
                        {
                            v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                            y1 = clip_rect.y;
                        }
                        if (x2 > clip_rect.z)
                        {
                            u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                            x2 = clip_rect.z;
                        }
                        if (y2 > clip_rect.w)
                        {
                            v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                            y2 = clip_rect.w;
                        }
                        if (y1 >= y2)
                        {
                            x += char_width;
                            continue;
                        }
                    }

                    // NB: we are not calling PrimRectUV() here because non-inlined causes too much overhead in a debug build.
                    // inlined:
                    {
                        idx_write[0] = (ImDrawIdx)(vtx_current_idx); idx_write[1] = (ImDrawIdx)(vtx_current_idx+1); idx_write[2] = (ImDrawIdx)(vtx_current_idx+2);
                        idx_write[3] = (ImDrawIdx)(vtx_current_idx); idx_write[4] = (ImDrawIdx)(vtx_current_idx+2); idx_write[5] = (ImDrawIdx)(vtx_current_idx+3);
                        vtx_write[0].pos.x = x1; vtx_write[0].pos.y = y1; vtx_write[0].col = col; vtx_write[0].uv.x = u1; vtx_write[0].uv.y = v1;
                        vtx_write[1].pos.x = x2; vtx_write[1].pos.y = y1; vtx_write[1].col = col; vtx_write[1].uv.x = u2; vtx_write[1].uv.y = v1;
                        vtx_write[2].pos.x = x2; vtx_write[2].pos.y = y2; vtx_write[2].col = col; vtx_write[2].uv.x = u2; vtx_write[2].uv.y = v2;
                        vtx_write[3].pos.x = x1; vtx_write[3].pos.y = y2; vtx_write[3].col = col; vtx_write[3].uv.x = u1; vtx_write[3].uv.y = v2;
                        vtx_write += 4;
                        vtx_current_idx += 4;
                        idx_write += 6;
                    }
                }
            }
        }

        x += char_width;
    }

    draw_list->_VtxWritePtr = vtx_write;
    draw_list->_VtxCurrentIdx = vtx_current_idx;
    draw_list->_IdxWritePtr = idx_write;

    // restore pos
    pos.x -= font->DisplayOffset.x;
    pos.y -= font->DisplayOffset.y;

}
static inline void ImDrawListAddTextLine(ImDrawList* draw_list,const ImFont* font, float font_size, ImVec2& pos, ImU32 col, const char* text_begin, const char* text_end, const ImVec4* cpu_fine_clip_rect = NULL)
{
    if (text_end == NULL)   text_end = text_begin + strlen(text_begin);
    if ((col >> 24) == 0)   {
        pos.x+= (int)pos.x + MyCalcTextWidth(text_begin,text_end);
        return;
    }
    if (text_begin == text_end) return;

    IM_ASSERT(font->ContainerAtlas->TexID == draw_list->_TextureIdStack.back());  // Use high-level ImGui::PushFont() or low-level ImDrawList::PushTextureId() to change font.

    // reserve vertices for worse case (over-reserving is useful and easily amortized)
    const int char_count = (int)(text_end - text_begin);
    const int vtx_count_max = char_count * 4;
    const int idx_count_max = char_count * 6;
    const int vtx_begin = draw_list->VtxBuffer.Size;
    const int idx_begin = draw_list->IdxBuffer.Size;
    draw_list->PrimReserve(idx_count_max, vtx_count_max);

    ImVec4 clip_rect = draw_list->_ClipRectStack.back();
    if (cpu_fine_clip_rect) {
        clip_rect.x = ImMax(clip_rect.x, cpu_fine_clip_rect->x);
        clip_rect.y = ImMax(clip_rect.y, cpu_fine_clip_rect->y);
        clip_rect.z = ImMin(clip_rect.z, cpu_fine_clip_rect->z);
        clip_rect.w = ImMin(clip_rect.w, cpu_fine_clip_rect->w);
    }
    ImDrawListRenderTextLine(draw_list,font,font_size, pos, col, clip_rect, text_begin, text_end, cpu_fine_clip_rect != NULL);

    // give back unused vertices
    // FIXME-OPT: clean this up
    draw_list->VtxBuffer.resize((int)(draw_list->_VtxWritePtr - draw_list->VtxBuffer.Data));
    draw_list->IdxBuffer.resize((int)(draw_list->_IdxWritePtr - draw_list->IdxBuffer.Data));
    int vtx_unused = vtx_count_max - (draw_list->VtxBuffer.Size - vtx_begin);
    int idx_unused = idx_count_max - (draw_list->IdxBuffer.Size - idx_begin);
    draw_list->CmdBuffer.back().ElemCount -= idx_unused;
    draw_list->_VtxWritePtr -= vtx_unused;
    draw_list->_IdxWritePtr -= idx_unused;
    draw_list->_VtxCurrentIdx = (ImDrawIdx)draw_list->VtxBuffer.Size;
}
static inline void ImDrawListAddTextLine(ImDrawList* draw_list,ImVec2& pos, ImU32 col, const char* text_begin, const char* text_end=NULL)   {
    ImDrawListAddTextLine(draw_list,GImGui->Font, GImGui->FontSize, pos, col, text_begin, text_end);
}

} // namespace ImGui

namespace ImGuiCe   {

// Static stuff
CodeEditor::Style CodeEditor::style;  // static variable initialization
inline static bool EditColorImU32(const char* label,ImU32& color) {
    static ImVec4 tmp;
    tmp = ImColor(color);
    const bool changed = ImGui::ColorEdit4(label,&tmp.x);
    if (changed) color = ImColor(tmp);
    return changed;
}
static const char* FontStyleStrings[FONT_STYLE_COUNT] = {"NORMAL","BOLD","ITALIC","BOLD_ITALIC"};
static const char* SyntaxHighlightingTypeStrings[SH_COUNT] = {"SH_KEYWORD_ACCESS","SH_KEYWORD_CONSTANT","SH_KEYWORD_CONTEXT","SH_KEYWORD_DECLARATION","SH_KEYWORD_EXCEPTION","SH_KEYWORD_ITERATION","SH_KEYWORD_JUMP","SH_KEYWORD_KEYWORD_USER1","SH_KEYWORD_KEYWORD_USER2","SH_KEYWORD_METHOD","SH_KEYWORD_MODIFIER","SH_KEYWORD_NAMESPACE","SH_KEYWORD_OPERATOR","SH_KEYWORD_OTHER","SH_KEYWORD_PARAMENTER","SH_KEYWORD_PREPROCESSOR","SH_KEYWORD_PROPERTY","SH_KEYWORD_SELECTION","SH_KEYWORD_TYPE","SH_LOGICAL_OPERATORS","SH_MATH_OPERATORS","SH_BRACKETS_CURLY","SH_BRACKETS_SQUARE","SH_BRACKETS_ROUND","SH_PUNCTUATION","SH_STRING","SH_NUMBER","SH_COMMENT","SH_FOLDED_PARENTHESIS","SH_FOLDED_COMMENT","SH_FOLDED_REGION"};
static const char* SyntaxHighlightingColorStrings[SH_COUNT] = {"COLOR_KEYWORD_ACCESS","COLOR_KEYWORD_CONSTANT","COLOR_KEYWORD_CONTEXT","COLOR_KEYWORD_DECLARATION","COLOR_KEYWORD_EXCEPTION","COLOR_KEYWORD_ITERATION","COLOR_KEYWORD_JUMP","COLOR_KEYWORD_KEYWORD_USER1","COLOR_KEYWORD_KEYWORD_USER2","COLOR_KEYWORD_METHOD","COLOR_KEYWORD_MODIFIER","COLOR_KEYWORD_NAMESPACE","COLOR_KEYWORD_OPERATOR","COLOR_KEYWORD_OTHER","COLOR_KEYWORD_PARAMENTER","COLOR_KEYWORD_PREPROCESSOR","COLOR_KEYWORD_PROPERTY","COLOR_KEYWORD_SELECTION","COLOR_KEYWORD_TYPE","COLOR_LOGICAL_OPERATORS","COLOR_MATH_OPERATORS","COLOR_BRACKETS_CURLY","COLOR_BRACKETS_SQUARE","COLOR_BRACKETS_ROUND","COLOR_PUNCTUATION","COLOR_STRING","COLOR_NUMBER","COLOR_COMMENT","COLOR_FOLDED_PARENTHESIS","COLOR_FOLDED_COMMENT","COLOR_FOLDED_REGION"};
static const char* SyntaxHighlightingFontStrings[SH_COUNT] = {"FONT_KEYWORD_ACCESS","FONT_KEYWORD_CONSTANT","FONT_KEYWORD_CONTEXT","FONT_KEYWORD_DECLARATION","FONT_KEYWORD_EXCEPTION","FONT_KEYWORD_ITERATION","FONT_KEYWORD_JUMP","FONT_KEYWORD_KEYWORD_USER1","FONT_KEYWORD_KEYWORD_USER2","FONT_KEYWORD_METHOD","FONT_KEYWORD_MODIFIER","FONT_KEYWORD_NAMESPACE","FONT_KEYWORD_OPERATOR","FONT_KEYWORD_OTHER","FONT_KEYWORD_PARAMENTER","FONT_KEYWORD_PREPROCESSOR","FONT_KEYWORD_PROPERTY","FONT_KEYWORD_SELECTION","FONT_KEYWORD_TYPE","FONT_LOGICAL_OPERATORS","FONT_MATH_OPERATORS","FONT_BRACKETS_CURLY","FONT_BRACKETS_SQUARE","FONT_BRACKETS_ROUND","FONT_PUNCTUATION","FONT_STRING","FONT_NUMBER","FONT_COMMENT","FONT_FOLDED_PARENTHESIS","FONT_FOLDED_COMMENT","FONT_FOLDED_REGION"};
CodeEditor::Style::Style() {
    color_background =          ImColor(40,40,50,255);
    color_text =                ImGui::GetStyle().Colors[ImGuiCol_Text];
    color_line_numbers =        ImVec4(color_text.x,color_text.y,color_text.z,0.25f);
    color_icon_margin_error =        ImColor(255,0,0);
    color_icon_margin_warning =      ImColor(255,255,0);
    color_icon_margin_breakpoint =   ImColor(255,190,0,225);
    color_icon_margin_bookmark =     ImColor(190,190,225);
    color_icon_margin_contour =      ImColor(50,50,150);
    icon_margin_contour_thickness =  1.f;
    font_text = font_line_numbers = FONT_STYLE_NORMAL;

    for (int i=0;i<SH_COUNT;i++)    {
        color_syntax_highlighting[i] = ImColor(color_text);
        font_syntax_highlighting[i] = FONT_STYLE_NORMAL;
    }

    color_folded_parenthesis_background =
            color_folded_comment_background =
            color_folded_region_background = ImColor(color_background.x,color_background.y,color_background.z,0.f);
    folded_region_contour_thickness = 1.f;

    // TODO: Make better colorSyntaxHighlighting and fontSyntaxHighlighting here
    font_syntax_highlighting[SH_FOLDED_PARENTHESIS] =
            font_syntax_highlighting[SH_FOLDED_COMMENT] =
            font_syntax_highlighting[SH_FOLDED_REGION] = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_COMMENT]               = ImColor(150,220,255,200);
    color_syntax_highlighting[SH_NUMBER]                = ImColor(255,200,220,255);
    color_syntax_highlighting[SH_STRING]                = ImColor(255,64,70,255);

    color_syntax_highlighting[SH_KEYWORD_PREPROCESSOR]  = ImColor(50,120,200,255);
    font_syntax_highlighting[ SH_KEYWORD_PREPROCESSOR]  = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_BRACKETS_CURLY]        = ImColor(240,240,255,255);
    font_syntax_highlighting[ SH_BRACKETS_CURLY]        = FONT_STYLE_BOLD;
    color_syntax_highlighting[SH_BRACKETS_SQUARE]       = ImColor(255,240,240,255);
    font_syntax_highlighting[ SH_BRACKETS_SQUARE]       = FONT_STYLE_BOLD;
    color_syntax_highlighting[SH_BRACKETS_ROUND]        = ImColor(240,255,240,255);
    //font_syntax_highlighting[ SH_BRACKETS_ROUND]        = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_TYPE]    = ImColor(220,220,40,255);
    font_syntax_highlighting[ SH_KEYWORD_TYPE]    = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_CONSTANT]    = ImColor(220,80,40,255);
    font_syntax_highlighting[ SH_KEYWORD_CONSTANT]    = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_CONTEXT]    = ImColor(120,200,40,255);
    font_syntax_highlighting[ SH_KEYWORD_CONTEXT]    = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_ACCESS]        = ImColor(220,150,40,255);
    font_syntax_highlighting[ SH_KEYWORD_ACCESS]        = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_MODIFIER]      = ImColor(220,150,40,255);
    font_syntax_highlighting[ SH_KEYWORD_MODIFIER]      = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_NAMESPACE]     = ImColor(40,250,40,255);
    font_syntax_highlighting[ SH_KEYWORD_NAMESPACE]     = FONT_STYLE_BOLD_ITALIC;

    color_syntax_highlighting[SH_KEYWORD_DECLARATION]   = ImColor(40,250,40,255);
    font_syntax_highlighting[ SH_KEYWORD_DECLARATION]   = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_ITERATION]      = ImColor(200,180,100,255);
    font_syntax_highlighting[ SH_KEYWORD_ITERATION]      = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_JUMP]          = ImColor(220,150,100,255);
    font_syntax_highlighting[ SH_KEYWORD_JUMP]          = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_SELECTION]      = ImColor(220,220,100,255);
    font_syntax_highlighting[ SH_KEYWORD_SELECTION]      = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_OPERATOR]      = ImColor(220,150,240,255);
    font_syntax_highlighting[ SH_KEYWORD_OPERATOR]      = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_KEYWORD_OTHER]      = ImColor(220,150,150,255);
    font_syntax_highlighting[ SH_KEYWORD_OTHER]      = FONT_STYLE_BOLD;

    color_syntax_highlighting[SH_MATH_OPERATORS]        = ImColor(255,150,100,255);
    //font_syntax_highlighting[ SH_MATH_OPERATORS]        = FONT_STYLE_BOLD;


    color_icon_margin_background = color_line_numbers_background = color_folding_margin_background
    = ImColor(color_background.x,color_background.y,color_background.z,0.f);

    color_syntax_highlighting[SH_FOLDED_COMMENT] = color_syntax_highlighting[SH_COMMENT];
    color_syntax_highlighting[SH_FOLDED_PARENTHESIS] = color_syntax_highlighting[SH_BRACKETS_CURLY];
    color_syntax_highlighting[SH_FOLDED_REGION] = ImColor(225,250,200,255);

}

bool CodeEditor::Style::Edit(CodeEditor::Style& s) {
    bool changed = false;
    const float dragSpeed = 0.5f;
    const char prec[] = "%1.1f";
    ImGui::PushID(&s);

    ImGui::Separator();
    ImGui::Text("Main");
    ImGui::Separator();
    ImGui::Spacing();
    changed|=ImGui::ColorEdit4( "background##color_background",&s.color_background.x);
    changed|=ImGui::ColorEdit4( "text##color_text",&s.color_text.x);
    changed|=ImGui::Combo("text##font_text",&s.font_text,&FontStyleStrings[0],FONT_STYLE_COUNT,-1);
    changed|=EditColorImU32( "line_numbers_bg##color_line_numbers_background",s.color_line_numbers_background);
    changed|=ImGui::ColorEdit4( "line_numbers##color_line_numbers",&s.color_line_numbers.x);
    changed|=ImGui::Combo("line_numbers##font_line_numbers",&s.font_line_numbers,&FontStyleStrings[0],FONT_STYLE_COUNT,-1);
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::Text("Margin");
    ImGui::Separator();
    ImGui::Spacing();
    changed|=EditColorImU32(    "error##color_margin_error",s.color_icon_margin_error);
    changed|=EditColorImU32(    "warning##color_margin_warning",s.color_icon_margin_warning);
    changed|=EditColorImU32(    "breakpoint##color_margin_breakpoint",s.color_icon_margin_breakpoint);
    changed|=EditColorImU32(    "bookmark##color_margin_bookmark",s.color_icon_margin_bookmark);
    changed|=EditColorImU32(    "contour##color_margin_contour",s.color_icon_margin_contour);
    changed|=ImGui::DragFloat(  "contour_width##margin_contour_thickness",&s.icon_margin_contour_thickness,dragSpeed,0.5f,5.f,prec);
    ImGui::Spacing();
    changed|=EditColorImU32( "icon_margin_bg##color_icon_margin_background",s.color_icon_margin_background);
    changed|=EditColorImU32( "folding_margin_bg##color_folding_margin_background",s.color_folding_margin_background);
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::Text("Syntax Highlighting");
    ImGui::Separator();
    ImGui::Spacing();
    static int item=0;
    ImGui::Combo("##Token##Syntax Highlighting Token",&item, &SyntaxHighlightingTypeStrings[0], SH_COUNT, -1);
    ImGui::Spacing();
    changed|=EditColorImU32("color##color_sh_token",s.color_syntax_highlighting[item]);
    changed|=ImGui::Combo("font##font_sh_token",&s.font_syntax_highlighting[item],&FontStyleStrings[0],FONT_STYLE_COUNT,-1);
    if (item == SH_FOLDED_PARENTHESIS) changed|=EditColorImU32("background##color_folded_parenthesis_background",s.color_folded_parenthesis_background);
    else if (item == SH_FOLDED_COMMENT)	changed|=EditColorImU32("background##color_folded_comment_background",s.color_folded_comment_background);
    else if (item == SH_FOLDED_REGION)	{
        changed|=EditColorImU32("background##color_folded_region_background",s.color_folded_region_background);
        changed|=ImGui::DragFloat(  "contour_width##folded_region_contour_thickness",&s.folded_region_contour_thickness,dragSpeed,0.5f,5.f,prec);
    }

    ImGui::Separator();

    ImGui::PopID();
    return changed;
}
#if (!defined(NO_IMGUIHELPER) && !defined(NO_IMGUIHELPER_SERIALIZATION))
#ifndef NO_IMGUIHELPER_SERIALIZATION_SAVE
#include "../imguihelper/imguihelper.h"
bool CodeEditor::Style::Save(const CodeEditor::Style &style, const char *filename)    {
    ImGuiHelper::Serializer s(filename);
    if (!s.isValid()) return false;

    ImVec4 tmpColor = ImColor(style.color_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_background",4);
    tmpColor = ImColor(style.color_text);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_text",4);
    s.save(&style.font_text,"font_text");
    tmpColor = ImColor(style.color_line_numbers_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_line_numbers_background",4);
    tmpColor = ImColor(style.color_line_numbers);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_line_numbers",4);
    s.save(&style.font_line_numbers,"font_line_numbers");
    tmpColor = ImColor(style.color_icon_margin_error);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_icon_margin_error",4);
    tmpColor = ImColor(style.color_icon_margin_warning);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_icon_margin_warning",4);
    tmpColor = ImColor(style.color_icon_margin_breakpoint);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_icon_margin_breakpoint",4);
    tmpColor = ImColor(style.color_icon_margin_bookmark);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_icon_margin_bookmark",4);
    tmpColor = ImColor(style.color_icon_margin_contour);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_icon_margin_contour",4);

    tmpColor = ImColor(style.color_icon_margin_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_icon_margin_background",4);
    tmpColor = ImColor(style.color_folding_margin_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_folding_margin_background",4);


    for (int i=0;i<SH_COUNT;i++)    {
        tmpColor = ImColor(style.color_syntax_highlighting[i]);s.save(ImGui::FT_COLOR,&tmpColor.x,SyntaxHighlightingColorStrings[i],4);
        s.save(ImGui::FT_ENUM,&style.font_syntax_highlighting[i],SyntaxHighlightingFontStrings[i]);
    }

    s.save(ImGui::FT_FLOAT,&style.icon_margin_contour_thickness,"icon_margin_contour_thickness");

    tmpColor = ImColor(style.color_folded_parenthesis_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_folded_parenthesis_background",4);
    tmpColor = ImColor(style.color_folded_comment_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_folded_comment_background",4);
    tmpColor = ImColor(style.color_folded_region_background);s.save(ImGui::FT_COLOR,&tmpColor.x,"color_folded_region_background",4);

    s.save(ImGui::FT_FLOAT,&style.folded_region_contour_thickness,"folded_region_contour_thickness");

    return true;
}
#endif //NO_IMGUIHELPER_SERIALIZATION_SAVE
#ifndef NO_IMGUIHELPER_SERIALIZATION_LOAD
#include "../imguihelper/imguihelper.h"
static bool StyleParser(ImGuiHelper::FieldType ft,int /*numArrayElements*/,void* pValue,const char* name,void* userPtr)    {
    CodeEditor::Style& s = *((CodeEditor::Style*) userPtr);
    ImVec4& tmp = *((ImVec4*) pValue);  // we cast it soon to float for now...    
    switch (ft) {
    case ImGui::FT_FLOAT:
        if (strcmp(name,"icon_margin_contour_thickness")==0)                s.icon_margin_contour_thickness = tmp.x;
    else if (strcmp(name,"folded_region_contour_thickness")==0)             s.folded_region_contour_thickness = tmp.x;
    //else if (strcmp(name,"grid_size")==0)                                 s.grid_size = tmp.x;
    break;
    case ImGui::FT_INT:
        //if (strcmp(name,"link_num_segments")==0)                          s.link_num_segments = *((int*)pValue);
    break;
    case ImGui::FT_ENUM:
        if (strcmp(name,"font_line_numbers")==0)   {
            int pi = *((int*) pValue);
            if (pi>=0 && pi<FONT_STYLE_COUNT) s.font_line_numbers = pi;
        }
        else if (strcmp(name,"font_text")==0)   {
            int pi = *((int*) pValue);
            if (pi>=0 && pi<FONT_STYLE_COUNT) s.font_text = pi;
        }
        else for (int i=0;i<SH_COUNT;i++) {
            if (strcmp(name,SyntaxHighlightingFontStrings[i])==0)           {
                int pi = *((int*) pValue);
                if (pi>=0 && pi<FONT_STYLE_COUNT) s.font_syntax_highlighting[i] = pi;
                break;
            }
        }
    break;
    case ImGui::FT_COLOR:
        if (strcmp(name,"color_background")==0)                             s.color_background = ImColor(tmp);
        else if (strcmp(name,"color_text")==0)                              s.color_text = ImColor(tmp);
        else if (strcmp(name,"color_line_numbers_background")==0)           s.color_line_numbers = ImColor(tmp);
        else if (strcmp(name,"color_line_numbers")==0)                      s.color_line_numbers = ImColor(tmp);
        else if (strcmp(name,"color_icon_margin_error")==0)                 s.color_icon_margin_error = ImColor(tmp);
        else if (strcmp(name,"color_icon_margin_warning")==0)               s.color_icon_margin_warning = ImColor(tmp);
        else if (strcmp(name,"color_icon_margin_breakpoint")==0)            s.color_icon_margin_breakpoint = ImColor(tmp);
        else if (strcmp(name,"color_icon_margin_bookmark")==0)              s.color_icon_margin_bookmark = ImColor(tmp);
        else if (strcmp(name,"color_icon_margin_contour")==0)               s.color_icon_margin_contour = ImColor(tmp);
        else if (strcmp(name,"color_folded_parenthesis_background")==0)     s.color_folded_parenthesis_background = ImColor(tmp);
        else if (strcmp(name,"color_folded_comment_background")==0)         s.color_folded_comment_background = ImColor(tmp);
        else if (strcmp(name,"color_folded_region_background")==0)          s.color_folded_region_background = ImColor(tmp);
        else if (strcmp(name,"color_icon_margin_background")==0)            s.color_icon_margin_background = ImColor(tmp);
        else if (strcmp(name,"color_folding_margin_background")==0)         s.color_folding_margin_background = ImColor(tmp);
        else {
            for (int i=0;i<SH_COUNT;i++) {
                if (strcmp(name,SyntaxHighlightingColorStrings[i])==0)      {s.color_syntax_highlighting[i] = ImColor(tmp);break;}
            }
        }
    break;
    default:
    // TODO: check
    break;
    }
    return false;
}
bool CodeEditor::Style::Load(CodeEditor::Style &style, const char *filename)  {
    ImGuiHelper::Deserializer d(filename);
    if (!d.isValid()) return false;
    d.parse(StyleParser,(void*)&style);
    return true;
}
#endif //NO_IMGUIHELPER_SERIALIZATION_LOAD
#endif //NO_IMGUIHELPER_SERIALIZATION


const ImFont* CodeEditor::ImFonts[FONT_STYLE_COUNT] = {NULL,NULL,NULL,NULL};

void CodeEditor::TextLineWithSHV(const char* fmt, va_list args) {
    if (ImGui::GetCurrentWindow()->SkipItems)  return;

//#define TEST_HERE
#ifndef TEST_HERE
    ImGuiState& g = *GImGui;
    const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);
    TextLineUnformattedWithSH(g.TempBuffer, text_end);
#else //TEST_HERE
    ImGuiState& g = *GImGui;
    const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);
    // Bg Color-------------------
    ImU32 bg_col = ImColor(255,255,255,50);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    // All the way to the right
    //ImGui::RenderFrame(pos, ImVec2(pos.x + ImGui::GetContentRegionMax().x+ImGui::GetScrollX(), pos.y + ImGui::GetTextLineHeight()), bg_col,false);
    // Until text end
    ImGui::RenderFrame(pos, ImVec2(pos.x + ImGui::MyCalcTextWidth(g.TempBuffer, text_end), pos.y + ImGui::GetTextLineHeight()), bg_col,false);
    //----------------------------
    TextLineUnformattedWithSH(g.TempBuffer, text_end);
#undef TEST_HERE
#endif //TEST_HERE
}
void CodeEditor::TextLineWithSH(const char* fmt, ...)   {
    va_list args;
    va_start(args, fmt);
    TextLineWithSHV(fmt, args);
    va_end(args);
}


struct Line {
    enum Attribute {
	AT_BOOKMARK             = 1,
	AT_BREAKPOINT           = 1<<1,
	AT_WARNING              = 1<<2,
	AT_ERROR                = 1<<3,
	AT_FOLDING_START        = 1<<4,
	AT_FOLDING_END          = 1<<5,
	AT_HIDDEN               = 1<<6,
	AT_SAVE_SPACE_LINE      = 1<<7,
	AT_FOLDING_START_FOLDED   = 1<<8
    };

    // Fixed data
    ImString text;
    int attributes;

    // Offst Data
    int lineNumber;		// zero based
    unsigned offset;            // in bytes, from the start of the document, without '\n' and '\r'
    unsigned offsetInUTF8chars; // in UTF8,  from the start of the document, without '\n' and '\r'
    int numUTF8chars;

    // Folding Data
    const FoldingTag* foldingStartTag;
    int foldingStartOffset;     // in bytes, used if AT_FOLDING_START,	from the start of the line to the first char of the "start folding" tag
    Line* foldingEndLine;	// reference used if AT_FOLDING_START
    int foldingEndOffset;       // in bytes, used if AT_FOLDING_END,	from the start of the line to the last  char of the "end   folding" tag
    Line* foldingStartLine;	// reference used if AT_FOLDING_END

    // Proposal: use "foldingStartLine" and "foldingEndLine" for all the lines inside a folding region too.
    // => DONE <=

    void reset() {text="";}
    int size() {return text.size();}
    Line(const ImString& txt="") : text(txt),attributes(0),
    lineNumber(-1),offset(0),offsetInUTF8chars(0),numUTF8chars(0),
    foldingStartTag(NULL),foldingStartOffset(0),foldingEndLine(NULL),
    foldingEndOffset(0),foldingStartLine(NULL) {}

    inline bool isFoldable() const {return (attributes&AT_FOLDING_START);}
    inline bool isFolded() const {return (attributes&AT_FOLDING_START) && (attributes&AT_FOLDING_START_FOLDED);}
    inline void setFoldedState(bool state) {if (!state) attributes&=~AT_FOLDING_START_FOLDED;else attributes|=AT_FOLDING_START_FOLDED;}
    inline bool isFoldingEnd() const {return (attributes&AT_FOLDING_END);}
    inline bool canFoldingBeMergedWithLineAbove() const {return (attributes&AT_FOLDING_START)&&(attributes&AT_SAVE_SPACE_LINE);}
    inline void resetFoldingAttributes() {
	attributes&=~(AT_FOLDING_START|AT_FOLDING_END|AT_SAVE_SPACE_LINE|AT_FOLDING_START_FOLDED|AT_HIDDEN);
	foldingStartOffset=foldingEndOffset=0;
	foldingStartTag = NULL;
	foldingStartLine = foldingEndLine = NULL;
    }
    inline void resetFoldingStartAttributes() {
	attributes&=~(AT_FOLDING_START|AT_SAVE_SPACE_LINE|AT_FOLDING_START_FOLDED|AT_HIDDEN);
	foldingStartOffset=0;
	foldingStartTag = NULL;
	foldingEndLine = NULL;
    }
    inline void resetFoldingEndAttributes() {
	attributes&=~(AT_FOLDING_END|AT_HIDDEN);
	foldingEndOffset=0;
	foldingStartLine = NULL;
    }
    inline static void AdjustHiddenFlagsForFoldingStart(Lines& lines,const Line& line) {
	if (!line.isFoldable() || !line.foldingEndLine) return;
	bool state = !line.isFolded();
	for (int i=line.lineNumber+1,isz=line.foldingEndLine->lineNumber;i<isz;i++) {
	    Line* ln = lines[i];
	    ln->setHidden(state);
	    if (ln->isFolded() && ln->foldingEndLine) i=ln->foldingEndLine->lineNumber-1;
	}
	if (line.foldingStartTag->kind==FOLDING_TYPE_REGION) line.foldingEndLine->setHidden(state);
    }
    inline bool isHidden() const {return attributes&AT_HIDDEN;}
    inline void setHidden(bool state)  {if (state) attributes&=~AT_HIDDEN;else attributes|=AT_HIDDEN;}
    friend class Lines;friend class FoldSegment;friend class FoldingString;
};


Line *Lines::add(int lineNum) {
    if (lineNum<0 || lineNum>size()) lineNum=size();
    resize(size()+1);
    for (int i=size()-1;i>lineNum;i--)    (*this)[i] = (*this)[i-1];
    Line* line = (*this)[lineNum] = IMGUI_NEW(Line);
    return line;
}

bool Lines::remove(int lineNum)    {
    if (lineNum<0 || lineNum>=size()) return false;
    Line* line = (*this)[lineNum];
    for (int i=lineNum,isz=size()-1;i<isz;i++)   (*this)[i] = (*this)[i+1];
    resize(size()-1);
    IMGUI_DELETE(Line,line);line=NULL;
    return true;
}

void Lines::destroy(bool keepFirstLine) {
    for (int i=size()-1;i>=0;i--)  {
        Line*& line = (*this)[i];
        IMGUI_DELETE(Line,line);line=NULL;
    }
    Base::clear();
    if (keepFirstLine) add();
}


void Lines::getText(ImString &rv, int startLineNum, int startLineOffsetBytes, int endLineNum, int endLineOffsetBytes) const {
    rv="";
    IM_ASSERT(size()>0);

    if (startLineNum<0) startLineNum=0;
    if (startLineOffsetBytes<0) startLineOffsetBytes=0;
    else if (startLineOffsetBytes>(*this)[startLineNum]->text.size()) {++startLineNum;startLineOffsetBytes=0;}
    if (startLineNum>=size()) return;
    if (endLineNum<0 || endLineNum>=size()) {endLineNum=size()-1;endLineOffsetBytes=(*this)[endLineNum]->text.size();}

    rv+=(*this)[startLineNum]->text.substr(startLineOffsetBytes);
    if (startLineNum!=endLineNum) rv+=cr;
    for (int ln=startLineNum+1;ln<=endLineNum-1;ln++) {
        rv+=(*this)[ln]->text;
        rv+=cr;
    }
    rv+=(*this)[endLineNum]->text.substr(0,endLineOffsetBytes);
}

void Lines::getText(ImString &rv, int startTotalOffsetInBytes, int endTotalOffsetBytes) const {
    rv="";
    IM_ASSERT(size()>0);
    const int CR_SIZE = (int) cr.size();
    const ImString& CR = cr;
    if (startTotalOffsetInBytes<0) startTotalOffsetInBytes=0;
    if (endTotalOffsetBytes==0 || (endTotalOffsetBytes>0 && endTotalOffsetBytes<startTotalOffsetInBytes)) return;

    int offset=0,tmpOffset=0;
    Line* line0 = (*this)[0];
    tmpOffset=offset+line0->size();
    if (startTotalOffsetInBytes<tmpOffset)  rv+=line0->text.substr(startTotalOffsetInBytes-offset);
    offset=tmpOffset;
    tmpOffset=offset+CR_SIZE;
    if (startTotalOffsetInBytes<tmpOffset && size()>1)  {
        rv+=CR;
        if (endTotalOffsetBytes>0 && endTotalOffsetBytes<tmpOffset) {
            rv=rv.substr(0,endTotalOffsetBytes-startTotalOffsetInBytes);
            return;
        }
    }
    offset=tmpOffset;

    for (int ln=1,lnsz=size();ln<lnsz;ln++) {
        Line* line = (*this)[ln];
        tmpOffset=offset+line->size();
        if (rv.size()==0)   {
            if (startTotalOffsetInBytes<tmpOffset)  {
                if (endTotalOffsetBytes>0 && endTotalOffsetBytes<tmpOffset) {
                    rv+=line->text.substr(startTotalOffsetInBytes-offset,endTotalOffsetBytes-startTotalOffsetInBytes);
                    return;
                }
                else rv+=line->text.substr(startTotalOffsetInBytes-offset);
            }
            offset = tmpOffset;
            if (lnsz>ln+1)  {
                tmpOffset = offset+CR_SIZE;
                if (endTotalOffsetBytes<0 || endTotalOffsetBytes>=tmpOffset)  rv+=CR;
                else {
                    rv+=CR.substr(0,endTotalOffsetBytes-tmpOffset);
                    return;
                }
                offset=tmpOffset;
            }
            continue;
        }

        if (rv.size()>0 && endTotalOffsetBytes>0 && endTotalOffsetBytes<tmpOffset)  {
            rv+=line->text.substr(0,endTotalOffsetBytes-offset);
            return;
        }
        offset = tmpOffset;
        if (rv.size()>0) rv+=line->text;
        if (lnsz>ln+1)  {
            tmpOffset = offset+CR_SIZE;
            if (rv.size()>0)    {
                if (endTotalOffsetBytes<0 || endTotalOffsetBytes>=tmpOffset)  rv+=CR;
                else {
                    rv+=CR.substr(0,endTotalOffsetBytes-tmpOffset);
                    return;
                }
            }
            offset=tmpOffset;
        }
    }

}


void Lines::SplitText(const char *text, ImVector<Line*> &lines,ImString* pOptionalCRout)    {
    IM_ASSERT(lines.size()==0); // replacement to the line below
    //lines.clear();  //MMMmhhh, should we call delete on all the line* ?
    if (pOptionalCRout) *pOptionalCRout="\n";
    if (!text) return;
    ImString ln("");char c;unsigned offsetInBytes=0,offsetInUTF8Chars=0;
    unsigned nl_n=0,nl_rn=0,nl_r=0,nl_nr=0;Line* line=NULL;
    for (int i=0,isz=strlen(text);i<isz;i++)    {
	line = NULL;
        c=text[i];
        if (c=='\n')    {
	    line = IMGUI_NEW(Line);
            if (i+1<isz && text[i+1]=='\r') {++i;++nl_nr;}
            else ++nl_n;
        }
        else if (c=='\r')   {
	    line = IMGUI_NEW(Line);
            if (i+1<isz && text[i+1]=='\n') {++i;++nl_rn;}
            else ++nl_r;
        }
	if (line)   {
	    //-----------------------------
	    line->text =		ln;
	    line->lineNumber =		lines.size();
	    line->offset =		offsetInBytes;
	    line->offsetInUTF8chars =	offsetInUTF8Chars;
        line->numUTF8chars =	ImGui::CountUTF8Chars(line->text.c_str());
	    lines.push_back(line);
	    //-----------------------------
	    offsetInBytes+=line->size();
	    offsetInUTF8Chars+=line->numUTF8chars;
	    ln="";continue;
	}
        ln+=c;
    }
    line = IMGUI_NEW(Line);
    line->text=ln;
    //-----------------------------
    line->text =		ln;
    line->lineNumber =		lines.size();
    line->offset =		offsetInBytes;
    line->offsetInUTF8chars =	offsetInUTF8Chars;
    line->numUTF8chars =	ImGui::CountUTF8Chars(line->text.c_str());
    lines.push_back(line);
    //-----------------------------
    offsetInBytes+=line->size();
    offsetInUTF8Chars+=line->numUTF8chars;
    ln="";

    if (pOptionalCRout) {
        if (nl_n<nl_rn || nl_n<nl_r)    *pOptionalCRout= nl_rn >= nl_r ? "\r\n" : "\r";
        //fprintf(stderr,"CR=\"%s\"\n",(*pOptionalCRout=="\n")?"\\n":(*pOptionalCRout=="\r\n")?"\\r\\n":(*pOptionalCRout=="\r")?"\\r":"\\n\\r");
    }
}

void Lines::setText(const char *text)   {
    destroy(text ? false : true);
    if (!text) return;
    ImVector<Line*> Lines;SplitText(text,Lines,&cr);
    for (int i=0,isz=Lines.size();i<isz;i++) {
        Base::push_back(Lines[i]);
    }
}

void CodeEditor::SetFonts(const ImFont *normal, const ImFont *bold, const ImFont *italic, const ImFont *boldItalic)  {

    const ImFont* fnt = ImFonts[FONT_STYLE_NORMAL]=(normal ? normal : (ImGui::GetIO().Fonts->Fonts.size()>0) ? (ImGui::GetIO().Fonts->Fonts[0]) : NULL);
    ImFonts[FONT_STYLE_BOLD]=bold?bold:fnt;
    ImFonts[FONT_STYLE_ITALIC]=italic?italic:fnt;
    ImFonts[FONT_STYLE_BOLD_ITALIC]=boldItalic?boldItalic:ImFonts[FONT_STYLE_BOLD]?ImFonts[FONT_STYLE_BOLD]:ImFonts[FONT_STYLE_ITALIC]?ImFonts[FONT_STYLE_ITALIC]:fnt;

    IM_ASSERT(ImFonts[FONT_STYLE_NORMAL]->FontSize == ImFonts[FONT_STYLE_BOLD]->FontSize);
    IM_ASSERT(ImFonts[FONT_STYLE_BOLD]->FontSize == ImFonts[FONT_STYLE_ITALIC]->FontSize);
    IM_ASSERT(ImFonts[FONT_STYLE_ITALIC]->FontSize == ImFonts[FONT_STYLE_BOLD_ITALIC]->FontSize);
}



// Folding Stuff
FoldingTag::FoldingTag(const ImString &s, const ImString &e, const ImString &_title, FoldingType t, bool _gainOneLineWhenPossible)  {
    start = s;
    IM_ASSERT(start.length()>0); // start string can't be empty
    end = e;
    IM_ASSERT(end.length()>0 && (!(end==start && t!=FOLDING_TYPE_COMMENT))); // end string can't be empty or equal to start string if they're not multiline comments
    title = _title;
    kind = t;
    gainOneLineWhenPossible = _gainOneLineWhenPossible;
}
class FoldingString : public FoldingTag	{
public:
    class FoldingPoint  {
    public:
        int lineNumber;
	Line* line;
        int lineOffset;         // (in bytes) from the start of the line
        bool isStartFolding;
        bool canGainOneLine;    // used only if isStartFolding == tru
        ImString customTitle;
        int openCnt;
	inline FoldingPoint(int _lineNumber=-1,Line* _line=NULL,int _lineOffset=-1,bool _isStartFolding=false,int _openCnt=0,bool _canGainOneLine=false)
        : lineNumber(_lineNumber),line(_line),lineOffset(_lineOffset),
        isStartFolding(_isStartFolding),canGainOneLine(_canGainOneLine),openCnt(_openCnt)
        {}
    };

public:
    const ImString& getStart() const {return start;}
    const ImString& getEnd() const {return end;}
    FoldingType getKind() const {return kind;}
    // If the folding title is "", the folding title will be set as the text between "Start" and the rest of the line
    const ImString& getTitle() const {return title;}
    bool getGainOneLineWhileFoldingWhenPossible() const {return gainOneLineWhenPossible;}
public:
    ImVectorEx<FoldingPoint> foldingPoints;
    FoldingPoint* getMatchingFoldingPoint (int foldingPointIndex)    {
        FoldingPoint* rv = NULL;
        if (foldingPointIndex >= (int)foldingPoints.size())  return NULL;
        const FoldingPoint& fp = foldingPoints [foldingPointIndex];
        int openCnt = fp.openCnt;

        if (fp.isStartFolding) {
            for (int i = foldingPointIndex+1,isz = (int)foldingPoints.size();i < isz; i++) {
                rv = &foldingPoints[i];
                if (rv->openCnt == openCnt)	{
                    if (!rv->isStartFolding) return rv;
                    return NULL;
                }
            }
        } else {
            for (int i = foldingPointIndex-1; i>=0; i--) {
                rv = &foldingPoints[i];
                if (rv->openCnt == openCnt)	{
                    if (rv->isStartFolding) return rv;
                    return NULL;
                }
            }
        }
        return NULL;
    }
    FoldingString(const ImString& s,const ImString& e,const ImString& _title,FoldingType t,bool _gainOneLineWhenPossible=false)
    : FoldingTag(s,e,_title,t,_gainOneLineWhenPossible)
    {
        openCnt=0;
    }
    FoldingString(const FoldingTag& tag) : FoldingTag(tag) {
        openCnt=0;
    }
    FoldingString() : FoldingTag() {
        openCnt=0;
    }

    int openCnt;//=0;

    int matchStartBeg;
    int curStartCharIndex;      // inside Start
    int matchEndBeg;
    int curEndCharIndex;		// inside End
    bool lookForCustomTitle;

};
class FoldingStringVector : public ImVectorEx<FoldingString> {
    protected:
    typedef ImVectorEx<FoldingString> Base;
    public:
    bool mergeAdditionalTrailingCharIfPossible;
    char additionalTrailingChar;
    FoldingStringVector() {
        mergeAdditionalTrailingCharIfPossible = false;
        additionalTrailingChar = ';';
        punctuationStringsMerged[0] = '\0';
        resetSHvariables();
    }
    FoldingStringVector(size_t size) : Base(size) {
        mergeAdditionalTrailingCharIfPossible = false;
        additionalTrailingChar = ';';
        punctuationStringsMerged[0] = '\0';
        resetSHvariables();
    }

    FoldingStringVector(const ImVectorEx<FoldingTag>& tags,bool _mergeAdditionalTrailingCharIfPossible=false,char _additionalTrailingChar=';')   {
        punctuationStringsMerged[0] = '\0';
        mergeAdditionalTrailingCharIfPossible = _mergeAdditionalTrailingCharIfPossible;
        additionalTrailingChar = _additionalTrailingChar;
        this->reserve(tags.size());
        for (size_t i=0,isz=tags.size();i<isz;i++)  {
            this->push_back(FoldingString(tags[i]));
        }
        resetSHvariables();
    }

    void resetAllTemporaryData()    {
        for (size_t i=0,isz=size();i<isz;i++) {
            FoldingString& f = (*this)[i];
            f.openCnt = 0;
            f.matchStartBeg=f.matchEndBeg=-1;
            f.curStartCharIndex=f.curEndCharIndex=0;
            f.lookForCustomTitle=false;
            f.foldingPoints.clear();
        }
    }
    void resetTemporaryLineData()
    {
        for (size_t i=0,isz=size();i<isz;i++) {
            FoldingString& f =(*this)[i];
            f.matchStartBeg=f.matchEndBeg=-1;
            f.curStartCharIndex=f.curEndCharIndex=0;
            f.lookForCustomTitle=false;
        }
    }

    // Stuff added for Syntax highlighting:
    public:
    void resetSHvariables() {
        for (int i=0;i<SH_LOGICAL_OPERATORS;i++) keywords[i].clear();
        for (int i=0;i<SH_STRING-SH_LOGICAL_OPERATORS+1;i++) punctuationStrings[i]=NULL;
        singleLineComment = multiLineCommentStart = multiLineCommentEnd = NULL;
        punctuationStringsMerged[0]='\0';
        punctuationStringsMergedSHMap.clear();        
        languageExtensions = NULL;
        stringEscapeChar = '\\';
        stringDelimiterChars = NULL;
    }

    // These const char* are all references to persistent strings except punctuationStringsMerged
    ImVectorEx<const char* > keywords[SH_LOGICAL_OPERATORS];
    const char* punctuationStrings[SH_STRING-SH_LOGICAL_OPERATORS+1];
    const char* singleLineComment;
    const char* multiLineCommentStart,*multiLineCommentEnd;
    char stringEscapeChar;
    const char* stringDelimiterChars;   // e.g. "\"'"

    char punctuationStringsMerged[1024];	// This must be sorted alphabetically (to be honest now that we use a map, sorting it's not needed anymore (and it was not used even before!))
    ImVectorEx<int> punctuationStringsMergedSHMap;
    const char* languageExtensions;

    public:
    // Maybe we could defer this call until a language is effectively required...
    void initSyntaxHighlighting() {
    static const char* PunctuationStringsDefault[SH_COMMENT-SH_LOGICAL_OPERATORS+1+2] = {"&!|~^","+-*/<>=","{}","[]","()",".:,;?%","\"'","//","/*","*/"};


    for (int i=0;i<SH_STRING-SH_LOGICAL_OPERATORS+1;i++) {
        if (!punctuationStrings[i]) punctuationStrings[i] = PunctuationStringsDefault[i];
	    //fprintf(stderr,"punctuationStrings[%d] = \"%s\"\n",i,punctuationStrings[i]);
	}

    if (!singleLineComment) singleLineComment = PunctuationStringsDefault[SH_COMMENT-SH_LOGICAL_OPERATORS];
    //fprintf(stderr,"singleLineComment = \"%s\"\n",singleLineComment);
    if (!multiLineCommentStart)	multiLineCommentStart = PunctuationStringsDefault[SH_COMMENT+1-SH_LOGICAL_OPERATORS];
    if (!multiLineCommentEnd)   multiLineCommentEnd = PunctuationStringsDefault[SH_COMMENT+2-SH_LOGICAL_OPERATORS];
	//fprintf(stderr,"multiLineCommentStart = \"%s\"\n",multiLineCommentStart);
	//fprintf(stderr,"multiLineCommentEnd = \"%s\"\n",multiLineCommentEnd);
    if (!stringDelimiterChars) {
        static const char* sdc = "\"'";
        stringDelimiterChars = sdc;
    }

	recalculateMergedString();

	//fprintf(stderr,"punctuationStringsMerged = \"%s\"\n",punctuationStringsMerged);

    }

    public:
    void recalculateMergedString() {
    punctuationStringsMerged[0]='\0';strcat(punctuationStringsMerged,"\t ");    // Add space and tab
	for (int i=0;i<SH_PUNCTUATION-SH_LOGICAL_OPERATORS+1;i++) {
        IM_ASSERT(strlen(punctuationStringsMerged)+strlen(punctuationStrings[i])<1022); // punctuationStringsMerged too short
	    strcat(punctuationStringsMerged,punctuationStrings[i]);
	}
	// Sort punctuationStringsMerged:
	const int len = strlen(punctuationStringsMerged);char tmp;
	for (int i=0;i<len;i++)	{
	    char& t = punctuationStringsMerged[i];
	    for (int j=i+1;j<len;j++)	{
		char& u = punctuationStringsMerged[j];
		if (u<t)    {tmp = t;t = u; u = tmp;}
	    }
	}
	punctuationStringsMergedSHMap.clear();
	punctuationStringsMergedSHMap.resize(len);
	for (int i=0;i<len;i++)	{
	    punctuationStringsMergedSHMap[i]=-1;
	    const char& t = punctuationStringsMerged[i];
	    //fprintf(stderr,"t[%d] = '%c'\n",i,punctuationStringsMerged[i]);

	    for (int j=0;j<SH_STRING-SH_LOGICAL_OPERATORS+1;j++) {
		const char* str = punctuationStrings[j];
		//fprintf(stderr,"punctuationStrings[%d] = \"%s\"\n",i,punctuationStrings[i]);
		if (!str || strlen(str)==0) continue;
		if (strchr(str,t)) {
		    punctuationStringsMergedSHMap[i] = j + SH_LOGICAL_OPERATORS;
		    break;
		}
	    }
        //IM_ASSERT (punctuationStringsMergedSHMap[i]>=0 && punctuationStringsMergedSHMap[i]<SH_COUNT);
	    //fprintf(stderr,"punctuationStringsMergedSHMap[%d] = %d\n",i,punctuationStringsMergedSHMap[i]);
	}
    }


};
class FoldSegment {
public:
    const FoldingTag* matchingTag;
    //ImString title;                         // e.g. "{...}", or "" for FOLDING_TYPE_REGION (if the folding title is "", the folding title will be set as the text between "startLineOffset" and the rest of the startLine)
    int startLineIndex,endLineIndex;        // zero based.
    Line *startLine, *endLine;
    int startLineOffset,endLineOffset;      // in bytes, relative to the start [pos of the first "start folding mark" char and pos after the last "end folding mark" char] of the respective lines.
    //FoldingType kind;
    bool isFolded;
    bool canGainOneLine;                    // when true, the folded code can be appended to lines[startLineIndex-1] (instead of startLine).
    inline FoldSegment(const FoldingTag* _matchingTag=NULL,
		       int _startLineIndex=-1,Line* _startLine=NULL,int _startLineOffset=-1,
		       int _endLineIndex=-1,Line* _endLine=NULL,int _endLineOffset=-1,
		       bool _canGainOneLine=false)
    : matchingTag(_matchingTag),
      startLineIndex(_startLineIndex),endLineIndex(_endLineIndex),
      startLine(_startLine),endLine(_endLine),
      startLineOffset(_startLineOffset),endLineOffset(_endLineOffset),
      isFolded(false),canGainOneLine(_canGainOneLine)
    {}
    void fprintf() const {
        ::fprintf(stderr,"startLine[%d:%d (k:%d %s %s) \"%s\"] endLine[%d:%d \"%s\"] title:\"%s\"",
		startLineIndex,startLineOffset,(int)matchingTag->kind,isFolded?"F":"O",canGainOneLine?"^":" ",startLine->text.c_str(),
                endLineIndex,endLineOffset,endLine->text.c_str(),
		matchingTag->title.c_str());
    }
};
class FoldSegmentVector : public ImVectorEx<FoldSegment> {
public:
};



static ImVectorEx<FoldingStringVector> gFoldingStringVectors;
static int gFoldingStringVectorIndices[LANG_COUNT] = {-1,-1,-1,-1,-1}; // global variable (values are indices from LANG_ENUM into gFoldingStringVectors)
static ImString gTotalLanguageExtensionFilter = ""; // SOMETHING LIKE ".cpp;.h;.cs;.py"
static void InitFoldingStringVectors() {
    if (gFoldingStringVectors.size()==0)    {
	//gFoldingStringVectors.reserve(LANG_COUNT);
        // CPP
        {
        FoldingStringVector foldingStrings; // To fill and push_back
        // Main Folding
	    foldingStrings.push_back(FoldingString ("{", "}", "{...}", FOLDING_TYPE_PARENTHESIS,true));
	    foldingStrings.push_back(FoldingString ("//region ", "//endregion", "", FOLDING_TYPE_REGION));
        foldingStrings.push_back (FoldingString ("/*", "*/", "/*...*/", FOLDING_TYPE_COMMENT));
        // Optional Folding
	    foldingStrings.push_back(FoldingString ("#pragma region ", "#pragma endregion", "", FOLDING_TYPE_REGION));
	    foldingStrings.push_back(FoldingString ("(", ")", "(...)", FOLDING_TYPE_PARENTHESIS, true));
	    foldingStrings.push_back(FoldingString ("[", "]", "[...]", FOLDING_TYPE_PARENTHESIS, true));

        // Syntax (for Syntax Highlighting)
        static const char* vars[]={"//","/*","*/","\"'"}; // static storage
        foldingStrings.singleLineComment = vars[0];
        foldingStrings.multiLineCommentStart = vars[1];
        foldingStrings.multiLineCommentEnd = vars[2];
        foldingStrings.stringDelimiterChars = vars[3];
        foldingStrings.stringEscapeChar = '\\';

        // Keywords
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_PREPROCESSOR;
            static const char* vars[] = {"#include","#if","#ifdef","#ifndef","#else","#elif","#endif","#include","#define","#undef","#warning","#error","#pragma"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_ACCESS;
            static const char* vars[] = {"this","base","private","protected","public"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_DECLARATION;
            static const char* vars[] = {"class","struct","enum","union","template","typedef","typename"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_ITERATION;
            static const char* vars[] = {"for","while","do"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_JUMP;
            static const char* vars[] = {"break","continue","return","goto"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_SELECTION;
            static const char* vars[] = {"switch","case","default","if","else"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_OPERATOR;
            static const char* vars[] = {"new","delete","const_cast","dynamic_cast","reinterpret_cast","static_cast","static_assert","slots","signals","typeid","operator","decltype","and","and_eq","not","not_eq","or","or_eq","xor","xor_eq","bitand","bitor"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_EXCEPTION;
            static const char* vars[] = {"try","throw","catch","finally","bad_typeid","bad_cast","noexcept"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_CONSTANT;
            static const char* vars[] = {"true","false","NULL","nullptr","sizeof","alignof","alignas","type_info","type_index","this","asm"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_MODIFIER;
            static const char* vars[] = {"virtual","override","final","explicit","export","friend","mutable","const","static","thread_local","volatile","extern","register"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_TYPE;
            static const char* vars[] = {"bool","void","int","signed","unsigned","long","float","double","short","char","wchar_t","char16_t","char32_t","size_t","ptrdiff_t","max_align_t","offsetof","string","vector","deque","map","unordered_map","set","auto"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }
        {
            const SyntaxHighlightingType sht = SH_KEYWORD_NAMESPACE;
            static const char* vars[] = {"namespace","using"};
            const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
        }

        // Extensions:
        static const char exts[] = ".c;.cpp;.cxx;.cc;.h;.hpp;.hxx"; // static storage
        foldingStrings.languageExtensions = exts;
        // Mandatory call:
        foldingStrings.initSyntaxHighlighting();     // Mandatory

        // Assignment:
        gFoldingStringVectors.push_back(foldingStrings);
	    gFoldingStringVectorIndices[LANG_CPP] = gFoldingStringVectors.size()-1;
        }
        // CS
        {
            FoldingStringVector foldingStrings;
            foldingStrings.push_back(FoldingString ("{", "}", "{...}", FOLDING_TYPE_PARENTHESIS,true));
            foldingStrings.push_back(FoldingString ("#region ", "#endregion", "", FOLDING_TYPE_REGION));
            foldingStrings.push_back (FoldingString ("/*", "*/", "/*...*/", FOLDING_TYPE_COMMENT));
            // Optionals:
            foldingStrings.push_back(FoldingString ("(", ")", "(...)", FOLDING_TYPE_PARENTHESIS, true));
            foldingStrings.push_back(FoldingString ("[", "]", "[...]", FOLDING_TYPE_PARENTHESIS, true));

	    // Syntax (for Syntax Highlighting)
	    static const char* vars[]={"//","/*","*/","\"'"}; // static storage
	    foldingStrings.singleLineComment = vars[0];
	    foldingStrings.multiLineCommentStart = vars[1];
	    foldingStrings.multiLineCommentEnd = vars[2];
	    foldingStrings.stringDelimiterChars = vars[3];
	    foldingStrings.stringEscapeChar = '\\';

            // Keywords
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_PREPROCESSOR;
                static const char* vars[] = {"#if","#else","#elif","#endif","#define","#undef","#warning","#error","#line","#region","#endregion","#pragma"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_ACCESS;
                static const char* vars[] = {"this","base"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_OPERATOR;
                static const char* vars[] = {"as","is","new","sizeof","typeof","stackalloc"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_SELECTION;
                static const char* vars[] = {"else","if","switch","case","default"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_ITERATION;
                static const char* vars[] = {"do","for","foreach","in","while"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_JUMP;
                static const char* vars[] = {"break","continue","goto","return"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_CONTEXT;
                static const char* vars[] = {"yield","partial","global","where","__arglist","__makeref","__reftype","__refvalue","by","descending",
                                             "from","group","into","orderby","select","let","ascending","join","on","equals"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_EXCEPTION;
                static const char* vars[] = {"try","throw","catch","finally"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_CONSTANT;
                static const char* vars[] = {"true","false","null"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_MODIFIER;
                static const char* vars[] = {"abstract","async","await","const","event","extern","override","readonly","sealed","static",
                                             "virtual","volatile","public","protected","private","internal"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_TYPE;
                static const char* vars[] = {"void","bool","byte","char","decimal","double","float","int","long","sbyte"
                                             "short","uint","ushort","ulong","object","string","var","dynamic"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_NAMESPACE;
                static const char* vars[] = {"namespace","using"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_PROPERTY;
                static const char* vars[] = {"get","set","add","remove","value"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_DECLARATION;
                static const char* vars[] = {"class","interface","delegate","enum","struct","explicit","implicit","operator"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_OTHER;
                static const char* vars[] = {"checked","unchecked","fixed","unsafe","lock"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }

            // Extensions:
            static const char exts[] = ".cs";
            foldingStrings.languageExtensions = exts;
            // Mandatory call:
            foldingStrings.initSyntaxHighlighting();     // Mandatory

            // Assignment:
            gFoldingStringVectors.push_back(foldingStrings);
            gFoldingStringVectorIndices[LANG_CS] = gFoldingStringVectors.size()-1;
        }
        // LUA
        {
            FoldingStringVector foldingStrings;
            foldingStrings.push_back(FoldingString ("{", "}", "{...}", FOLDING_TYPE_PARENTHESIS,false));
            foldingStrings.push_back(FoldingString ("--region ", "--endregion", "", FOLDING_TYPE_REGION));
            foldingStrings.push_back (FoldingString ("--[[", "--]]", "--...--", FOLDING_TYPE_COMMENT));
            // Optionals:
            foldingStrings.push_back(FoldingString ("(", ")", "(...)", FOLDING_TYPE_PARENTHESIS));
            foldingStrings.push_back(FoldingString ("[", "]", "[...]", FOLDING_TYPE_PARENTHESIS));
            // Syntax (for Syntax Highlighting)
            static const char* vars[]={"--","--[[","]--"}; // static storage
            foldingStrings.singleLineComment = vars[0];
            foldingStrings.multiLineCommentStart = vars[1];
            foldingStrings.multiLineCommentEnd = vars[2];

            // Keywords
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_OPERATOR;
                static const char* vars[] = {"and","or","not"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_SELECTION;
                static const char* vars[] = {"else","elseif","end","if","in","then"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_ITERATION;
                static const char* vars[] = {"do","for","repeat","until","while"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_CONSTANT;
                static const char* vars[] = {"false","nil","true"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_MODIFIER;
                static const char* vars[] = {"global","local"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_DECLARATION;
                static const char* vars[] = {"function"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }

            // Extensions:
            static const char exts[] = ".lua"; // static storage
            foldingStrings.languageExtensions = exts;
            // Mandatory call:
            foldingStrings.initSyntaxHighlighting();     // Mandatory

            // Assignment:
            gFoldingStringVectors.push_back(foldingStrings);
            gFoldingStringVectorIndices[LANG_LUA] = gFoldingStringVectors.size()-1;
        }
        // PYTHON
        {
            FoldingStringVector foldingStrings;
            foldingStrings.push_back(FoldingString ("{", "}", "{...}", FOLDING_TYPE_PARENTHESIS,true));
            foldingStrings.push_back(FoldingString ("#region ", "#endregion", "", FOLDING_TYPE_REGION));
            foldingStrings.push_back (FoldingString ("\"\"\"", "\"\"\"", "\"\"\"...\"\"\"", FOLDING_TYPE_COMMENT));
            // Optionals:
            foldingStrings.push_back(FoldingString ("(", ")", "(...)", FOLDING_TYPE_PARENTHESIS));
            foldingStrings.push_back(FoldingString ("[", "]", "[...]", FOLDING_TYPE_PARENTHESIS));

            // Syntax (for Syntax Highlighting)
            static const char* vars[]={"#","\"\"\"","\"\"\""}; // static storage
            foldingStrings.singleLineComment = vars[0];
            foldingStrings.multiLineCommentStart = vars[1];
            foldingStrings.multiLineCommentEnd = vars[2];

            // Keywords
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_SELECTION;
                static const char* vars[] = {"and","assert","break","class","continue","def","del","elif","else","except",
                                             "exec","finally","for","global","if","in","is","lambda","not","or",
                                             "pass","print","raise","return","try","while","yield","with"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_JUMP;
                static const char* vars[] = {"abs","all","any","apply","basestring","bool","buffer","callable","chr","classmethod",
                                             "cmp","coerce","compile","complex","delattr","dict","dir","divmod","enumerate","eval",
                                             "execfile","file","filter","float","frozenset","getattr","globals","hasattr","hash","hex",
                                             "id","input","int","intern","isinstance","issubclass","iter","len","list","locals",
                                             "long","map","max","min","object","oct","open","ord","pow","property",
                                             "range","raw_input","reduce","reload","repr","reversed","round","setattr","set","slice",
                                             "sorted","staticmethod","str","sum","super","tuple","type","unichr","unicode","vars",
                                             "xrange","zip"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_CONTEXT;
                static const char* vars[] = {"False","None","True"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_EXCEPTION;
                static const char* vars[] = {"ArithmeticError","AssertionError","AttributeError","EnvironmentError","EOFError","Exception",
                                             "FloatingPointError","ImportError","IndentationError","IndexError",
                                             "IOError","KeyboardInterrupt","KeyError","LookupError","MemoryError",
                                             "NameError","NotImplementedError","OSError","OverflowError","ReferenceError",
                                             "RuntimeError","StandardError","StopIteration","SyntaxError","SystemError",
                                             "SystemExit","TabError","TypeError","UnboundLocalError","UnicodeDecodeError",
                                             "UnicodeEncodeError","UnicodeError","UnicodeTranslateError","ValueError","WindowsError",
                                             "ZeroDivisionError","Warning","UserWarning","DeprecationWarning","PendingDeprecationWarning",
                                             "SyntaxWarning","OverflowWarning","RuntimeWarning","FutureWarning"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_TYPE;
                static const char* vars[] = {"BufferType","BuiltinFunctionType","BuiltinMethodType","ClassType","CodeType",
                                             "ComplexType","DictProxyType","DictType","DictionaryType","EllipsisType",
                                             "FileType","FloatType","FrameType","FunctionType","GeneratorType",
                                             "InstanceType","IntType","LambdaType","ListType","LongType",
                                             "MethodType","ModuleType","NoneType","ObjectType","SliceType",
                                             "StringType","StringTypes","TracebackType","TupleType","TypeType",
                                             "UnboundMethodType","UnicodeType","XRangeType"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_NAMESPACE;
                static const char* vars[] = {"import","from","as"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }
            {
                const SyntaxHighlightingType sht = SH_KEYWORD_OTHER;
                static const char* vars[] = {"__abs__","__add__","__all__","__author__","__bases__",
                                             "__builtins__","__call__","__class__","__cmp__","__coerce__",
                                             "__contains__","__debug__","__del__","__delattr__","__delitem__",
                                             "__delslice__","__dict__","__div__","__divmod__","__doc__",
                                             "__docformat__","__eq__","__file__","__float__","__floordiv__",
                                             "__future__","__ge__","__getattr__","__getattribute__","__getitem__",
                                             "__getslice__","__gt__","__hash__","__hex__","__iadd__",
                                             "__import__","__imul__","__init__","__int__","__invert__",
                                             "__iter__","__le__","__len__","__long__","__lshift__",
                                             "__lt__","__members__","__metaclass__","__mod__","__mro__",
                                             "__mul__","__name__","__ne__","__neg__","__new__",
                                             "__nonzero__","__oct__","__or__","__path__","__pos__",
                                             "__pow__","__radd__","__rdiv__","__rdivmod__","__reduce__",
                                             "__repr__","__rfloordiv__","__rlshift__","__rmod__","__rmul__",
                                             "__ror__","__rpow__","__rrshift__","__rsub__","__rtruediv__",
                                             "__rxor__","__setattr__","__setitem__","__setslice__","__self__",
                                             "__slots__","__str__","__sub__","__truediv__","__version__",
                                             "__xor__"};
                const int varsSize = (int)sizeof(vars)/sizeof(vars[0]);foldingStrings.keywords[sht].reserve(foldingStrings.keywords[sht].size()+varsSize);for (int i=0;i<varsSize;i++) foldingStrings.keywords[sht].push_back(vars[i]);
            }

            // Extensions:
            static const char exts[] = ".py";
            foldingStrings.languageExtensions = exts;
            // Mandatory call:
            foldingStrings.initSyntaxHighlighting();     // Mandatory


            // Assignment:
            gFoldingStringVectors.push_back(foldingStrings);
            gFoldingStringVectorIndices[LANG_PYTHON] = gFoldingStringVectors.size()-1;
        }
    }
}
void CodeEditor::init() {
    inited = true;
    if (!ImFonts[FONT_STYLE_NORMAL]) SetFonts(NULL);
    if (gFoldingStringVectors.size()==0) InitFoldingStringVectors();

    gTotalLanguageExtensionFilter = "";
    for (int lng=0;lng<LANG_COUNT;lng++)    {
	const int id = gFoldingStringVectorIndices[lng];
	if (id<0) continue;
	const FoldingStringVector& fsv = gFoldingStringVectors[id];
	if (fsv.languageExtensions && strlen(fsv.languageExtensions)>0)	{
	    if (gTotalLanguageExtensionFilter.size()>0) gTotalLanguageExtensionFilter+=";";
	    gTotalLanguageExtensionFilter+=fsv.languageExtensions;
	}
    }

    StaticInited = true;
}


bool CodeEditor::StaticInited = false;
inline static FoldingStringVector* GetGlobalFoldingStringVectorForLanguage(Language language)    {
    const int index = gFoldingStringVectorIndices[language];
    return (index>=0 && index<gFoldingStringVectors.size()) ? &gFoldingStringVectors[index] : NULL;
}
bool CodeEditor::HasFoldingSupportForLanguage(Language language)    {
    return (GetGlobalFoldingStringVectorForLanguage(language)!=NULL);
}
bool CodeEditor::SetFoldingSupportForLanguage(Language language, const ImVectorEx<FoldingTag> &foldingTags, bool mergeAdditionalTrailingCharIfPossible, char additionalTrailingChar)  {
    IM_ASSERT(!StaticInited);   // you must use the static methods before rendering or initing any instance of the Code Editor
    FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(language);
    if (!fsv) {
        gFoldingStringVectors.push_back(FoldingStringVector());
        gFoldingStringVectorIndices[language]= gFoldingStringVectors.size()-1;
        fsv = &gFoldingStringVectors[gFoldingStringVectors.size()-1];
    }
    if (!fsv) return false;
    fsv->clear();
    *fsv = FoldingStringVector(foldingTags,mergeAdditionalTrailingCharIfPossible,additionalTrailingChar);
    return true;
}
bool CodeEditor::AddSyntaxHighlightingTokens(Language language, SyntaxHighlightingType type, const char **tokens, int numTokens)
{
    IM_ASSERT(!StaticInited);   // you must use the static methods before rendering or initing any instance of the Code Editor
    IM_ASSERT(tokens && numTokens>0);
    IM_ASSERT(language<LANG_COUNT);
    IM_ASSERT(type<SH_LOGICAL_OPERATORS);   // Only keywords here please
    FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(language);
    IM_ASSERT(fsv); // No time to check this
    if (!fsv) return false;
    for (int i=0;i<numTokens;i++)  fsv->keywords[type].push_back(tokens[i]);
    return true;
}

bool CodeEditor::ClearSyntaxHighlightingTokens(Language language, SyntaxHighlightingType type)
{
    IM_ASSERT(!StaticInited);   // you must use the static methods before rendering or initing any instance of the Code Editor
    IM_ASSERT(language<LANG_COUNT);
    IM_ASSERT(type<SH_LOGICAL_OPERATORS);   // Only keywords here please
    FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(language);
    IM_ASSERT(fsv); // No time to check this
    if (!fsv) return false;
    fsv->keywords[type].clear();
    return true;
}

bool CodeEditor::SetSyntaxHighlightingExtraStuff(Language language, const char *singleLineComment, const char *stringDelimiters, const char *logicalOperators, const char *mathOperators, const char *punctuation)
{
    IM_ASSERT(!StaticInited);   // you must use the static methods before rendering or initing any instance of the Code Editor
    IM_ASSERT(language<LANG_COUNT);
    FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(language);
    IM_ASSERT(fsv); // No time to check this
    if (!fsv) return false;
    if (singleLineComment) fsv->singleLineComment = singleLineComment;
    if (stringDelimiters)  fsv->punctuationStrings[SH_STRING-SH_LOGICAL_OPERATORS]              = stringDelimiters;
    if (logicalOperators)  fsv->punctuationStrings[SH_LOGICAL_OPERATORS-SH_LOGICAL_OPERATORS]   = logicalOperators;
    if (mathOperators)  fsv->punctuationStrings[SH_MATH_OPERATORS-SH_LOGICAL_OPERATORS]         = mathOperators;
    if (punctuation)  fsv->punctuationStrings[SH_PUNCTUATION-SH_LOGICAL_OPERATORS]              = punctuation;
    return true;
}

Language CodeEditor::GetLanguageFromFilename(const char *filename)
{
    if (!filename || strlen(filename)==0) return LANG_NONE;
    const char* ext = strrchr(filename,'.');
    if (!ext || strlen(ext)==0) return LANG_NONE;
    const int ext_len = strlen(ext);
    for (int i=0,isz=LANG_COUNT;i<isz;i++)  {
        const int index = gFoldingStringVectorIndices[i];
        if (index<0 || index>=gFoldingStringVectors.size()) continue;
        const FoldingStringVector* fsv = &gFoldingStringVectors[index];
        if (!fsv) continue;
        const char* exts = fsv->languageExtensions; // e.g. ".cpp;.h;.hpp"
        if (!exts || strlen(exts)==0) continue;
        //fprintf(stderr,"exts[%d]=\"%s\" (ext=\"%s\") i=%d\n",i,exts,ext,i);
        const char* ex = ext;
        int cnt=0;
        for (int j=0,jsz=strlen(exts);j<jsz;j++)    {
            const char* ch = &exts[j];
            while (*ex++==*ch++) {
                ++cnt;
                if (cnt==ext_len) return (Language) i;
            }
            ex = ext;cnt=0;
        }

    }
    return LANG_NONE;
}

#ifndef NO_IMGUICODEEDITOR_SAVE
bool CodeEditor::save(const char* filename) {
    if (!filename || strlen(filename)==0) return false;
    ImString text = "";
    lines.getText(text);
    FILE* f = fopen(filename,"w");
    if (!f) return false;
    // TODO: UTF8 BOM here ?
    fwrite((const void*) text.c_str(),text.size(),1,f);
    fclose(f);
    return true;
}
#endif //NO_IMGUICODEEDITOR_SAVE
#ifndef NO_IMGUICODEEDITOR_LOAD
bool CodeEditor::load(const char* filename, Language optionalLanguage) {
    if (!filename || strlen(filename)==0) return false;
    ImVector<char> text;
    FILE* f = fopen(filename,"r");
    if (!f) return false;
    fseek(f,0,SEEK_END);
    const size_t length = ftell(f);
    fseek(f,0,SEEK_SET);
    // TODO: UTF8 BOM here ?
    text.resize(length+1);
    fread((void*) &text[0],length,1,f);
    text[length]='\0';
    fclose(f);
    if (optionalLanguage==LANG_COUNT) {
        optionalLanguage = GetLanguageFromFilename(filename);
    }
    setText(length>0 ? &text[0] : "",optionalLanguage);
    return true;
}
#endif //NO_IMGUICODEEDITOR_LOAD
void CodeEditor::setText(const char *text, Language _lang) {
    if (lang!=_lang)    {
        shTypeKeywordMap.clear();
        shTypePunctuationMap.clear();

        FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(_lang);
        if (fsv)    {
            // replace our keywords hashmap
            for (int i=0,isz = SH_LOGICAL_OPERATORS;i<isz;i++)  {
                const ImVectorEx<const char*>& v = fsv->keywords[i];
                for (int j=0,jsz=v.size();j<jsz;j++) {
                    shTypeKeywordMap.put((MyKeywordMapType::KeyType)v[j],i);
                    //fprintf(stderr,"Putting in shTypeMap: \"%s\",%d\n",v[j],i);
                }
            }
            // replace our punctuation hashmap
            for (int i=0,isz = strlen(fsv->punctuationStringsMerged);i<isz;i++)  {
                const char c = fsv->punctuationStringsMerged[i];
                shTypePunctuationMap.put(c,fsv->punctuationStringsMergedSHMap[i]);
                //fprintf(stderr,"Putting in shTypePunctuationMap: '%c',%d\n",c,fsv->punctuationStringsMergedSHMap[i]);
            }
        }
    }
    lang = _lang;
    lines.setText(text);
    if (enableTextFolding) {
        ParseTextForFolding(false,true);
    }
}

inline static ImString TrimSpacesAndTabs(const ImString& sIn)    {
    ImString tOut = sIn;
    if (sIn.size()<1) return tOut;
    int sz=0;
    while ((sz=tOut.size())>0)   {
	if (tOut[0]==' ' || tOut[0]=='\t')  {tOut=tOut.substr(1);continue;}
	--sz;
	if (tOut[sz]==' ' || tOut[sz]=='\t')  {tOut=tOut.substr(0,sz);continue;}
	break;
    }
    return tOut;
}

void CodeEditor::ParseTextForFolding(bool forceAllSegmentsFoldedOrNot, bool foldingStateToForce) {
    // Naive algorithm: TODO: rewrite to handle "//" and strings correctly

    if (!enableTextFolding || lines.size()==0) return;
    FoldingStringVector* pFoldingStrings = GetGlobalFoldingStringVectorForLanguage(lang);
    if (!pFoldingStrings || pFoldingStrings->size()==0) return;
    FoldingStringVector& foldingStrings = *pFoldingStrings;
    //--------------------- START --------------------------------------------

    foldingStrings.resetAllTemporaryData();

    ImVectorEx<FoldSegment> foldSegments;

    bool acceptStartMultilineCommentFolding = true;	//internal, do not touch
    Line* line=NULL;
    char ch;const int numLines = lines.size();
    const int singleLineCommentSize = foldingStrings.singleLineComment ? strlen(foldingStrings.singleLineComment) : 0;
    int firstValidCharPos = -1;
    for (int i=0;i<numLines; i++) {
        line = lines[i];
        if (!line) continue;
        const ImString& text = line->text;
        if (text.size() == 0)   continue;
        foldingStrings.resetTemporaryLineData();

        firstValidCharPos = -1; // calculated only if singleLineCommentSize>0 ATM
    for (int ti=0,tisz=text.length(); ti<tisz; ti++) {
	    const char c = text [ti];
        if (firstValidCharPos==-1 && singleLineCommentSize && (!(c==' ' || c=='\t'))) {
            firstValidCharPos = ti;
            // check if it's a line comment so we can exit early and prevent tokens after "//" to be incorrectly detected.
            // TODO: shouldn't we exit even when we're not "firstValidCharPos" ? YES, but doing proper code folding is DIFFICULT... so we don't do it.
            const char* ptext = &text[ti];
            if (strncmp(ptext,foldingStrings.singleLineComment,singleLineCommentSize)==0) {
                //fprintf(stderr,"%d) %c (%s %d)\n",i+1,c,foldingStrings.singleLineComment,singleLineCommentSize);
                // Ok, but we can't skip regions: (e.g. "//region Blah blah blah" or "//endregion")
                bool mustSkip = true;
                for (int fsi=0,fsisz=foldingStrings.size();fsi<fsisz;fsi++) {
                    const FoldingString& fs = foldingStrings[fsi];
                    if (fs.kind!=FOLDING_TYPE_REGION) continue;
                    if ( (strncmp(ptext,fs.start.c_str(),fs.start.size())==0) || (strncmp(ptext,fs.end.c_str(),fs.end.size())==0) ) {
                        mustSkip = false;
                        break;
                    }
                }
                if (mustSkip) break;   // Skip line
            }
        }
	    for (int fsi=0,fsisz=foldingStrings.size();fsi<fsisz;fsi++) {
		FoldingString& fs = foldingStrings[fsi];
		if (acceptStartMultilineCommentFolding || fs.kind != FOLDING_TYPE_COMMENT) {
		    ch = fs.start[fs.curStartCharIndex];
            if (fs.lookForCustomTitle && fs.foldingPoints.size() > 0)   fs.foldingPoints[fs.foldingPoints.size() - 1].customTitle += c;// = fs.foldingPoints[fs.foldingPoints.Count-1].CustomTitle + c.ToString ();
            else if (c == ch) {
                if (fs.matchStartBeg < 0)   fs.matchStartBeg = ti;
                ++fs.curStartCharIndex;
                if (fs.curStartCharIndex == fs.start.length()) {
                    //Tmatch-------------
                    //Console.WriteLine ("Match: \""+fs.Start+"\" at line: "+(i)+" and column: "+fs.matchStartBeg);

                    bool gainOneLine = false;
                    if (fs.gainOneLineWhenPossible && i > 1 && TrimSpacesAndTabs(text.substr(0, fs.matchStartBeg)).length() == 0) {
                        Line* prevline = lines[i - 1];
                        if (prevline) {
                            ImString prevText = prevline->text;
                            if (TrimSpacesAndTabs(prevText).length() != 0) {
                                gainOneLine = true;
                                //fs.foldingPoints.push_back(FoldingString::FoldingPoint (i - 1, prevText.length(), prevline->offset + prevText.length(), true, fs.openCnt));
                                fs.foldingPoints.push_back(FoldingString::FoldingPoint (i,line,fs.matchStartBeg,true,fs.openCnt,gainOneLine));
                            }
                        }
                    }
                    if (!gainOneLine) fs.foldingPoints.push_back(FoldingString::FoldingPoint (i,line,fs.matchStartBeg,true,fs.openCnt,gainOneLine));

                            if (fs.title.length() == 0)  fs.lookForCustomTitle = true;
                    ++fs.openCnt;
                    //------------------------
                    fs.curStartCharIndex = 0;
                    fs.matchStartBeg = -1;	//reset matcher
                    if (fs.kind == FOLDING_TYPE_COMMENT) {
                        acceptStartMultilineCommentFolding = false;
                        fs.curEndCharIndex = 0;
                        fs.matchEndBeg = -1;	//reset end matcher too
                    }
                }
            } else {
                fs.curStartCharIndex = 0;
                fs.matchStartBeg = -1;	//reset
            }
		}

		ch = fs.end [fs.curEndCharIndex];
		if (c == ch) {
		    if (fs.matchEndBeg < 0) fs.matchEndBeg = ti;
		    ++fs.curEndCharIndex;
		    if (fs.curEndCharIndex == fs.end.length()) {
			--fs.openCnt;
			//match-------------
			//Console.WriteLine ("Match: \""+fs.End+"\" at line: "+(i)+" and column: "+fs.matchEndBeg);
            fs.foldingPoints.push_back (FoldingString::FoldingPoint (i,line, fs.matchEndBeg,false,fs.openCnt));
			// region Add folding region (copying some data from regions already present)
			if (fs.foldingPoints.size() >= 2) {
                FoldingString::FoldingPoint& endPoint = fs.foldingPoints [fs.foldingPoints.size() - 1];
                FoldingString::FoldingPoint* startPoint = fs.getMatchingFoldingPoint(fs.foldingPoints.size() - 1);
                if (startPoint && endPoint.lineNumber == startPoint->lineNumber) {
                    // HP)
                    // Inside here 'startPoint' and 'endPoint' refer to the same line of text AND
                    // this same line is the one pointed by 'line' and 'text'
                    const bool foldSingleLineRegions = false;
                    if (!/*fs.*/foldSingleLineRegions)  startPoint = NULL;	// = do not fold because the user doesn't want to
                    else    {
                        //There are still cases in which we want not to fold the single line, even if user wants to
                        int netStartColumn = startPoint->lineOffset + fs.start.length();
                        int netEndColumn = endPoint.lineOffset - fs.end.length();
                        int netLength = netEndColumn - netStartColumn;
                        if (netLength < 1 || TrimSpacesAndTabs(text.substr(netStartColumn, netLength)).length() < 2)  startPoint = NULL;	// = do not fold
                    }
                }
                if (startPoint) {
				//bool useCustomTitle = (fs.title.length() == 0);
		//ImString title = useCustomTitle ? TrimSpacesAndTabs(startPoint->customTitle) : fs.title;

		FoldSegment newSegment(&fs,
                                       startPoint->lineNumber,startPoint->line,startPoint->lineOffset,
                                       endPoint.lineNumber,endPoint.line,endPoint.lineOffset,
				       startPoint->canGainOneLine);

				//Check to see if the same folding was already present (we will replace all present foldings),
				//and retrive if it was folded or not
                if (!forceAllSegmentsFoldedOrNot) newSegment.isFolded = newSegment.startLine->isFolded();
                else  newSegment.isFolded = foldingStateToForce;

				foldSegments.push_back(newSegment);
				//Console.WriteLine("Added Folding: "+title+ " Fold Region Length = "+(endPoint.Offset - startPoint.Offset));
			    }
			}
			// endregion
			//------------------------
			fs.curEndCharIndex = 0;
			fs.matchEndBeg = -1;	//reset
			if (fs.kind == FOLDING_TYPE_COMMENT/* && fs.openCnt<=0*/)   acceptStartMultilineCommentFolding = true;
		    }
		} else {
		    fs.curEndCharIndex = 0;
		    fs.matchEndBeg = -1;	//reset
		}
	    }

	}

    }

    // Reset Line foldings
    for (int lni=0,lnisz=lines.size();lni<lnisz;lni++)	{
	Line* line = lines[lni];
	line->resetFoldingAttributes();
    }

    // Now use "foldSegments" to update the folding state of all lines.
    for (int i=0,isz=foldSegments.size();i<isz;i++) {
	const FoldSegment& fs = foldSegments[i];

    //fprintf(stderr,"foldSegments[%d]: ",i);fs.fprintf();fprintf(stderr,"\n");

    // start folding line
    fs.startLine->attributes|=(Line::AT_FOLDING_START | (fs.canGainOneLine?Line::AT_SAVE_SPACE_LINE:0));
    fs.startLine->foldingStartTag = fs.matchingTag;
    fs.startLine->foldingEndLine = fs.endLine;
    fs.startLine->foldingStartOffset = fs.startLineOffset;
    fs.startLine->setFoldedState(fs.isFolded);
    // lines in between
    int nestedFoldings = 0;
    for (int j=fs.startLineIndex+1;j<fs.endLineIndex;j++)   {
        Line* line = lines[j];
        if (fs.isFolded) line->setHidden(true);
        // Testing Stuff ------------
        if (line->isFoldable()) {
            if (nestedFoldings==0) line->foldingStartLine = fs.startLine;
            ++nestedFoldings;
            continue;
        }
        if (line->isFoldingEnd()) {
            --nestedFoldings;
            if (nestedFoldings==0) line->foldingEndLine = fs.endLine;
            continue;
        }
        if (nestedFoldings==0)  {
            line->foldingStartLine = fs.startLine;
            line->foldingEndLine = fs.endLine;
            //fprintf(stderr,"Setting: line[%d] foldingStartLine=%d foldingEndLine=%d\n",j+1,line->foldingStartLine->lineNumber+1,line->foldingEndLine->lineNumber+1);
        }
        // End Testing Stuff ------------
    }

    // end folding line
    fs.endLine->attributes|=Line::AT_FOLDING_END;
    fs.endLine->foldingEndOffset = fs.endLineOffset;
    fs.endLine->foldingStartLine = fs.startLine;

    // Fix: remove "canGainOneLine" to lines whose previous line was foldable itself
    if (fs.startLine->canFoldingBeMergedWithLineAbove())    {
        if (fs.startLine->lineNumber-1>=0 && lines[fs.startLine->lineNumber-1]->isFoldable())  fs.startLine->attributes&=~Line::AT_SAVE_SPACE_LINE;
    }
    else if (fs.startLine->isFoldable()) {
        if (fs.startLine->lineNumber+1<lines.size() && lines[fs.startLine->lineNumber+1]->canFoldingBeMergedWithLineAbove()) lines[fs.startLine->lineNumber+1]->attributes&=~Line::AT_SAVE_SPACE_LINE;
    }

    }



}


static const SyntaxHighlightingType FoldingTypeToSyntaxHighlightingType[FOLDING_TYPE_REGION+1] = {SH_FOLDED_PARENTHESIS,SH_FOLDED_COMMENT,SH_FOLDED_REGION};

// Global temporary variables:
static bool gCurlineStartedWithDiesis;
static Line* gCurline;
static bool gIsCurlineHovered;
static bool gIsCursorChanged;

// Main method
void CodeEditor::render()   {
    if (lines.size()==0) return;
    if (!inited) init();
    static const ImVec4 transparent(1,1,1,0);

    ImGuiIO& io = ImGui::GetIO();

    // Init
    if (!ImFonts[0]) {
        if (io.Fonts->Fonts.size()==0) return;
        for (int i=0;i<FONT_STYLE_COUNT;i++) ImFonts[i]=io.Fonts->Fonts[0];
    }



    bool leftPaneHasMouseCursor = false;
    if (show_left_pane) {
        // Helper stuff for setting up the left splitter
        static ImVec2 lastWindowSize=ImGui::GetWindowSize();      // initial window size
        ImVec2 windowSize = ImGui::GetWindowSize();
        const bool windowSizeChanged = lastWindowSize.x!=windowSize.x || lastWindowSize.y!=windowSize.y;
        if (windowSizeChanged) lastWindowSize = windowSize;
        static float w = lastWindowSize.x*0.2f;                    // initial width of the left window

        //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));

        ImGui::BeginChild("code_editor_left_pane", ImVec2(w,0));

        ImGui::Spacing();
        if (show_style_editor)   {
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Style Editor##styleEditor",NULL,false))   {
                ImGui::Separator();
                // Color Mode
                static const char* btnlbls[2]={"HSV##myColorBtnType","RGB##myColorBtnType"};
                if (colorEditMode!=ImGuiColorEditMode_RGB)  {
                    if (ImGui::SmallButton(btnlbls[0])) {
                        colorEditMode = ImGuiColorEditMode_RGB;
                        ImGui::ColorEditMode(colorEditMode);
                    }
                }
                else if (colorEditMode!=ImGuiColorEditMode_HSV)  {
                    if (ImGui::SmallButton(btnlbls[1])) {
                        colorEditMode = ImGuiColorEditMode_HSV;
                        ImGui::ColorEditMode(colorEditMode);
                    }
                }
                ImGui::SameLine(0);ImGui::Text("Color Mode");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::ColorEditMode(colorEditMode);
                Style::Edit(this->style);
                ImGui::Separator();
#if             (!defined(NO_IMGUIHELPER) && !defined(NO_IMGUIHELPER_SERIALIZATION))
                const char* saveName = "codeEditor.ce.style";
#               ifndef NO_IMGUIHELPER_SERIALIZATION_SAVE
                if (ImGui::SmallButton("Save##saveGNEStyle")) {
                    Style::Save(this->style,saveName);
                }
                ImGui::SameLine();
#               endif //NO_IMGUIHELPER_SERIALIZATION_SAVE
#               ifndef NO_IMGUIHELPER_SERIALIZATION_LOAD
                if (ImGui::SmallButton("Load##loadGNEStyle")) {
                    Style::Load(this->style,saveName);
                }
                ImGui::SameLine();
#               endif //NO_IMGUIHELPER_SERIALIZATION_LOAD
#               endif //NO_IMGUIHELPER_SERIALIZATION

                if (ImGui::SmallButton("Reset##resetGNEStyle")) {
                    Style::Reset(this->style);
                }
            }
            ImGui::Separator();
        }
#if ((!(defined(NO_IMGUICODEEDITOR_SAVE)) || (!defined(NO_IMGUICODEEDITOR_LOAD))))
    if (show_load_save_buttons) {
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Load/Save File##serialization",NULL,false))   {
        ImGui::Separator();
#       ifdef NO_IMGUIFILESYSTEM
        const char* saveName = "myCodeEditorFile.cpp";
#       ifndef NO_IMGUICODEEDITOR_SAVE
        if (ImGui::SmallButton("Save##saveGNE")) {
            save(saveName);
        }
        ImGui::SameLine();
#       endif //NO_IMGUICODEEDITOR_SAVE
#       ifndef NO_IMGUICODEEDITOR_LOAD
        if (ImGui::SmallButton("Load##loadGNE")) {
            load(saveName);
        }
#		endif //NO_IMGUICODEEDITOR_LOAD
#       else //NO_IMGUIFILESYSTEM
        const char* chosenPath = NULL;                
#       ifndef NO_IMGUICODEEDITOR_LOAD
        static ImGuiFs::Dialog fsInstanceLoad;
        const bool loadButtonPressed = ImGui::SmallButton("Load##loadCE");
	chosenPath = fsInstanceLoad.chooseFileDialog(loadButtonPressed,fsInstanceLoad.getChosenPath(),gTotalLanguageExtensionFilter.c_str()/*fsv ? fsv->languageExtensions: ""*/);
        if (strlen(chosenPath)>0) load(chosenPath);
        ImGui::SameLine();
#		endif //NO_IMGUICODEEDITOR_LOAD
#       ifndef NO_IMGUICODEEDITOR_SAVE
        static ImGuiFs::Dialog fsInstanceSave;
        const bool saveButtonPressed = ImGui::SmallButton("Save##saveCE");
	const FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(lang);
        chosenPath = fsInstanceSave.saveFileDialog(saveButtonPressed,fsInstanceSave.getChosenPath(),fsv ? fsv->languageExtensions: "");
        if (strlen(chosenPath)>0) save(chosenPath);
#       endif //NO_IMGUICODEEDITOR_SAVE
#       endif //NO_IMGUIFILESYSTEM
        }
#       endif // (!(defined(NO_IMGUICODEEDITOR_SAVE) || !defined(NO_IMGUICODEEDITOR_LOAD)))
        ImGui::Separator();
    }
        ImGui::EndChild();


        // horizontal splitter
        ImGui::SameLine(0);
        static const float splitterWidth = 6.f;

        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(1,1,1,0.2f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(1,1,1,0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(1,1,1,0.5f));
        ImGui::Button("##hsplitter1", ImVec2(splitterWidth,-1));
        ImGui::PopStyleColor(3);
        const bool splitterActive = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || splitterActive) {
            leftPaneHasMouseCursor = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (splitterActive)  w += ImGui::GetIO().MouseDelta.x;
        if (splitterActive || windowSizeChanged)  {
            const float minw = ImGui::GetStyle().WindowPadding.x + ImGui::GetStyle().FramePadding.x;
            const float maxw = minw + windowSize.x - splitterWidth - ImGui::GetStyle().WindowMinSize.x - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ItemSpacing.x;
            if (w>maxw)         w = maxw;
            else if (w<minw)    w = minw;
        }
        ImGui::SameLine(0);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, style.color_background);  // The line below is just to set the bg color....
    ImGui::BeginChild("CodeEditorChild", ImVec2(0,0), true, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));

    const ImVec2 windowPos = ImGui::GetWindowPos();
    //const ImVec2 windowSize = ImGui::GetWindowSize();
    const float windowScale = ImGui::GetCurrentWindow()->FontWindowScale;

    const float lineHeight = ImGui::GetTextLineHeight();    // This is the font size too
    int lineStart,lineEnd;
    ImGui::CalcListClipping(lines.size(),lineHeight, &lineStart, &lineEnd);
    // Ensure that lineStart is not hidden
    while (lineStart<lines.size()-1 && lineStart>0 && (lines[lineStart]->isHidden() || (lines[lineStart]->canFoldingBeMergedWithLineAbove() && lines[lineStart]->isFolded()))) {
        if (io.MouseWheel>=0) {--lineStart;/*--lineEnd;*/}
        else {++lineStart;++lineEnd;}
    }
    if (scrollToLine>=0) {
        if (lineStart>scrollToLine)  lineStart = scrollToLine;
        else if (lineEnd<=scrollToLine)   lineEnd = scrollToLine+1;
        else scrollToLine = -1;   // we reset it now
        if (scrollToLine>=0)  {
            //TODO: ensure that scrollLine is visible by unfolding lines
        }
    }
    if (lineEnd>=lines.size()) lineEnd = lines.size()-1;

    const ImVec2 btnSize(lineHeight,lineHeight);    // used in showIconMagin
    float lineNumberSize = 0;                       // used in showLineNumbers
    int precision =  0;
    if (showLineNumbers)    { // No time for a better approach
        ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_line_numbers]));
        if (lineEnd<9)          {precision=1;lineNumberSize=ImGui::MyCalcTextWidth("9");}
        else if (lineEnd<99)    {precision=2;lineNumberSize=ImGui::MyCalcTextWidth("99");}
        else if (lineEnd<999)   {precision=3;lineNumberSize=ImGui::MyCalcTextWidth("999");}
        else if (lineEnd<9999)  {precision=4;lineNumberSize=ImGui::MyCalcTextWidth("9999");}
        else if (lineEnd<99999) {precision=5;lineNumberSize=ImGui::MyCalcTextWidth("99999");}
        else if (lineEnd<999999){precision=6;lineNumberSize=ImGui::MyCalcTextWidth("999999");}
        else                    {precision=7;lineNumberSize=ImGui::MyCalcTextWidth("9999999");}
        ImGui::PopFont();
    }
    ImGui::PushFont(const_cast<ImFont*>(ImFonts[FONT_STYLE_NORMAL]));
    const float sizeFoldingMarks = ImGui::MyCalcTextWidth("   ");    // Wrong, but no time to fix it
    const ImVec2 startCursorPosIconMargin(ImGui::GetCursorPosX(),ImGui::GetCursorPosY() + (lineStart * lineHeight));
    const ImVec2 startCursorPosLineNumbers(startCursorPosIconMargin.x + (showIconMargin ? btnSize.x : 0.f),startCursorPosIconMargin.y);
    const ImVec2 startCursorPosFoldingMarks(startCursorPosLineNumbers.x + (enableTextFolding ? lineNumberSize : 0.f),startCursorPosLineNumbers.y);
    const ImVec2 startCursorPosTextEditor(startCursorPosFoldingMarks.x + sizeFoldingMarks,startCursorPosFoldingMarks.y);

    visibleLines.clear();

    // Draw text and fill visibleLines
    {
        ImGui::SetCursorPos(startCursorPosTextEditor);
        ImGui::BeginGroup();
        gIsCursorChanged = false;
        const float folded_region_contour_thickness = style.folded_region_contour_thickness * windowScale;
        ImGui::PushStyleColor(ImGuiCol_Text,style.color_text);
        ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_text]));
        bool mustSkipNextVisibleLine = false;
        for (int i = lineStart;i<=lineEnd;i++) {
            if (i>=lines.size()) break;
            if (i==scrollToLine) ImGui::SetScrollPosHere();
            Line* line = lines[i];
            if (line->isHidden()) {
                //fprintf(stderr,"line %d is hidden\n",line->lineNumber+1); // This seems to happen on "//endregion" lines only
                ++lineEnd;
                continue;
            }

            if (mustSkipNextVisibleLine) mustSkipNextVisibleLine = false;
            else visibleLines.push_back(line);

            gCurlineStartedWithDiesis = gIsCurlineHovered = false;
            gCurline = line;
            // ImGui::PushID(line);// ImGui::PopID();
            //if (line->isFoldable()) {fprintf(stderr,"Line[%d] is foldable\n",line->lineNumber);}

            if (line->isFoldable()) {
                const int startOffset = (line->isFoldingEnd() && line->foldingStartLine->isFolded()) ? (line->foldingEndOffset+line->foldingStartLine->foldingStartTag->end.size()) : 0;
                if (line->isFolded()) {
                    // start line of a folded region

                    // draw sh text before the folding point
                    if (line->foldingStartOffset>0) {
                        this->TextLineWithSH("%.*s",line->foldingStartOffset-startOffset,&line->text[startOffset]);
                        ImGui::SameLine(0,0);
                    }

                    // draw the folded tag
                    SyntaxHighlightingType sht = FoldingTypeToSyntaxHighlightingType[line->foldingStartTag->kind];
                    if (style.color_syntax_highlighting[sht]>>24==0)    {
                        if (line->foldingStartTag->kind==FOLDING_TYPE_COMMENT) sht = SH_COMMENT;
                        else if (line->foldingStartTag->kind==FOLDING_TYPE_PARENTHESIS) {
                            if (line->foldingStartTag->start[0]=='{') sht = SH_BRACKETS_CURLY;
                            else if (line->foldingStartTag->start[0]=='[') sht = SH_BRACKETS_SQUARE;
                            else if (line->foldingStartTag->start[0]=='(') sht = SH_BRACKETS_ROUND;
                        }
                    }
                    ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]));
                    ImGui::PushStyleColor(ImGuiCol_Text,ImColor(style.color_syntax_highlighting[sht]));
                    if (line->foldingStartTag->title.size()>0) {
                        // See if we must draw the bg:
                        const ImU32 bgColor = line->foldingStartTag->kind == FOLDING_TYPE_COMMENT ? style.color_folded_comment_background : line->foldingStartTag->kind == FOLDING_TYPE_PARENTHESIS ? style.color_folded_parenthesis_background : 0;
                        if (bgColor >> 24 !=0)	{
                            const ImVec2 regionNameSize = ImGui::CalcTextSize(line->foldingStartTag->title.c_str());    // Well, it'a a monospace font... we can optimize it
                            ImVec2 startPos = ImGui::GetCursorPos();
                            startPos.x+= windowPos.x - ImGui::GetScrollX() - lineHeight*0.09f;
                            startPos.y+= windowPos.y - ImGui::GetScrollY();
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            ImGui::ImDrawListAddRect(drawList,startPos,ImVec2(startPos.x+regionNameSize.x+lineHeight*0.25f,startPos.y+regionNameSize.y),bgColor,0,0.f,0x0F,0);
                        }
                        // Normal folding
                        ImGui::Text("%s",line->foldingStartTag->title.c_str());
                        ImGui::SameLine(0,0);
                        mustSkipNextVisibleLine = true;++lineEnd;
                    }
                    else {
                        // Custom region
                        const char* regionName = &line->text[line->foldingStartOffset+line->foldingStartTag->start.size()];
                        if (regionName && regionName[0]!='\0')  {
                            // Draw frame around text
                            const ImVec2 regionNameSize = ImGui::CalcTextSize(regionName);    // Well, it'a a monospace font... we can optimize it
                            ImVec2 startPos = ImGui::GetCursorPos();
                            startPos.x+= windowPos.x - ImGui::GetScrollX() - lineHeight*0.18f;
                            startPos.y+= windowPos.y - ImGui::GetScrollY();
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            ImGui::ImDrawListAddRect(drawList,startPos,ImVec2(startPos.x+regionNameSize.x+lineHeight*0.5f,startPos.y+regionNameSize.y),ImColor(style.color_folded_region_background),ImColor(style.color_syntax_highlighting[sht]),0.f,0x0F,folded_region_contour_thickness);

                            // Draw text
                            ImGui::Text("%s",regionName);
                        }
                    }
                    i = line->foldingEndLine->lineNumber-1;lineEnd+=line->foldingEndLine->lineNumber-line->lineNumber-1;
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                else if (line->foldingStartTag->kind==FOLDING_TYPE_COMMENT) {
                    // Open multiline comment starting line

                    // draw sh text before the folding point (that is not folded now)
                    if (line->foldingStartOffset>0) {
                        this->TextLineWithSH("%.*s",line->foldingStartOffset-startOffset,&line->text[startOffset]);
                        ImGui::SameLine(0,0);
                    }

                    // draw the rest of the line directly as SH_COMMENT (using ImGui::Text(...), without parsing it with this->TextLineWithSH(...))
                    const SyntaxHighlightingType sht = SH_COMMENT;
                    ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]));
                    ImGui::PushStyleColor(ImGuiCol_Text,ImColor(style.color_syntax_highlighting[sht]));
                    ImGui::Text("%s",&line->text[line->foldingStartOffset]);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                else this->TextLineWithSH("%s",&line->text[startOffset]);
            }
            else if (line->isFoldingEnd())  {
                if (line->foldingStartLine->isFolded()) {
                    // End line of a folded region. Here we must just display what's left after the foldngEndOffset.
                    this->TextLineWithSH("%s",&line->text[line->foldingEndOffset+line->foldingStartLine->foldingStartTag->end.size()]);
                    //fprintf(stderr,"Line[%d] is a folded folding end\n",line->lineNumber);
                }
                else if (line->foldingStartLine->foldingStartTag->kind==FOLDING_TYPE_COMMENT) {
                    // Open multiline comment ending line

                    // draw the start of the line directly as SH_COMMENT (using ImGui::Text(...), without parsing it with this->TextLineWithSH(...))
                    const SyntaxHighlightingType sht = SH_COMMENT;
                    ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]));
                    ImGui::PushStyleColor(ImGuiCol_Text,ImColor(style.color_syntax_highlighting[sht]));
                    ImGui::Text("%.*s",line->foldingEndOffset+line->foldingStartLine->foldingStartTag->end.size(),line->text.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();

                    // draw the end of the line parsed
                    ImGui::SameLine(0,0);
                    this->TextLineWithSH("%s",&line->text[line->foldingEndOffset+line->foldingStartLine->foldingStartTag->end.size()]);
                }
                else this->TextLineWithSH("%s",line->text.c_str());
            }
            else if (line->foldingStartLine && line->foldingStartLine->foldingStartTag->kind==FOLDING_TYPE_COMMENT) {
                // Internal lines of an unfolded comment region
                const SyntaxHighlightingType sht = SH_COMMENT;
                ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]));
                ImGui::PushStyleColor(ImGuiCol_Text,ImColor(style.color_syntax_highlighting[sht]));
                ImGui::Text("%s",line->text.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            else this->TextLineWithSH("%s",line->text.c_str());

            const bool nextLineMergeble = (i+1<lines.size() && lines[i+1]->canFoldingBeMergedWithLineAbove() && lines[i+1]->isFolded());
            if (nextLineMergeble) {
                ImGui::SameLine(0,0);
                mustSkipNextVisibleLine=true;++lineEnd;
                //fprintf(stderr,"Line[%d] can be merged to the next\n",lines[i]->lineNumber);
            }
        }
        gCurlineStartedWithDiesis = gIsCurlineHovered = false;gCurline = NULL;    // Reset temp variables
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndGroup();
        if (!leftPaneHasMouseCursor)    {
            if (!gIsCursorChanged) {
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
                else ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            }
        }        
    }

    //if (visibleLines.size()>0) fprintf(stderr,"visibleLines.size()=%d firstVisibleLine=%d lastVisibleLine=%d\n",visibleLines.size(),visibleLines[0]->lineNumber+1,visibleLines[visibleLines.size()-1]->lineNumber+1);

    // Draw icon margin
    if (showIconMargin && ImGui::GetScrollX()<startCursorPosLineNumbers.x) {
        ImGui::SetCursorPos(startCursorPosIconMargin);
        // Draw background --------------------------------------
        if (style.color_icon_margin_background>>24!=0)    {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            //const float deltaX = windowScale * ImGui::GetStyle().WindowPadding.x;   // I'm not sure it's ImGui::GetStyle().WindowPadding.x *2.f...
            const ImVec2 ssp(startCursorPosIconMargin.x+windowPos.x-ImGui::GetScrollX()-lineHeight*0.25f,startCursorPosIconMargin.y+windowPos.y-ImGui::GetScrollY());
            const ImVec2 sep(ssp.x+lineHeight*1.25f,ssp.y+ lineHeight*visibleLines.size());
            drawList->AddRectFilled(ssp,sep,style.color_icon_margin_background);
        }
        //-------------------------------------------------------
        ImVec2 startPos,endPos,screenStartPos,screenEndPos;
        const float icon_margin_contour_thickness = style.icon_margin_contour_thickness * windowScale;
        ImGui::BeginGroup();
        for (int i = 0,isz=visibleLines.size();i<isz;i++) {
            Line* line = visibleLines[i];

            ImGui::PushID(line);
            startPos = ImGui::GetCursorPos();
            screenStartPos.x = startPos.x + windowPos.x - ImGui::GetScrollX() + lineHeight*0.75f;//windowScale * ImGui::GetStyle().WindowPadding.x;
            screenStartPos.y = startPos.y + windowPos.y - ImGui::GetScrollY();
            if (ImGui::InvisibleButton("##DummyButton",btnSize))    {
                if (io.KeyCtrl) {
                    line->attributes^=Line::AT_BOOKMARK;
                    fprintf(stderr,"AT_BOOKMARK(%d) = %s\n",i,(line->attributes&Line::AT_BOOKMARK)?"true":"false");
                }
                else {
                    line->attributes^=Line::AT_BREAKPOINT;
                    fprintf(stderr,"AT_BREAKPOINT(%d) = %s\n",i,(line->attributes&Line::AT_BREAKPOINT)?"true":"false");
                }
            }
            endPos = ImGui::GetCursorPos();
            screenEndPos.x = endPos.x + windowPos.x - ImGui::GetScrollX();
            screenEndPos.y = endPos.y + windowPos.y - ImGui::GetScrollY();
            if (line->attributes&Line::AT_ERROR) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImGui::ImDrawListAddRect(drawList,screenStartPos,screenEndPos,style.color_icon_margin_error,style.color_icon_margin_contour,0.f,0x0F,icon_margin_contour_thickness);
            }
            else if (line->attributes&Line::AT_WARNING) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImGui::ImDrawListAddRect(drawList,screenStartPos,screenEndPos,style.color_icon_margin_warning,style.color_icon_margin_contour,0.f,0x0F,icon_margin_contour_thickness);
            }
            if (line->attributes&Line::AT_BREAKPOINT) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 center((screenStartPos.x+screenEndPos.x)*0.5f,(screenStartPos.y+screenEndPos.y)*0.5f);
                const float radius = (screenEndPos.y-screenStartPos.y)*0.4f;
                //drawList->AddCircleFilled(center,radius,style.color_margin_breakpoint);
                //drawList->AddCircle(center,radius,style.color_margin_contour);
                ImGui::ImDrawListAddCircle(drawList,center,radius,style.color_icon_margin_breakpoint,style.color_icon_margin_contour,12,icon_margin_contour_thickness);

            }
            if (line->attributes&Line::AT_BOOKMARK) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 center((screenStartPos.x+screenEndPos.x)*0.5f,(screenStartPos.y+screenEndPos.y)*0.5f);
                const float radius = (screenEndPos.y-screenStartPos.y)*0.25f;
                const ImVec2 a(center.x-radius,center.y-radius);
                const ImVec2 b(center.x+radius,center.y+radius);
                //ImGui::ImDrawListAddRect(drawList,a,b,style.color_margin_bookmark,style.color_margin_contour);
                ImGui::ImDrawListAddRect(drawList,a,b,style.color_icon_margin_bookmark,style.color_icon_margin_contour,0.f,0x0F,icon_margin_contour_thickness);
            }
            ImGui::SetCursorPos(endPos);

            if (ImGui::IsItemHovered())	{
                // to remove (dbg)
		ImGui::SetTooltip("Line:%d offset=%u offsetInUTF8chars=%u\nsize()=%d numUTF8chars=%d isFoldingStart=%s\nisFoldingEnd=%s isFolded=%s isHidden=%s\nfoldingStartLine=%d foldingEndLine=%d",line->lineNumber+1,line->offset,line->offsetInUTF8chars,
                                  line->text.size(),line->numUTF8chars,
                                  line->isFoldable()?"true":"false",
                                  (line->attributes&Line::AT_FOLDING_END)?"true":"false",
                                  line->isFolded()?"true":"false",
                                  line->isHidden()?"true":"false",
                                  line->foldingStartLine ? line->foldingStartLine->lineNumber+1 : -1,
                                  line->foldingEndLine ? line->foldingEndLine->lineNumber+1 : -1
                                                   );
            }

            ImGui::PopID();
        }
        ImGui::EndGroup();
        ImGui::SameLine(0,0);
    }
    //else fprintf(stderr,"Hidden: %1.2f\n",ImGui::GetScrollX());

     // Draw line numbers
    if (showLineNumbers && ImGui::GetScrollX()<startCursorPosFoldingMarks.x) {
        ImGui::SetCursorPos(startCursorPosLineNumbers);
        ImGui::BeginGroup();
        // Draw background --------------------------------------
        if (style.color_line_numbers_background>>24!=0)    {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 ssp(startCursorPosLineNumbers.x+windowPos.x-ImGui::GetScrollX(),startCursorPosLineNumbers.y+windowPos.y-ImGui::GetScrollY());
            const ImVec2 sep(ssp.x+lineNumberSize,ssp.y+ lineHeight*visibleLines.size());
            drawList->AddRectFilled(ssp,sep,style.color_line_numbers_background);
        }
        //-------------------------------------------------------
        ImGui::PushStyleColor(ImGuiCol_Text,style.color_line_numbers);
        ImGui::PushFont(const_cast<ImFont*>(ImFonts[style.font_line_numbers]));
        // I've struggled so much to display the line numbers half of their size, with no avail...
        for (int i = 0,isz=visibleLines.size();i<isz;i++) {
            Line* line = visibleLines[i];
            ImGui::Text("%*d",precision,(line->lineNumber+1));
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::SameLine(0,0);
    }
    //else fprintf(stderr,"Hidden: %1.2f\n",ImGui::GetScrollX());

    // Draw folding spaces:
    if (enableTextFolding &&  ImGui::GetScrollX()<startCursorPosTextEditor.x) {
        ImGui::SetCursorPos(startCursorPosFoldingMarks);
        // Draw background --------------------------------------
        if (style.color_folding_margin_background>>24!=0)    {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 ssp(startCursorPosFoldingMarks.x+windowPos.x-ImGui::GetScrollX(),startCursorPosFoldingMarks.y+windowPos.y-ImGui::GetScrollY());
            const ImVec2 sep(ssp.x+sizeFoldingMarks*0.8f,ssp.y+ lineHeight*visibleLines.size());
            drawList->AddRectFilled(ssp,sep,style.color_folding_margin_background);
        }
        //-------------------------------------------------------
        ImGui::BeginGroup();
        ImGui::PushFont(const_cast<ImFont*>(ImFonts[FONT_STYLE_NORMAL]));
        ImGui::PushStyleColor(ImGuiCol_Header,transparent);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,transparent);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,transparent);
        Line *line=NULL,*nextLine=NULL,*foldableLine=NULL;
        for (int i = 0,isz=visibleLines.size();i<isz;i++) {
            line = visibleLines[i];
            //fprintf(stderr,"Line[%d] - %d \n",line->lineNumber,i);
            if (line == nextLine) {nextLine=NULL;ImGui::Text("%s"," ");continue;}
            foldableLine = line->isFoldable() ? line : NULL;
            if (!foldableLine)  {
                nextLine = line->lineNumber+1<lines.size()?lines[line->lineNumber+1]:NULL;
                nextLine = (nextLine && nextLine->canFoldingBeMergedWithLineAbove()) ? nextLine : NULL;
                foldableLine = nextLine;
                //if (foldableLine) fprintf(stderr,"line: %d\n",foldableLine->lineNumber);
            }
            else nextLine = NULL;

            if (foldableLine)
            {
                //fprintf(stderr,"enableTextFolding: Line[%d] is foldable\n",line->lineNumber);
                SyntaxHighlightingType sht = FoldingTypeToSyntaxHighlightingType[foldableLine->foldingStartTag->kind];
                if (style.color_syntax_highlighting[sht]>>24==0)    {
                    if (line->foldingStartTag->kind==FOLDING_TYPE_COMMENT) sht = SH_COMMENT;
                    else if (line->foldingStartTag->kind==FOLDING_TYPE_PARENTHESIS) {
                        if (line->foldingStartTag->start[0]=='{') sht = SH_BRACKETS_CURLY;
                        else if (line->foldingStartTag->start[0]=='[') sht = SH_BRACKETS_SQUARE;
                        else if (line->foldingStartTag->start[0]=='(') sht = SH_BRACKETS_ROUND;
                    }
                }
                const bool wasFolded = foldableLine->isFolded();
                ImGui::PushStyleColor(ImGuiCol_Text,ImColor(style.color_syntax_highlighting[sht]));
                ImGui::SetNextTreeNodeOpened(!wasFolded,ImGuiSetCond_Always);
                if (!ImGui::TreeNode(line,"%s",""))  {
                    if (!wasFolded)   {
                        // process next lines to make them visible someway
                        foldableLine->setFoldedState(true);
                        Line::AdjustHiddenFlagsForFoldingStart(lines,*foldableLine);
                        /*if (nextLineIsFoldableAndMergeble && foldableLine!=nextLine && !nextLine->isFolded()) {
                            nextLine->setFoldedState(true);
                            Line::AdjustHiddenFlagsForFoldingStart(lines,*nextLine);
                        }*/
                    }
                }
                else {
                    ImGui::TreePop();
                    if (wasFolded)   {
                        // process next lines to make them invisible someway
                        foldableLine->setFoldedState(false);
                        Line::AdjustHiddenFlagsForFoldingStart(lines,*foldableLine);
                        /*if (nextLineIsFoldableAndMergeble && foldableLine!=nextLine && nextLine->isFolded()) {
                            nextLine->setFoldedState(false);
                            Line::AdjustHiddenFlagsForFoldingStart(lines,*nextLine);
                        }*/
                    }
                }
                ImGui::PopStyleColor();
            }
            else {
                ImGui::Text("%s"," ");
                //fprintf(stderr,"enableTextFolding: Line[%d] is not foldable\n",line->lineNumber);
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SameLine(0,0);
    }
    //else fprintf(stderr,"Hidden: %1.2f\n",ImGui::GetScrollX());

    ImGui::PopFont();
    ImGui::PopStyleVar();
    scrollToLine=-1;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ((lines.size() - lineEnd) * lineHeight));

    ImGui::EndChild();      // "CodeEditorChild"
    ImGui::PopStyleColor(); // style.color_background

}


void CodeEditor::TextLineUnformattedWithSH(const char* text, const char* text_end)  {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    IM_ASSERT(text != NULL);
    const char* text_begin = text;
    if (text_end == NULL) text_end = text + strlen(text); // FIXME-OPT

    {

        // Account of baseline offset
        ImVec2 text_pos = window->DC.CursorPos;
        text_pos.y += window->DC.CurrentLineTextBaseOffset;

        // I would like to remove the call to CalcText(...) here (in "text_size"), and I could simply retrieve
        // bb.Min after the call to RenderTextLineWrappedWithSH(...) to calculate it for free...
        // But the truth is that RenderTextLineWrappedWithSH(...) is using "gIsCurlineHovered"
        // to detect if it can make more expensive processing to detect when mouse is hovering over tokens.

        // Maybe here we can set "gIsCurlineHovered" only if mouse is in the y range and > x0.
        // Is it possible to do it ? How ?

        // Idea: Do it only if "ImGui::GetIO().KeyCtrl" is down.

        if (ImGui::GetIO().KeyCtrl) {
            // TO FIX: Folded code when KeyCtrl is down appears shifted one tab to the right. Why ?

            const ImVec2 text_size(ImGui::MyCalcTextWidth(text_begin, text_end),ImGui::GetTextLineHeight());
            ImRect bb(text_pos, text_pos + text_size);
            ImGui::ItemSize(text_size);
            if (!ImGui::ItemAdd(bb, NULL))  return;
            gIsCurlineHovered = ImGui::IsItemHovered();

            // Render (we don't hide text after ## in this end-user function)
            RenderTextLineWrappedWithSH(bb.Min, text_begin, text_end);
        }
        else {
            const ImVec2 old_text_pos = text_pos;

            gIsCurlineHovered = false;
            RenderTextLineWrappedWithSH(text_pos, text_begin, text_end);

            const ImVec2 text_size(text_pos.x-old_text_pos.x,ImGui::GetTextLineHeight());
            ImRect bb(old_text_pos, old_text_pos + text_size);
            ImGui::ItemSize(text_size);
            if (!ImGui::ItemAdd(bb, NULL))  return;
            gIsCurlineHovered = ImGui::IsItemHovered();
        }
    }
}
template <int NUM_TOKENS> inline static const char* FindNextToken(const char* text,const char* text_end,const char* token_start[NUM_TOKENS],const char* token_end[NUM_TOKENS],int* pTokenIndexOut=NULL,const char* optionalStringDelimiters=NULL,const char stringEscapeChar='\\',bool skipEscapeChar=false) {
    if (pTokenIndexOut) *pTokenIndexOut=-1;
    const char *pt,*t,*tks,*tke;
    int optionalStringDelimitersSize = optionalStringDelimiters ? strlen(optionalStringDelimiters) : -1;
    for (const char* p = text;p!=text_end;p++)  {
	if (token_start)    {
	    for (int j=0;j<NUM_TOKENS;j++)  {
		tks = token_start[j];
		tke = token_end[j];
		t = tks;
		pt = p;
		while (*t++==*pt++) {
		    if (t==tke) {
			if (pTokenIndexOut) *pTokenIndexOut=j;
			return p;
		    }
		}
		//fprintf(stderr,"%d) \"%s\"\n",j,tks);
	    }
	}
	if (optionalStringDelimiters)	{
	    for (int j=0;j<optionalStringDelimitersSize;j++)	{
		if (*p == optionalStringDelimiters[j])	{
		    if (skipEscapeChar || p == text || *(p-1)!=stringEscapeChar) {
			if (pTokenIndexOut) *pTokenIndexOut=NUM_TOKENS + j;
			return p;
		    }
		}
	    }
	}
    }
    return NULL;
}
void CodeEditor::RenderTextLineWrappedWithSH(ImVec2& pos, const char* text, const char* text_end, bool skipLineCommentAndStringProcessing)
{
    ImGuiState& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    int text_len = (int)(text_end - text);
    if (!text_end)  text_end = text + text_len; // FIXME-OPT
    if (text==text_end) return;

    const FoldingStringVector* fsv = GetGlobalFoldingStringVectorForLanguage(this->lang);
    if (!fsv || lang==LANG_NONE)	{
        const int text_len = (int)(text_end - text);
        if (text_len > 0)   {
            ImGui::ImDrawListAddTextLine(window->DrawList,g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), text, text_end);
            if (g.LogEnabled) LogRenderedText(pos, text, text_end);
        }
        return;
    }

    if (!skipLineCommentAndStringProcessing) {
        const char* startComments[3] = {fsv->singleLineComment,fsv->multiLineCommentStart,fsv->multiLineCommentEnd};
        const char* endComments[3] = {fsv->singleLineComment+strlen(fsv->singleLineComment),fsv->multiLineCommentStart+strlen(fsv->multiLineCommentStart),fsv->multiLineCommentEnd+strlen(fsv->multiLineCommentEnd)};
        //for (int j=0;j<3;j++) fprintf(stderr,"%d) \"%s\"\n",j,startComments[j]);
        int tki=-1;
        const char* tk = FindNextToken<2>(text,text_end,startComments,endComments,&tki,fsv->stringDelimiterChars,fsv->stringEscapeChar,false);
        if (tk) {
            if (tki==0) {   // Found "//"
                if (tk!=text) RenderTextLineWrappedWithSH(pos,text,tk,true);    // Draw until "//"
                text = tk;
                const int text_len = (int)(text_end - text);
                if (text_len > 0)   {
                    ImGui::ImDrawListAddTextLine(window->DrawList,const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[SH_COMMENT]]), g.FontSize, pos, style.color_syntax_highlighting[SH_COMMENT], text, text_end);
                    if (g.LogEnabled) LogRenderedText(pos, text, text_end);
                }
                return;
            }
            else if (tki==1) {	// Found "/*"
                if (tk!=text) RenderTextLineWrappedWithSH(pos,text,tk,true);    // Draw until "/*"
                const int lenOpenCmn = strlen(startComments[1]);
                const char* endCmt = text_end;
                const char* tk2 = FindNextToken<1>(tk+lenOpenCmn,text_end,&startComments[2],&endComments[2]);	// Look for "*/"
                if (tk2) endCmt = tk2+strlen(startComments[2]);
                text = tk;
                const int text_len = (int)(endCmt - text);
                if (text_len > 0)   {
                    ImGui::ImDrawListAddTextLine(window->DrawList,const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[SH_COMMENT]]), g.FontSize, pos, style.color_syntax_highlighting[SH_COMMENT], text, endCmt);
                    if (g.LogEnabled) LogRenderedText(pos, text, endCmt);
                }
                if (tk2 && endCmt<text_end) RenderTextLineWrappedWithSH(pos,endCmt,text_end);    // Draw after "*/"
                return;
            }
            else if (tki>1) {	// Found " (or ')
                tki = tki-2;	// Found fsv->stringDelimiterChars[tki]
                if (tk!=text) RenderTextLineWrappedWithSH(pos,text,tk,true);    // Draw until "
                //const bool mustSkipNextEscapeChar = (lang==LANG_CS && tk>text && *(tk-1)=='@');   // Nope, I don't think this matters...
                if (tk+1<text_end)  {
                    // Here I have to match exactly fsv->stringDelimiterChars[tki], not another! (we don't use FindNextToken<>)
                    const char *tk2 = NULL,*tk2Start = tk+1;
                    while ((tk2 = strchr(tk2Start,fsv->stringDelimiterChars[tki])))   {
                        if (tk2>=text_end) {tk2 = NULL;break;}
                        tk2Start = tk2+1;
                        if (tk2>text && *(tk2-1)==fsv->stringEscapeChar) continue;
                        break;
                    }
                    const char* endStringSH = tk2==NULL ? text_end : (tk2+1);
                    // Draw String:
                    const ImVec2 oldPos = pos;
                    ImGui::ImDrawListAddTextLine(window->DrawList,const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[SH_STRING]]), g.FontSize, pos, style.color_syntax_highlighting[SH_STRING], tk, endStringSH);
                    if (g.LogEnabled) LogRenderedText(pos, tk, endStringSH);
                    const float token_width = pos.x - oldPos.x;  //MyCalcTextWidth(tk,endStringSH);
                    // TEST: Mouse interaction on token (gIsCurlineHovered == true when CTRL is pressed)-------------------
                    if (gIsCurlineHovered) {
                        // See if we can strip 2 chars
                        const ImVec2 token_size(token_width,g.FontSize);
                        const ImVec2& token_pos = oldPos;
                        ImRect bb(token_pos, token_pos + token_size);
                        if (ImGui::ItemAdd(bb, NULL) && ImGui::IsItemHovered()) {
                            window->DrawList->AddLine(ImVec2(bb.Min.x,bb.Max.y), bb.Max, style.color_syntax_highlighting[SH_STRING], 2.f);
                            //if (ImGui::GetIO().MouseClicked[0])  {fprintf(stderr,"Mouse clicked on token: \"%s\"(%d->\"%s\") curlineStartedWithDiesis=%s line=\"%s\"\n",s,len_tok,tok,curlineStartedWithDiesis?"true":"false",curline->text.c_str());}
                            ImGui::SetTooltip("Token (quotes are included): %.*s\nSH = %s\nLine (%d):\"%s\"\nLine starts with '#': %s",(int)(endStringSH-tk),tk,SyntaxHighlightingTypeStrings[SH_STRING],gCurline->lineNumber+1,gCurline->text.c_str(),gCurlineStartedWithDiesis?"true":"false");
                            gIsCursorChanged = true;ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                        }
                    }
                    // ----------------------------------------------------------------------------------------------------
                    //pos.x+=token_width;
                    if (tk2==NULL)	return;	// No other match found
                    text = endStringSH;
                    if (text!=text_end) {
                        //fprintf(stderr,"\"%.*s\"\n",(int)(text_end-text),text);
                        RenderTextLineWrappedWithSH(pos,text,text_end);
                    }
                    return;
                }
            }
            return;
        }
    }



    const char sp = ' ';const char tab='\t';
    const char *s = text;
    bool firstTokenHasPreprocessorStyle = false;	// hack to handle lines such as "#  include <...>" in cpp
    bool lineStartsWithDiesis = false;
    // Process the start of the line

    // skip tabs and spaces
    while (*s==sp || *s==tab)	{
    if (s+1==text_end)  {
        ImGui::ImDrawListAddTextLine(window->DrawList,g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), text, text_end);
        if (g.LogEnabled) LogRenderedText(pos, text, text_end);
        return;
    }
    ++s;
    }
    if (s>text)	{
        // Draw Tabs and spaces
        ImGui::ImDrawListAddTextLine(window->DrawList,g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), text, s);
        if (g.LogEnabled) LogRenderedText(pos, text, s);
        text=s;
    }

    // process special chars at the start of the line (e.g. "//" or '#' in cpp)
    if (s<text_end) {
        // Handle '#' in Cpp
        if (*s=='#')	{
            gCurlineStartedWithDiesis = lineStartsWithDiesis = true;
            if (lang==LANG_CPP && s+1<text_end && (*(s+1)==sp || *(s+1)==tab)) firstTokenHasPreprocessorStyle = true;
        }
    }



    // Tokenise
    IM_ASSERT(fsv->punctuationStringsMerged!=NULL && strlen(fsv->punctuationStringsMerged)>0);
    static char Text[2048];
    IM_ASSERT(text_end-text<2048);
    strncpy(Text,text,text_end-text);	// strtok is destructive
    Text[text_end-text]='\0';
    s=text;
    char* tok = strtok(Text,fsv->punctuationStringsMerged),*oldTok=Text;
    int offset = 0,len_tok=0,num_tokens=0;
    short int tokenIsNumber = 0;
    while (tok) {
        offset = tok-oldTok;
        if (offset>0) {
            // Print Punctuation
            /*window->DrawList->AddText(g.Font, g.FontSize, pos, GetColorU32(ImGuiCol_Button), s, s+offset, wrap_width);
    if (g.LogEnabled) LogRenderedText(pos, s, s+offset);
    //pos.x+=charWidth*(offset);
    pos.x+=CalcTextWidth(s, s+offset).x;*/
            for (int j=0;j<offset;j++)  {
                const char* ch = s+j;
                int sht = -1;
                if (tokenIsNumber && *ch=='.') {sht = SH_NUMBER;++tokenIsNumber;}
                if (sht==-1 && !shTypePunctuationMap.get(*ch,sht)) sht = -1;
                if (sht>=0 && sht<SH_COUNT) ImGui::ImDrawListAddTextLine(window->DrawList,const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]), g.FontSize, pos, style.color_syntax_highlighting[sht], s+j, s+j+1);
                else ImGui::ImDrawListAddTextLine(window->DrawList,g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), s+j, s+j+1);
                if (g.LogEnabled) LogRenderedText(pos, s+j, s+j+1);
            }
        }
        s+=offset;
        // Print Token (Syntax highlighting through HashMap here)
        len_tok = strlen(tok);if (--tokenIsNumber<0) tokenIsNumber=0;
        if (len_tok>0)  {
            int sht = -1;	// Mandatory assignment
            // Handle special starting tokens
            if ((firstTokenHasPreprocessorStyle && num_tokens<2) || (lang==LANG_CPP && lineStartsWithDiesis && strcmp(tok,"defined")==0))	sht = SH_KEYWORD_PREPROCESSOR;
            if (sht==-1)	{
                // Handle numbers
                if (tokenIsNumber==0)   {
                    const char *tmp=tok,*tmpEnd=(tok+len_tok);
                    for (tmp=tok;tmp!=tmpEnd;++tmp)	{
                        if ((*tmp)<'0' || (*tmp)>'9')	{
                            if ((tmp+2 == tmpEnd) && (*tmp)=='.')  tokenIsNumber = 1;	// TODO: What if '.' is a token splitter char ?
                            break;
                        }
                    }
                    if (tmp==tmpEnd) tokenIsNumber = 1;
                }
                if (tokenIsNumber) sht = SH_NUMBER;
            }
            const ImVec2 oldPos = pos;
            if (sht>=0 || shTypeKeywordMap.get(tok,sht)) {
                //fprintf(stderr,"Getting shTypeMap: \"%s\",%d\n",tok,sht);
                ImGui::ImDrawListAddTextLine(window->DrawList,const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]), g.FontSize, pos, style.color_syntax_highlighting[sht], tok, tok+len_tok);
            }
            else {
                //fprintf(stderr,"Not Getting shTypeMap: \"%s\",%d\n",tok,sht);
                ImGui::ImDrawListAddTextLine(window->DrawList,g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), tok, tok+len_tok);
            }
            if (g.LogEnabled) LogRenderedText(pos, tok, tok+len_tok);
            const float token_width = pos.x - oldPos.x;//MyCalcTextWidth(tok,tok+len_tok);   // We'll use this later
            // TEST: Mouse interaction on token (gIsCurlineHovered == true when CTRL is pressed)-------------------
            if (gIsCurlineHovered) {
                const ImVec2 token_size(token_width,g.FontSize);// = ImGui::CalcTextSize(tok, tok+len_tok, false, 0.f);
                const ImVec2& token_pos = oldPos;
                ImRect bb(token_pos, token_pos + token_size);
                if (ImGui::ItemAdd(bb, NULL) && ImGui::IsItemHovered()) {
                    window->DrawList->AddLine(ImVec2(bb.Min.x,bb.Max.y), bb.Max, sht>=0 ? style.color_syntax_highlighting[sht] : ImGui::GetColorU32(ImGuiCol_Text), 2.f);
                    if (ImGui::GetIO().MouseClicked[0])  {fprintf(stderr,"Mouse clicked on token: \"%s\"(%d->\"%s\") curlineStartedWithDiesis=%s line=\"%s\"\n",s,len_tok,tok,gCurlineStartedWithDiesis?"true":"false",gCurline->text.c_str());}
                    ImGui::SetTooltip("Token: \"%s\" len=%d\nToken unclamped: \"%s\"\nSH = %s\nLine (%d):\"%s\"\nLine starts with '#': %s",tok,len_tok,s,sht<0 ? "None" : SyntaxHighlightingTypeStrings[sht],gCurline->lineNumber+1,gCurline->text.c_str(),gCurlineStartedWithDiesis?"true":"false");
                    gIsCursorChanged = true;ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                }
            }
            // ----------------------------------------------------------------------------------------------------
        }
        //printf("Token: %s\n", tok);
        oldTok = tok+len_tok;s+=len_tok;
        tok = strtok(NULL,fsv->punctuationStringsMerged);

        ++num_tokens;
    }

    offset = text_end-s;
    if (offset>0) {
        // Print Punctuation
        //window->DrawList->AddText(g.Font, g.FontSize, pos, GetColorU32(ImGuiCol_Button), s, s+offset, wrap_width);
        //if (g.LogEnabled) LogRenderedText(pos, s, s+offset);
        for (int j=0;j<offset;j++)  {
            const char* ch = s+j;
            int sht = -1;
            if (tokenIsNumber && *ch=='.') {sht = SH_NUMBER;++tokenIsNumber;}
            if (sht==-1 && !shTypePunctuationMap.get(*ch,sht)) sht = -1;
            if (sht>=0 && sht<SH_COUNT) ImGui::ImDrawListAddTextLine(window->DrawList,const_cast<ImFont*>(ImFonts[style.font_syntax_highlighting[sht]]), g.FontSize, pos, style.color_syntax_highlighting[sht], s+j, s+j+1);
            else ImGui::ImDrawListAddTextLine(window->DrawList,g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), s+j, s+j+1);
            if (g.LogEnabled) LogRenderedText(pos, s+j, s+j+1);
        }
    }

}




} // namespace ImGuiCe


#undef IMGUI_NEW
#undef IMGUI_DELETE
