#include "core/SkShader.h"
#include "include/gpu/GrBackendSurface.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <core/SkCanvas.h>
#include <core/SkColor.h>
#include <core/SkData.h>
#include <core/SkFontMgr.h>
#include <core/SkPaint.h>
#include <core/SkScalar.h>
#include <cstring>
#include <include/core/SkColorSpace.h>
#include <include/gpu/GrDirectContext.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/gl/GrGLInterface.h>
#include <include/ports/SkFontMgr_data.h>
#include <iostream>
#include <modules/skparagraph/include/DartTypes.h>
#include <modules/skparagraph/include/Paragraph.h>
#include <modules/skparagraph/include/ParagraphBuilder.h>
#include <modules/skparagraph/include/ParagraphStyle.h>
#include <modules/skparagraph/src/ParagraphBuilderImpl.h>

#include <filesystem>
#include <fstream>

#include <modules/skparagraph/include/FontCollection.h>
#include <modules/skparagraph/include/ParagraphPainter.h>
#include <modules/skparagraph/include/TextStyle.h>
#include <modules/skparagraph/include/TypefaceFontProvider.h>

#include "src/core/SkOSFile.h"
#include <modules/skunicode/include/SkUnicode.h>
#include <stdexcept>
#include <tools/Resources.h>

using namespace skia::textlayout;
namespace fs = std::filesystem;

class ResourceFontCollection : public FontCollection {
public:
  ResourceFontCollection(bool testOnly = false)
      : fFontsFound(false), fResolvedFonts(0),
        fResourceDir("/home/ysl/Code/skparagraph_demo/build/fonts"),
        fFontProvider(sk_make_sp<TypefaceFontProvider>()) {
    std::vector<SkString> fonts;

    for (const auto &ent : std::filesystem::recursive_directory_iterator(
             "/usr/share/fonts/TTF")) {
      fonts.emplace_back(fs::path(ent).string());
    }

    for (const auto &ent : std::filesystem::recursive_directory_iterator(
             "/home/ysl/Code/skparagraph_demo/build/fonts")) {
      fonts.emplace_back(fs::path(ent).string());
    }

    fFontsFound = true;
    if (!fFontsFound) {
      // SkDebugf("Fonts not found, skipping all the tests\n");
      return;
    }
    // Only register fonts if we have to
    for (auto &font : fonts) {
      auto typeface = SkTypeface::MakeFromFile(font.c_str());
      if (typeface) {
        fFontProvider->registerTypeface(typeface);
        std::cout << font.c_str() << std::endl;
      }
    }

    if (testOnly) {
      this->setTestFontManager(std::move(fFontProvider));
    } else {
      this->setAssetFontManager(std::move(fFontProvider));
    }
    this->disableFontFallback();
  }

  size_t resolvedFonts() const { return fResolvedFonts; }

  // TODO: temp solution until we check in fonts
  bool fontsFound() const { return fFontsFound; }

private:
  bool fFontsFound;
  size_t fResolvedFonts;
  std::string fResourceDir;
  sk_sp<TypefaceFontProvider> fFontProvider;
};

class TestCanvas {
public:
  TestCanvas(SkCanvas *canvas) : canvas(canvas) {
    canvas->clear(SK_ColorWHITE);
  }

  void drawRects(SkColor color, std::vector<TextBox> &result,
                 bool fill = false) {

    SkPaint paint;
    if (!fill) {
      paint.setStyle(SkPaint::kStroke_Style);
      paint.setAntiAlias(true);
      paint.setStrokeWidth(1);
    }
    paint.setColor(color);
    for (auto &r : result) {
      canvas->drawRect(r.rect, paint);
    }
  }

  void drawLine(SkColor color, SkRect rect, bool vertical = true) {

    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setAntiAlias(true);
    paint.setStrokeWidth(1);
    paint.setColor(color);
    if (vertical) {
      canvas->drawLine(rect.fLeft, rect.fTop, rect.fLeft, rect.fBottom, paint);
    } else {
      canvas->drawLine(rect.fLeft, rect.fTop, rect.fRight, rect.fTop, paint);
    }
  }

  void drawLines(SkColor color, std::vector<TextBox> &result) {

    for (auto &r : result) {
      drawLine(color, r.rect);
    }
  }

  SkCanvas *get() { return canvas; }

private:
  SkBitmap bits;
  SkCanvas *canvas;
  const char *name;
};

sk_sp<ResourceFontCollection> fontCollection =
    sk_make_sp<ResourceFontCollection>();

