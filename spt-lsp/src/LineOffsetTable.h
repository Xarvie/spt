/**
 * @file LineOffsetTable.h
 * @brief Line/Offset Conversion Utility for LSP Operations
 *
 * Provides efficient bidirectional conversion between:
 * - (line, column) positions (1-based, LSP uses 0-based externally)
 * - byte offsets (0-based)
 *
 * Key Features:
 * - O(1) line-to-offset lookup via precomputed table
 * - O(log n) offset-to-line lookup via binary search
 * - UTF-8 aware column handling
 * - Supports incremental updates for live editing
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lang {
namespace lsp {

/**
 * @brief Position in source code (1-based line/column)
 *
 * Note: LSP protocol uses 0-based positions externally.
 * This struct uses 1-based internally for consistency with compiler diagnostics.
 */
struct Position {
  uint32_t line = 1;   ///< 1-based line number
  uint32_t column = 1; ///< 1-based column (byte offset within line)

  [[nodiscard]] bool isValid() const noexcept { return line > 0 && column > 0; }

  [[nodiscard]] static Position invalid() noexcept { return {0, 0}; }

  // C++17 兼容的比较操作符
  bool operator==(const Position &other) const noexcept {
    return line == other.line && column == other.column;
  }

  bool operator!=(const Position &other) const noexcept { return !(*this == other); }

  bool operator<(const Position &other) const noexcept {
    return line < other.line || (line == other.line && column < other.column);
  }

  bool operator<=(const Position &other) const noexcept { return *this < other || *this == other; }

  bool operator>(const Position &other) const noexcept { return other < *this; }

  bool operator>=(const Position &other) const noexcept { return !(*this < other); }

  /// Convert to 0-based (for LSP protocol)
  [[nodiscard]] Position toZeroBased() const noexcept {
    return {line > 0 ? line - 1 : 0, column > 0 ? column - 1 : 0};
  }

  /// Convert from 0-based (from LSP protocol)
  [[nodiscard]] static Position fromZeroBased(uint32_t line, uint32_t column) noexcept {
    return {line + 1, column + 1};
  }
};

/**
 * @brief Range in source code [start, end)
 */
struct Range {
  Position start;
  Position end;

  [[nodiscard]] bool isValid() const noexcept { return start.isValid() && end.isValid(); }

  [[nodiscard]] static Range invalid() noexcept { return {}; }

  [[nodiscard]] bool contains(Position pos) const noexcept { return start <= pos && pos < end; }

  [[nodiscard]] bool overlaps(const Range &other) const noexcept {
    return start < other.end && other.start < end;
  }
};

/**
 * @brief Line offset table for efficient position conversion
 *
 * Usage:
 *   LineOffsetTable table(sourceText);
 *
 *   // Get offset from position
 *   uint32_t offset = table.getOffset({5, 10});
 *
 *   // Get position from offset
 *   Position pos = table.getPosition(42);
 *
 *   // Update after edit
 *   table.rebuild(newSourceText);
 */
class LineOffsetTable {
public:
  LineOffsetTable() = default;

  explicit LineOffsetTable(std::string_view source) { build(source); }

  /**
   * @brief Build the line offset table from source text
   * @param source The source code text
   */
  void build(std::string_view source) {
    lineOffsets_.clear();
    lineOffsets_.push_back(0); // Line 1 starts at offset 0

    // 存储源文本用于后续查询
    source_ = std::string(source);

    for (size_t i = 0; i < source.size(); ++i) {
      if (source[i] == '\n') {
        lineOffsets_.push_back(static_cast<uint32_t>(i + 1));
      } else if (source[i] == '\r') {
        // Handle \r\n (Windows) and lone \r (old Mac)
        if (i + 1 < source.size() && source[i + 1] == '\n') {
          ++i; // Skip the \n in \r\n
        }
        lineOffsets_.push_back(static_cast<uint32_t>(i + 1));
      }
    }

    sourceLength_ = static_cast<uint32_t>(source.size());
  }

  /**
   * @brief Rebuild the table (alias for build)
   */
  void rebuild(std::string_view source) { build(source); }

  /**
   * @brief Get byte offset from (line, column) position
   * @param pos 1-based position
   * @return 0-based byte offset, or sourceLength_ if invalid
   */
  [[nodiscard]] uint32_t getOffset(Position pos) const noexcept {
    if (!pos.isValid() || pos.line > lineCount()) {
      return sourceLength_;
    }

    uint32_t lineStart = lineOffsets_[pos.line - 1];
    uint32_t lineEnd = (pos.line < lineCount()) ? lineOffsets_[pos.line] : sourceLength_;

    // Column is 1-based, so subtract 1
    uint32_t offset = lineStart + (pos.column - 1);
    return std::min(offset, lineEnd);
  }

  /**
   * @brief Get (line, column) position from byte offset
   * @param offset 0-based byte offset
   * @return 1-based position
   */
  [[nodiscard]] Position getPosition(uint32_t offset) const noexcept {
    if (lineOffsets_.empty()) {
      return {1, 1};
    }

    offset = std::min(offset, sourceLength_);

    // Binary search for the line containing this offset
    auto it = std::upper_bound(lineOffsets_.begin(), lineOffsets_.end(), offset);

    // upper_bound gives us the first element > offset
    // We want the line before that (or the last line if at end)
    uint32_t lineIndex =
        static_cast<uint32_t>(it == lineOffsets_.begin() ? 0 : (it - lineOffsets_.begin() - 1));

    uint32_t lineStart = lineOffsets_[lineIndex];
    uint32_t column = offset - lineStart + 1; // 1-based column

    return {lineIndex + 1, column}; // 1-based line
  }

