#pragma once
#include <QDialog>

class QLineEdit;
class QListWidget;
class QTextBrowser;
class QLabel;

class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);
private:
    QLineEdit *search;
    QListWidget *topics;
    QTextBrowser *description;
    QLabel *count;
    void filterTopics();
    void showTopic();
};
