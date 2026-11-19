#pragma once
#include <QBuffer>
#include <QDialog>
#include <QPdfDocument>

class QComboBox;
class QLabel;
class QPdfView;
class QPushButton;
class QSpinBox;
class QPrinter;

namespace shadowlogis {
// Holds one immutable PDF snapshot: saving always copies the bytes being viewed.
class DocumentPreview final : public QDialog {
public:
    DocumentPreview(QByteArray pdf, const QString &title, QString suggestedName,
                    QWidget *parent = nullptr);
    ~DocumentPreview() override;
    void saveCopy(const QString &path) const;
    void printTo(QPrinter &printer);
private:
    QBuffer buffer;
    QPdfDocument document;
    QString filename;
    QPdfView *view;
    QSpinBox *page;
    QComboBox *zoom;
    QLabel *pageCount, *message;
    QPushButton *previous, *next, *save, *print;
    void updateState();
    void chooseSavePath();
    void choosePrinter();
};
}
