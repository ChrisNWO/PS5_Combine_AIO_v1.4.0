#pragma once
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QWidget>

#include "I18n.h"

// Поле пути + кнопка «Обзор…» + drag&drop файла/папки.
class PathRow : public QWidget
{
    Q_OBJECT
public:
    enum Mode { OpenFile, OpenDir, SaveFile };

    explicit PathRow(Mode m, QWidget *parent = nullptr) : QWidget(parent), mode_(m)
    {
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(8);
        edit_ = new QLineEdit;
        edit_->setAcceptDrops(false);             // дроп обрабатывает сам PathRow
        btn_ = new QPushButton;
        l->addWidget(edit_, 1);
        l->addWidget(btn_);
        setAcceptDrops(true);
        connect(btn_, &QPushButton::clicked, this, &PathRow::browse);
        connect(edit_, &QLineEdit::textChanged, this, &PathRow::textChanged);
        retranslate();
    }

    QString text() const { return edit_->text().trimmed(); }
    void setText(const QString &s) { edit_->setText(s); }
    void setMode(Mode m) { mode_ = m; }
    void setFilter(const QString &f) { filter_ = f; }
    void setPlaceholder(const QString &s) { edit_->setPlaceholderText(s); }
    void retranslate() { btn_->setText(TR("common.browse")); }

signals:
    void textChanged(const QString &);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        const auto urls = e->mimeData()->urls();
        if (!urls.isEmpty()) edit_->setText(QDir::toNativeSeparators(urls.first().toLocalFile()));
    }

private:
    void browse()
    {
        const QString cur = text();
        const QString start = cur.isEmpty() ? QString() : QFileInfo(cur).absolutePath();
        QString r;
        switch (mode_) {
        case OpenFile: r = QFileDialog::getOpenFileName(this, TR("common.choose"), start, filter_); break;
        case OpenDir:  r = QFileDialog::getExistingDirectory(this, TR("common.choose"), start); break;
        case SaveFile: r = QFileDialog::getSaveFileName(this, TR("common.choose"), start, filter_); break;
        }
        if (!r.isEmpty()) edit_->setText(QDir::toNativeSeparators(r));
    }

    Mode mode_;
    QString filter_;
    QLineEdit *edit_ = nullptr;
    QPushButton *btn_ = nullptr;
};