inline sk_sp<SkData> GetBinFromFile(const std::filesystem::path fileName) {
  return SkData::MakeFromFileName(fileName.string().c_str());
}

template <typename F> void runOnUtf8(const char *utf8, size_t bytes, F &&f) {

  const char *cur = utf8;
  const char *next_char = nullptr;
  int char_count = 0;
  int bytes_count = 0;

  auto next_utf8_char = [](unsigned char *p_begin,
                           int &char_count) -> const char * {
    if (*p_begin >> 7 == 0) {
      char_count = 1;
      ++p_begin;
    } else if (*p_begin >> 5 == 6 && p_begin[1] >> 6 == 2) {
      p_begin += 2;
      char_count = 1;
    } else if (*p_begin >> 4 == 0x0E && p_begin[1] >> 6 == 2 &&
               p_begin[2] >> 6 == 2) {
      p_begin += 3;
      char_count = 1;
    } else if (*p_begin >> 3 == 0x1E && p_begin[1] >> 6 == 2 &&
               p_begin[2] >> 6 == 2 && p_begin[3] >> 6 == 2) {
      p_begin += 4;
      char_count = 2;
    } else {
      // TODO::invalid utf8
    }
    return (const char *)p_begin;
  };
  while ((next_char = next_utf8_char((unsigned char *)cur, char_count))) {
    bytes_count += (next_char - cur);
    if (bytes_count > bytes)
      break;
    f(cur, next_char, char_count);
    cur = next_char;
  }
}

struct Style {
  int length = 0;
  int fontSize = 14;
  float wordSpacing = 1.0;
  float letterSpacing = 1.0;
};
TextStyle createStyle(const Style &s,
                      const std::vector<SkString> &fontFamilies) {
  TextStyle txtStyle;
  txtStyle.setColor(SK_ColorBLACK);
  txtStyle.setFontFamilies({SkString("Roboto"), SkString("Noto Color Emoji")});
  txtStyle.setFontSize(s.fontSize);
  txtStyle.setLetterSpacing(s.letterSpacing);
  txtStyle.setWordSpacing(s.wordSpacing);
  txtStyle.setHeight(100);
  return txtStyle;
}

std::unique_ptr<ParagraphBuilder>
fromTab(int count, int fontSize, const std::vector<SkString> &fontFamilies) {
  // cassert(count < 40);
  ParagraphStyle style;
  style.setEllipsis(u"...");
  style.setTextAlign(TextAlign::kLeft);
  TextStyle txtStyle;
  txtStyle.setFontFamilies(fontFamilies);
  txtStyle.setFontSize(fontSize);
  style.setTextAlign(TextAlign::kLeft);
  style.setTextStyle(txtStyle);
  auto builder = ParagraphBuilder::make(style, fontCollection);
  char tabs[40];
  memset(tabs, '\t', count);
  tabs[count] = '\0';
  builder->addText(tabs);
  return builder;
}

const std::string text2 = "The quick brown fox 🦊 ate a zesty hamburgerfons "
                          "🍔.\nThe 👩‍👩‍👧‍👧 laughed.";

// const std::string text2 =
// "uuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuffffff"
//                           "ffffffffffffffffffffffffffffffff";
std::vector<Style> styles{
    Style{.length = 6, .fontSize = 20, .wordSpacing = 2.0, .letterSpacing = 10},
    Style{
        .length = 2, .fontSize = 80, .wordSpacing = 2.0, .letterSpacing = 3.0},
    Style{
        .length = 11, .fontSize = 50, .wordSpacing = 2.0, .letterSpacing = 3.0},
    Style{.length = 11,
          .fontSize = 50,
          .wordSpacing = 2.0,
          .letterSpacing = 3.0}};

std::vector<SkString> fontFamilies{SkString("Roboto"),
                                   SkString("Noto Color Emoji")};

std::unique_ptr<ParagraphBuilder>
fromParagraph(const std::vector<Style> &styles, const std::string &utf8,
              int maxLines, TextAlign align) {
  ParagraphStyle style;
  style.setEllipsis(u"...");
  style.setTextAlign(align);
  style.setMaxLines(maxLines);
  // style.setHeight(40);

  auto builder = ParagraphBuilder::make(style, fontCollection);
  int styleIndex = 0;
  if (styles.size() >= 1) {
    const char *last = utf8.c_str();
    int offset = styles[styleIndex].length;
    int bytes = utf8.size();
    int char_index = 0;
    runOnUtf8(utf8.c_str(), bytes,
              [&](const char *begin, const char *end, int char_count) {
                char_index += char_count;
                if (char_index == offset) {
                  builder->pushStyle(
                      createStyle(styles[styleIndex], fontFamilies));
                  builder->addText(last, (end - last));
                  builder->pop();
                  styleIndex++;
                  if (styleIndex < styles.size())
                    offset += styles[styleIndex].length;
                  last = end;
                }
              });
    if (last - utf8.c_str() < bytes) {
      // no style for last text segment
      TextStyle txtStyle;
      txtStyle.setColor(SK_ColorBLACK);
      txtStyle.setFontFamilies(fontFamilies);
      txtStyle.setFontSize(28);
      builder->pushStyle(txtStyle);
      builder->addText(last);
    }
  }

  return std::move(builder);
}

