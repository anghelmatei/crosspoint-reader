#include "ChapterHtmlSlimParser.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <JpegToBmpConverter.h>
#include <SDCardManager.h>
#include <expat.h>

#include <cctype>

#include "../Page.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

// Minimum file size (in bytes) to show progress bar - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_PROGRESS = 50 * 1024;  // 50KB

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* IMAGE_TAGS[] = {"img", "image"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

bool hasJpegExtension(const std::string& path) {
  auto endsWithIgnoreCase = [](const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
      const char a = static_cast<char>(std::tolower(value[value.size() - suffix.size() + i]));
      const char b = static_cast<char>(std::tolower(suffix[i]));
      if (a != b) return false;
    }
    return true;
  };

  return endsWithIgnoreCase(path, ".jpg") || endsWithIgnoreCase(path, ".jpeg");
}

// given the start and end of a tag, check to see if it matches a known tag
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

// flush the contents of partWordBuffer to currentTextBlock
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  // determine font style
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (boldUntilDepth < depth && italicUntilDepth < depth) {
    fontStyle = EpdFontFamily::BOLD_ITALIC;
  } else if (boldUntilDepth < depth) {
    fontStyle = EpdFontFamily::BOLD;
  } else if (italicUntilDepth < depth) {
    fontStyle = EpdFontFamily::ITALIC;
  }
  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(partWordBuffer, fontStyle);
  partWordBufferIndex = 0;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const TextBlock::Style style) {
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->setStyle(style);
      return;
    }

    makePages();
  }
  currentTextBlock.reset(new ParsedText(style, extraParagraphSpacing, hyphenationEnabled));
}

void ChapterHtmlSlimParser::flushCurrentTextBlock() {
  if (!currentTextBlock) {
    currentTextBlock.reset(new ParsedText((TextBlock::Style)paragraphAlignment, extraParagraphSpacing,
                                          hyphenationEnabled));
    return;
  }

  if (!currentTextBlock->isEmpty()) {
    makePages();
  }
  currentTextBlock.reset(new ParsedText((TextBlock::Style)paragraphAlignment, extraParagraphSpacing,
                                        hyphenationEnabled));
}

