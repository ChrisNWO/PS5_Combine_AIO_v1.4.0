#pragma once
#include <QTextBrowser>

#include "PageBase.h"

class AboutPage : public PageBase
{
    Q_OBJECT
public:
    explicit AboutPage(QWidget *parent = nullptr);
protected:
    void onRetranslate() override { fill(); }
private:
    void fill();
    QTextBrowser *view_;
};