struct LayoutState {
  SkScalar cursorX = 0.f;
  SkScalar cursorY = 0.f;
  SkScalar layoutWidth = 500.f;

  void advanceX(SkScalar a) {
    cursorX += a;
    layoutWidth -= a;
  }
  void advanceY(SkScalar a) { cursorY += a; }
};

void drawParagraphDebugInfo(TestCanvas &canvas, Paragraph *p,
                            const LayoutState &state) {

  canvas.get()->save();
  canvas.get()->translate(state.cursorX, state.cursorY);
  auto rects =
      p->getRectsForRange(0, 100, RectHeightStyle::kMax, RectWidthStyle::kMax);
  auto h = p->getHeight();
  auto mw = p->getMaxWidth();
  auto iw = p->getMaxIntrinsicWidth();
  SkPaint pen;
  pen.setColor(SK_ColorGREEN);
  canvas.get()->drawRect(SkRect{0, 0, mw, h}, pen);
  // pen.setColor(SK_ColorBLUE);
  // canvas.get()->drawRect(SkRect{0, 0, iw, h}, pen);
  canvas.drawRects(SK_ColorRED, rects);
  canvas.get()->restore();
  p->paint(canvas.get(), state.cursorX, state.cursorY);
}

void drawParagraph(TestCanvas &canvas) {

  LayoutState state;

  auto builder = fromTab(6, 80, fontFamilies);
  auto p = builder->Build();
  p->layout(state.layoutWidth);
  auto iw = p->getMaxIntrinsicWidth();
  drawParagraphDebugInfo(canvas, p.get(), state);

  state.advanceX(iw);
  builder = fromParagraph(styles, text2, 1000, TextAlign::kLeft);
  p = builder->Build();
  p->layout(state.layoutWidth);
  p->paint(canvas.get(), iw, 0);
  drawParagraphDebugInfo(canvas, p.get(), state);
}

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    return -1;
  }
  SDL_version compileVersion, linkVersion;
  SDL_VERSION(&compileVersion);
  SDL_GetVersion(&linkVersion);

  // setup opengl properties
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  auto m_width = 1920;
  auto m_height = 1080;
  auto window = SDL_CreateWindow(
      nullptr, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    return -1;
  }
  // create context
  SDL_GLContext glContext = SDL_GL_CreateContext(window);
  if (!glContext) {
    return -1;
  }
  if (SDL_GL_MakeCurrent(window, glContext) != 0) {
    return -1;
  }
  sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
  if (!interface) {
    return -1;
  }
  sk_sp<GrDirectContext> grContext = GrDirectContext::MakeGL(interface);
  if (!grContext) {
    return -1;
  }

  GrGLFramebufferInfo info;
  info.fFBOID = 0;
  // GR_GL_GetIntegerv(m_skiaState.interface.get(),
  //                   GR_GL_FRAMEBUFFER_BINDING,
  //                   (GrGLint*)&info.fFBOID);

  // color type and info format must be the followings for
  // both OpenGL and OpenGL ES, otherwise it will fail
  info.fFormat = 0x8058;
  GrBackendRenderTarget target(m_width, m_height, 0, 8, info);

  SkSurfaceProps props;
  auto surface = SkSurfaces::WrapBackendRenderTarget(
      grContext.get(), target, kBottomLeft_GrSurfaceOrigin,
      SkColorType::kRGBA_8888_SkColorType, nullptr, &props);

  auto canvas = surface->getCanvas();
  bool noexit = true;

  // init vars

  TestCanvas testCanvas(canvas);
  fontCollection->setDefaultFontManager(SkFontMgr::RefDefault());
  fontCollection->enableFontFallback();

  while (noexit) {
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
      if (evt.type == SDL_QUIT) {
        noexit = false;
        break;
      }
    }

    canvas->clear(SK_ColorWHITE);
    drawParagraph(testCanvas);
    canvas->flush();
    SDL_GL_SwapWindow(window);
  }

  return 0;
}
