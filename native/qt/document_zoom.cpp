#include "document_zoom.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QElapsedTimer>
#include <cstdio>
#include <unordered_map>
#include <cmath>

namespace wolfmark::qt {
namespace {
template <typename Format>
Format scaled(Format format, double ratio) {
    for (const auto property : {QTextFormat::FontPointSize, QTextFormat::FontPixelSize,
            QTextFormat::BlockTopMargin, QTextFormat::BlockBottomMargin,
            QTextFormat::BlockLeftMargin, QTextFormat::BlockRightMargin, QTextFormat::TextIndent,
            QTextFormat::FrameTopMargin, QTextFormat::FrameBottomMargin,
            QTextFormat::FrameLeftMargin, QTextFormat::FrameRightMargin, QTextFormat::FramePadding,
            QTextFormat::TableCellTopPadding, QTextFormat::TableCellBottomPadding,
            QTextFormat::TableCellLeftPadding, QTextFormat::TableCellRightPadding}) {
        if (format.hasProperty(property))
            format.setProperty(property, format.doubleProperty(property) * ratio);
    }
    return format;
}
} // namespace

void DocumentZoom::capture(QTextDocument* document) {
    document_ = document;
    font_ = document->defaultFont();
    spans_.clear();
    blocks_.clear();
    frames_.clear();
    cells_.clear();
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        blocks_.push_back({block.position(), block.blockFormat(), block.charFormat(),
                           block.blockFormatIndex(), block.charFormatIndex()});
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.charFormat().isImageFormat())
                spans_.push_back({fragment.position(), fragment.length(), fragment.charFormat(), fragment.charFormatIndex()});
        }
    }
    for (auto* frame : document->rootFrame()->childFrames()) captureFrame(frame);
}

void DocumentZoom::captureFrame(QTextFrame* frame) {
    frames_.push_back({frame, frame->frameFormat()});
    if (auto* table = qobject_cast<QTextTable*>(frame)) {
        for (int row = 0; row < table->rows(); ++row)
            for (int column = 0; column < table->columns(); ++column) {
                const auto cell = table->cellAt(row, column);
                cells_.push_back({cell, cell.format().toTableCellFormat()});
            }
    }
    for (auto* child : frame->childFrames()) captureFrame(child);
}

void DocumentZoom::apply(int percent) {
    if (!document_) return;
    const double ratio = percent / 100.0;
    QElapsedTimer timer;
    timer.start();
    auto font = font_;
    font.setPointSizeF(font_.pointSizeF() * ratio);
    document_->setDefaultFont(font);
    QTextCursor cursor(document_);
    cursor.beginEditBlock();
    std::unordered_map<int, QTextCharFormat> characters;
    std::unordered_map<int, QTextBlockFormat> blocks;
    const auto character = [&](int index, const QTextCharFormat& baseline) -> const QTextCharFormat& {
        auto [it, inserted] = characters.try_emplace(index);
        if (inserted) it->second = scaled(baseline, ratio);
        return it->second;
    };
    for (const auto& block : blocks_) {
        cursor.setPosition(block.position);
        auto [it, inserted] = blocks.try_emplace(block.format_index);
        if (inserted) {
            it->second = scaled(block.format, ratio);
            auto tabs = it->second.tabPositions();
            for (auto& tab : tabs) tab.position *= ratio;
            it->second.setTabPositions(tabs);
        }
        // Percentage line heights and 1px presentation rules deliberately stay unchanged.
        cursor.setBlockFormat(it->second);
        cursor.setBlockCharFormat(character(block.character_index, block.character));
    }
    for (const auto& span : spans_) {
        cursor.setPosition(span.position);
        cursor.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(character(span.format_index, span.format));
    }
    const auto text_us = timer.nsecsElapsed() / 1000;
    for (const auto& frame : frames_) {
        if (!frame.frame) continue;
        if (auto* table = qobject_cast<QTextTable*>(frame.frame.data())) {
            auto format = scaled(frame.format.toTableFormat(), ratio);
            auto widths = format.columnWidthConstraints();
            for (auto& width : widths)
                if (width.type() == QTextLength::FixedLength)
                    width = QTextLength(QTextLength::FixedLength, width.rawValue() * ratio);
            format.setColumnWidthConstraints(widths);
            table->setFormat(format);
        } else {
            frame.frame->setFrameFormat(scaled(frame.format, ratio));
        }
    }
    for (auto& cell : cells_) cell.cell.setFormat(scaled(cell.format, ratio));
    cursor.endEditBlock();
    if (qEnvironmentVariableIsSet("WOLFMARK_PROFILE"))
        std::fprintf(stdout, "ZOOM_FORMAT text_us=%lld total_us=%lld\n",
                     static_cast<long long>(text_us), static_cast<long long>(timer.nsecsElapsed() / 1000));
}

bool DocumentZoom::matches(int percent) const {
    if (!document_ || spans_.empty()) return false;
    const double ratio = percent / 100.0;
    for (const auto& span : spans_) {
        QTextCursor cursor(document_);
        cursor.setPosition(span.position);
        cursor.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
        const auto actual = cursor.charFormat();
        if (std::abs(actual.fontPointSize() - span.format.fontPointSize() * ratio) > 0.001 ||
            actual.fontWeight() != span.format.fontWeight() ||
            actual.fontItalic() != span.format.fontItalic() ||
            actual.anchorHref() != span.format.anchorHref()) return false;
    }
    for (const auto& block : blocks_) {
        const auto actual = document_->findBlock(block.position).blockFormat();
        if (std::abs(actual.leftMargin() - block.format.leftMargin() * ratio) > 0.001 ||
            std::abs(actual.bottomMargin() - block.format.bottomMargin() * ratio) > 0.001 ||
            actual.lineHeight() != block.format.lineHeight()) return false;
    }
    for (const auto& frame : frames_) {
        if (!frame.frame || std::abs(frame.frame->frameFormat().padding() - frame.format.padding() * ratio) > 0.001)
            return false;
    }
    for (const auto& cell : cells_) {
        const auto actual = cell.cell.format().toTableCellFormat();
        if (std::abs(actual.leftPadding() - cell.format.leftPadding() * ratio) > 0.001 ||
            std::abs(actual.topPadding() - cell.format.topPadding() * ratio) > 0.001) return false;
    }
    return true;
}
} // namespace wolfmark::qt