void ChapterHtmlSlimParser::addImageToPage(const std::string& bmpPath, uint16_t width, uint16_t height) {
  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  int drawWidth = width;
  int drawHeight = height;
  if (drawHeight > viewportHeight) {
    const float scale = static_cast<float>(viewportHeight) / static_cast<float>(drawHeight);
    drawHeight = viewportHeight;
    drawWidth = static_cast<int>(drawWidth * scale);
  }
  if (drawWidth > viewportWidth) {
    const float scale = static_cast<float>(viewportWidth) / static_cast<float>(drawWidth);
    drawWidth = viewportWidth;
    drawHeight = static_cast<int>(drawHeight * scale);
  }

  if (currentPageNextY + drawHeight > viewportHeight && currentPageNextY > 0) {
    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  int16_t xPos = 0;
  if (drawWidth < viewportWidth) {
    xPos = static_cast<int16_t>((viewportWidth - drawWidth) / 2);
  }

  currentPage->elements.push_back(
      std::make_shared<PageImage>(bmpPath, static_cast<uint16_t>(drawWidth), static_cast<uint16_t>(drawHeight), xPos,
                                  currentPageNextY));
  currentPageNextY += drawHeight;

  if (extraParagraphSpacing) {
    const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
    currentPageNextY += lineHeight / 2;
  }
}

std::string ChapterHtmlSlimParser::resolveImageHref(const std::string& src) const {
  if (src.empty()) return {};

  std::string cleaned = src;
  const auto hashPos = cleaned.find_first_of("#?");
  if (hashPos != std::string::npos) {
    cleaned = cleaned.substr(0, hashPos);
  }

  if (cleaned.rfind("http://", 0) == 0 || cleaned.rfind("https://", 0) == 0 ||
      cleaned.rfind("data:", 0) == 0) {
    return {};
  }

  if (cleaned.empty()) return {};

  if (!cleaned.empty() && cleaned.front() == '/') {
    cleaned.erase(cleaned.begin());
    const auto base = epub ? epub->getBasePath() : std::string();
    if (!base.empty()) {
      return FsHelpers::normalisePath(base + "/" + cleaned);
    }
    return FsHelpers::normalisePath(cleaned);
  }

  std::string baseDir;
  const auto slashPos = itemHref.find_last_of('/');
  if (slashPos != std::string::npos) {
    baseDir = itemHref.substr(0, slashPos);
  }

  if (!baseDir.empty()) {
    return FsHelpers::normalisePath(baseDir + "/" + cleaned);
  }
  return FsHelpers::normalisePath(cleaned);
}

bool ChapterHtmlSlimParser::convertImageToBmp(const std::string& href, std::string* outBmpPath, uint16_t* outWidth,
                                              uint16_t* outHeight) {
  if (!epub || href.empty() || !hasJpegExtension(href)) {
    return false;
  }

  const auto cacheDir = epub->getCachePath() + "/images";
  SdMan.mkdir(cacheDir.c_str());

  const auto key = std::to_string(std::hash<std::string>{}(href));
  const auto bmpPath = cacheDir + "/img_" + key + ".bmp";

  if (SdMan.exists(bmpPath.c_str())) {
    FsFile bmpFile;
    if (!SdMan.openFileForRead("EIM", bmpPath, bmpFile)) {
      return false;
    }
    Bitmap bitmap(bmpFile);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      bmpFile.close();
      return false;
    }
    *outWidth = bitmap.getWidth();
    *outHeight = bitmap.getHeight();
    *outBmpPath = bmpPath;
    bmpFile.close();
    return true;
  }

  const auto tmpJpgPath = cacheDir + "/img_" + key + ".jpg";
  if (SdMan.exists(tmpJpgPath.c_str())) {
    SdMan.remove(tmpJpgPath.c_str());
  }

  FsFile tmpJpg;
  if (!SdMan.openFileForWrite("EIM", tmpJpgPath, tmpJpg)) {
    return false;
  }

  bool ok = epub->readItemContentsToStream(href, tmpJpg, 1024);
  tmpJpg.close();
  if (!ok) {
    SdMan.remove(tmpJpgPath.c_str());
    return false;
  }

  FsFile jpgFile;
  if (!SdMan.openFileForRead("EIM", tmpJpgPath, jpgFile)) {
    SdMan.remove(tmpJpgPath.c_str());
    return false;
  }

  FsFile bmpFile;
  if (!SdMan.openFileForWrite("EIM", bmpPath, bmpFile)) {
    jpgFile.close();
    SdMan.remove(tmpJpgPath.c_str());
    return false;
  }

  ok = JpegToBmpConverter::jpegFileToBmpStreamWithSize(jpgFile, bmpFile, viewportWidth, viewportHeight);
  jpgFile.close();
  bmpFile.close();
  SdMan.remove(tmpJpgPath.c_str());

  if (!ok) {
    SdMan.remove(bmpPath.c_str());
    return false;
  }

  FsFile bmpRead;
  if (!SdMan.openFileForRead("EIM", bmpPath, bmpRead)) {
    return false;
  }
  Bitmap bitmap(bmpRead);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    bmpRead.close();
    return false;
  }
  *outWidth = bitmap.getWidth();
  *outHeight = bitmap.getHeight();
  *outBmpPath = bmpPath;
  bmpRead.close();
  return true;
}