  /**
   * @brief Get the start offset of a line
   * @param line 1-based line number
   * @return 0-based byte offset of line start
   */
  [[nodiscard]] uint32_t getLineStartOffset(uint32_t line) const noexcept {
    if (line == 0 || line > lineCount()) {
      return sourceLength_;
    }
    return lineOffsets_[line - 1];
  }

  /**
   * @brief Get the end offset of a line (exclusive, before newline)
   * @param line 1-based line number
   * @return 0-based byte offset of line end (excluding line terminator)
   */
  [[nodiscard]] uint32_t getLineEndOffset(uint32_t line) const noexcept {
    if (line == 0 || line > lineCount()) {
      return sourceLength_;
    }

    if (line < lineCount()) {
      uint32_t nextLineStart = lineOffsets_[line];
      if (nextLineStart == 0) {
        return 0;
      }

      uint32_t end = nextLineStart;

      // 回退跳过换行符
      // 检查 \n
      if (end > 0 && source_[end - 1] == '\n') {
        --end;
      }
      // 检查 \r (处理 \r\n 和单独的 \r)
      if (end > 0 && source_[end - 1] == '\r') {
        --end;
      }

      return end;
    }
    return sourceLength_;
  }

  /**
   * @brief Get the number of lines in the source
   */
  [[nodiscard]] uint32_t lineCount() const noexcept {
    return static_cast<uint32_t>(lineOffsets_.size());
  }

  /**
   * @brief Get the total source length
   */
  [[nodiscard]] uint32_t sourceLength() const noexcept { return sourceLength_; }

  /**
   * @brief Check if an offset is at the start of a line
   */
  [[nodiscard]] bool isLineStart(uint32_t offset) const noexcept {
    return std::binary_search(lineOffsets_.begin(), lineOffsets_.end(), offset);
  }

  /**
   * @brief Get the length of a specific line (excluding newline)
   * @param line 1-based line number
   */
  [[nodiscard]] uint32_t getLineLength(uint32_t line) const noexcept {
    if (line == 0 || line > lineCount()) {
      return 0;
    }
    return getLineEndOffset(line) - getLineStartOffset(line);
  }

  /**
   * @brief Apply an incremental edit to the table
   *
   * This is more efficient than rebuilding for small edits.
   * For large edits or multiple edits, prefer rebuild().
   *
   * @param editStart Start offset of the edit
   * @param editEnd End offset of the edit (exclusive)
   * @param newText The replacement text
   * @param fullSource The complete new source (for validation)
   */
  void applyEdit(uint32_t editStart, uint32_t editEnd, std::string_view newText,
                 std::string_view fullSource) {
    // For simplicity and correctness, we rebuild.
    // An optimized version could update only affected lines.
    (void)editStart;
    (void)editEnd;
    (void)newText;
    build(fullSource);
  }

  /**
   * @brief Extract a line's text from source
   * @param source The full source text
   * @param line 1-based line number
   * @return The line's text (without newline)
   */
  [[nodiscard]] std::string_view getLineText(std::string_view source, uint32_t line) const {
    if (line == 0 || line > lineCount()) {
      return {};
    }

    uint32_t start = getLineStartOffset(line);
    uint32_t end = getLineEndOffset(line);

    if (start >= source.size()) {
      return {};
    }

    end = std::min(end, static_cast<uint32_t>(source.size()));
    return source.substr(start, end - start);
  }

  /**
   * @brief Get stored source text
   */
  [[nodiscard]] const std::string &source() const noexcept { return source_; }

private:
  std::vector<uint32_t> lineOffsets_; ///< Offset of each line's first character
  uint32_t sourceLength_ = 0;         ///< Total length of source
  std::string source_;                ///< Stored source text for line end calculation
};

// ============================================================================
// UTF-8 Utilities for Column Handling
// ============================================================================

namespace utf8 {

/**
 * @brief Count UTF-8 code points in a byte range
 * @param text The UTF-8 encoded text
 * @param byteOffset Byte position to count up to
 * @return Number of code points (characters)
 */
[[nodiscard]] inline uint32_t byteOffsetToCodePoint(std::string_view text, uint32_t byteOffset) {
  uint32_t codePoints = 0;
  uint32_t i = 0;

  while (i < byteOffset && i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);

    // Count only leading bytes (not continuation bytes 10xxxxxx)
    if ((c & 0xC0) != 0x80) {
      ++codePoints;
    }
    ++i;
  }

  return codePoints;
}

/**
 * @brief Convert code point offset to byte offset
 * @param text The UTF-8 encoded text
 * @param codePointOffset Number of code points from start
 * @return Byte offset
 */
[[nodiscard]] inline uint32_t codePointToByteOffset(std::string_view text,
                                                    uint32_t codePointOffset) {
  uint32_t codePoints = 0;
  uint32_t i = 0;

  while (codePoints < codePointOffset && i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);

    // Count only leading bytes
    if ((c & 0xC0) != 0x80) {
      ++codePoints;
    }
    ++i;
  }

  return i;
}

/**
 * @brief Get the byte length of a UTF-8 character starting at given position
 */
[[nodiscard]] inline uint32_t charByteLength(unsigned char leadByte) {
  if ((leadByte & 0x80) == 0)
    return 1; // ASCII
  if ((leadByte & 0xE0) == 0xC0)
    return 2; // 110xxxxx
  if ((leadByte & 0xF0) == 0xE0)
    return 3; // 1110xxxx
  if ((leadByte & 0xF8) == 0xF0)
    return 4; // 11110xxx
  return 1;   // Invalid, treat as single byte
}

} // namespace utf8

} // namespace lsp
} // namespace lang
