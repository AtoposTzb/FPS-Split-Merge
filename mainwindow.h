#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QProgressBar>
#include <QLineEdit>
#include <QCheckBox>
#include <QDomDocument>
#include <QMutex>

class XmlProcessor : public QObject
{
    Q_OBJECT
public:
    XmlProcessor() {}

    bool checkXmlFormat(const QString &filePath, QString &errorMsg);
    bool repairXml(const QString &filePath, QString &errorMsg);
    QStringList collectItemsFromFiles(const QStringList &filePaths, bool repair, QTextEdit *log, QProgressBar *progress, QMutex *mutex);
    void mergeFiles(const QStringList &filePaths, const QString &outputPath, bool repair, QTextEdit *log, QProgressBar *progress);
    void splitFile(const QString &filePath, const QString &outputDir, QTextEdit *log, QProgressBar *progress);
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void selectFiles();
    void selectFolder();
    void selectSplitFile();
    void selectOutputFolder();
    void performMerge();
    void performSplit();

private:
    QStringList inputFiles;
    QString inputFolder;
    QString splitFile;
    QLineEdit *outputFolderEdit;
    QLineEdit *mergeNameEdit;
    QCheckBox *repairCheck;
    QTextEdit *logText;
    QProgressBar *progressBar;
    XmlProcessor *processor;
};

#endif // MAINWINDOW_H