bool ChapterHtmlSlimParser::handleImageTag(const std::string& src, const std::string& alt) {
  (void)alt;
  if (!epub) return false;

  const auto resolved = resolveImageHref(src);
  if (resolved.empty()) {
    return false;
  }

  std::string bmpPath;
  uint16_t width = 0;
  uint16_t height = 0;
  if (!convertImageToBmp(resolved, &bmpPath, &width, &height)) {
    return false;
  }

  flushCurrentTextBlock();
  addImageToPage(bmpPath, width, height);
  return true;
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  // Special handling for tables - show placeholder text instead of dropping silently
  if (strcmp(name, "table") == 0) {
    // Add placeholder text
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);

    self->italicUntilDepth = min(self->italicUntilDepth, self->depth);
    // Advance depth before processing character data (like you would for a element with text)
    self->depth += 1;
    self->characterData(userData, "[Table omitted]", strlen("[Table omitted]"));

    // Skip table contents (skip until parent as we pre-advanced depth above)
    self->skipUntilDepth = self->depth - 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    std::string src;
    std::string alt;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }
    }

    const bool handled = !src.empty() && self->handleImageTag(src, alt);
    if (!handled) {
      const std::string altText = alt.empty() ? "[Image]" : "[Image: " + alt + "]";
      Serial.printf("[%lu] [EHP] Image fallback: %s\n", millis(), altText.c_str());
      if (!self->currentTextBlock) {
        self->currentTextBlock.reset(new ParsedText((TextBlock::Style)self->paragraphAlignment,
                                                    self->extraParagraphSpacing, self->hyphenationEnabled));
      }
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->currentTextBlock->addWord(altText, EpdFontFamily::ITALIC);
      self->depth += 1;
      return;
    }

    self->depth += 1;
    return;
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->depth += 1;
    return;
  }

  if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      self->startNewTextBlock(self->currentTextBlock->getStyle());
      self->depth += 1;
      return;
    }

    self->startNewTextBlock(static_cast<TextBlock::Style>(self->paragraphAlignment));
    if (strcmp(name, "li") == 0) {
      self->currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR);
    }

    self->depth += 1;
    return;
  }

  if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->depth += 1;
    return;
  }

  if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    self->depth += 1;
    return;
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Skip the whitespace char
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
    const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
    const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      self->flushPartWordBuffer();
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // If we have > 750 words buffered up, perform the layout and consume out all but the last line
  // There should be enough here to build out 1-2 full pages and doing this will free up a lot of
  // memory.
  // Spotted when reading Intermezzo, there are some really long text blocks in there.
  if (self->currentTextBlock->size() > 750) {
    Serial.printf("[%lu] [EHP] Text block too long, splitting into multiple pages\n", millis());
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->fontId, self->viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (self->partWordBufferIndex > 0) {
    // Only flush out part word buffer if we're closing a block tag or are at the top of the HTML file.
    // We don't want to flush out content when closing inline tags like <span>.
    // Currently this also flushes out on closing <b> and <i> tags, but they are line tags so that shouldn't happen,
    // text styling needs to be overhauled to fix it.
    const bool shouldBreakText =
        matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) || matches(name, HEADER_TAGS, NUM_HEADER_TAGS) ||
        matches(name, BOLD_TAGS, NUM_BOLD_TAGS) || matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) ||
        strcmp(name, "table") == 0 || matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) || self->depth == 1;

    if (shouldBreakText) {
      self->flushPartWordBuffer();
    }
  }

  self->depth -= 1;

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  // Leaving bold
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  // Leaving italic
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  startNewTextBlock((TextBlock::Style)this->paragraphAlignment);

  const XML_Parser parser = XML_ParserCreate(nullptr);
  int done;

  if (!parser) {
    Serial.printf("[%lu] [EHP] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", filepath, file)) {
    XML_ParserFree(parser);
    return false;
  }

  // Get file size for progress calculation
  const size_t totalSize = file.size();
  size_t bytesRead = 0;
  int lastProgress = -1;

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);

  do {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      Serial.printf("[%lu] [EHP] Couldn't allocate memory for buffer\n", millis());
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      file.close();
      return false;
    }

    const size_t len = file.read(buf, 1024);

    if (len == 0 && file.available() > 0) {
      Serial.printf("[%lu] [EHP] File read error\n", millis());
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      file.close();
      return false;
    }

    // Update progress (call every 10% change to avoid too frequent updates)
    // Only show progress for larger chapters where rendering overhead is worth it
    bytesRead += len;
    if (progressFn && totalSize >= MIN_SIZE_FOR_PROGRESS) {
      const int progress = static_cast<int>((bytesRead * 100) / totalSize);
      if (lastProgress / 10 != progress / 10) {
        lastProgress = progress;
        progressFn(progress);
      }
    }

    done = file.available() == 0;

    if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      Serial.printf("[%lu] [EHP] Parse error at line %lu:\n%s\n", millis(), XML_GetCurrentLineNumber(parser),
                    XML_ErrorString(XML_GetErrorCode(parser)));
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      file.close();
      return false;
    }
  } while (!done);

  XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
  XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  file.close();

  // Process last page if there is still text
  if (currentTextBlock) {
    makePages();
    completePageFn(std::move(currentPage));
    currentPage.reset();
    currentTextBlock.reset();
  }

  return true;
}

void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  if (currentPageNextY + lineHeight > viewportHeight) {
    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageNextY));
  currentPageNextY += lineHeight;
}

void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) {
    Serial.printf("[%lu] [EHP] !! No text block to make pages for !!\n", millis());
    return;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, viewportWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });
  // Extra paragraph spacing if enabled
  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}
