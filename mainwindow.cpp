#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QThreadPool>
#include <QRunnable>
#include <QBuffer>
#include <QMetaObject>

bool XmlProcessor::checkXmlFormat(const QString &filePath, QString &errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMsg = QString("无法打开文件: %1").arg(filePath);
        return false;
    }
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        errorMsg = QString("无效的XML文件: %1").arg(filePath);
        file.close();
        return false;
    }
    QDomElement root = doc.documentElement();
    if (root.tagName() != "fps") {
        errorMsg = QString("根节点不是<fps>: %1").arg(filePath);
        file.close();
        return false;
    }
    QDomNodeList items = root.elementsByTagName("item");
    if (items.isEmpty()) {
        errorMsg = QString("未找到<item>节点: %1").arg(filePath);
        file.close();
        return false;
    }
    for (int i = 0; i < items.count(); ++i) {
        QDomElement item = items.at(i).toElement();
        if (item.elementsByTagName("test_input").isEmpty() ||
            item.elementsByTagName("test_output").isEmpty()) {
            errorMsg = QString("缺少<test_input>或<test_output>节点: %1").arg(filePath);
            file.close();
            return false;
        }
    }
    file.close();
    return true;
}

bool XmlProcessor::repairXml(const QString &filePath, QString &errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMsg = QString("无法打开文件进行修复: %1").arg(filePath);
        return false;
    }
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        errorMsg = QString("无法修复无效的XML文件: %1").arg(filePath);
        file.close();
        return false;
    }
    QDomElement root = doc.documentElement();
    if (root.tagName() != "fps") {
        errorMsg = QString("无法修复非<fps>根节点: %1").arg(filePath);
        file.close();
        return false;
    }
    QDomNodeList items = root.elementsByTagName("item");
    for (int i = 0; i < items.count(); ++i) {
        QDomElement item = items.at(i).toElement();
        if (item.elementsByTagName("test_input").isEmpty()) {
            QDomElement newInput = doc.createElement("test_input");
            item.appendChild(newInput);
        }
        if (item.elementsByTagName("test_output").isEmpty()) {
            QDomElement newOutput = doc.createElement("test_output");
            item.appendChild(newOutput);
        }
    }
    file.close();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = QString("无法写入修复后的文件: %1").arg(filePath);
        return false;
    }
    QTextStream out(&file);
    doc.save(out, 4);
    file.close();
    return true;
}

QStringList XmlProcessor::collectItemsFromFiles(const QStringList &filePaths, bool repair, QTextEdit *log, QProgressBar *progress, QMutex *mutex)
{
    QStringList items;
    QMetaObject::invokeMethod(progress, "setMaximum", Qt::QueuedConnection, Q_ARG(int, filePaths.size()));
    int value = 0;
    for (const QString &filePath : filePaths) {
        QString errorMsg;
        bool valid = checkXmlFormat(filePath, errorMsg);
        if (!valid) {
            if (repair) {
                if (repairXml(filePath, errorMsg)) {
                    QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("已修复: %1").arg(filePath)));
                    valid = checkXmlFormat(filePath, errorMsg);
                } else {
                    QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("修复失败: %1 - %2").arg(filePath, errorMsg)));
                    continue;
                }
            } else {
                QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("忽略无效文件: %1 - %2").arg(filePath, errorMsg)));
                continue;
            }
        }
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("无法打开文件: %1").arg(filePath)));
            continue;
        }
        QDomDocument doc;
        doc.setContent(&file);
        QDomNodeList itemNodes = doc.documentElement().elementsByTagName("item");
        for (int i = 0; i < itemNodes.count(); ++i) {
            QDomElement item = itemNodes.at(i).toElement();
            QDomDocument tempDoc;
            QDomNode imported = tempDoc.importNode(item, true);
            tempDoc.appendChild(imported);
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            QTextStream out(&buffer);
            tempDoc.save(out, 4);
            items.append(QString(buffer.data()));
            buffer.close();
        }
        file.close();
        QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("已合并来自: %1").arg(filePath)));
        QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection, Q_ARG(int, ++value));
    }
    return items;
}

void XmlProcessor::mergeFiles(const QStringList &filePaths, const QString &outputPath, bool repair, QTextEdit *log, QProgressBar *progress)
{
    QMutex mutex;
    QStringList items = collectItemsFromFiles(filePaths, repair, log, progress, &mutex);

    QDomDocument mergedDoc;
    QDomElement fps = mergedDoc.createElement("fps");
    fps.setAttribute("version", "1.2");
    fps.setAttribute("url", "");
    mergedDoc.appendChild(fps);

    QDomElement generator = mergedDoc.createElement("generator");
    generator.setAttribute("name", "wzw");
    generator.setAttribute("url", "");
    fps.appendChild(generator);

    for (const QString &itemStr : items) {
        QDomDocument tempDoc;
        if (!tempDoc.setContent(itemStr)) {
            QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, "无效的项内容，无法合并"));
            continue;
        }
        QDomNodeList tempItems = tempDoc.elementsByTagName("item");
        for (int i = 0; i < tempItems.count(); ++i) {
            fps.appendChild(mergedDoc.importNode(tempItems.at(i), true));
        }
    }

    QFile outputFile(outputPath);
    if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&outputFile);
        mergedDoc.save(out, 4);
        QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("合并文件已保存至: %1").arg(outputPath)));
        outputFile.close();
    } else {
        QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("无法保存合并文件: %1").arg(outputPath)));
    }
}

void XmlProcessor::splitFile(const QString &filePath, const QString &outputDir, QTextEdit *log, QProgressBar *progress)
{
    QString errorMsg;
    if (!checkXmlFormat(filePath, errorMsg)) {
        QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("无法分割无效文件: %1 - %2").arg(filePath, errorMsg)));
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("无法打开文件: %1").arg(filePath)));
        return;
    }
    QDomDocument doc;
    doc.setContent(&file);
    QDomNodeList items = doc.documentElement().elementsByTagName("item");
    file.close();
    QMetaObject::invokeMethod(progress, "setMaximum", Qt::QueuedConnection, Q_ARG(int, items.count()));
    int value = 0;
    for (int i = 0; i < items.count(); ++i) {
        QDomDocument splitDoc;
        QDomElement fps = splitDoc.createElement("fps");
        fps.setAttribute("version", "1.2");
        fps.setAttribute("url", "");
        splitDoc.appendChild(fps);

        QDomElement generator = splitDoc.createElement("generator");
        generator.setAttribute("name", "wzw");
        generator.setAttribute("url", "");
        fps.appendChild(generator);

        fps.appendChild(splitDoc.importNode(items.at(i), true));

        QString outputPath = outputDir + "/split_" + QString::number(i + 1) + ".xml";
        QFile outputFile(outputPath);
        if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&outputFile);
            splitDoc.save(out, 4);
            QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("分割文件已保存至: %1").arg(outputPath)));
            outputFile.close();
        } else {
            QMetaObject::invokeMethod(log, "append", Qt::QueuedConnection, Q_ARG(QString, QString("无法保存分割文件: %1").arg(outputPath)));
        }
        value++;
        QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection, Q_ARG(int, value));
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("XML合并与分割工具");
    setMinimumSize(800, 600);

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    QHBoxLayout *inputLayout = new QHBoxLayout();
    QPushButton *selectFilesBtn = new QPushButton("选择XML文件", this);
    QPushButton *selectFolderBtn = new QPushButton("选择文件夹", this);
    QPushButton *selectSplitFileBtn = new QPushButton("选择要分割的文件", this);
    inputLayout->addWidget(selectFilesBtn);
    inputLayout->addWidget(selectFolderBtn);
    inputLayout->addWidget(selectSplitFileBtn);
    mainLayout->addLayout(inputLayout);

    QHBoxLayout *outputLayout = new QHBoxLayout();
    QLabel *outputLabel = new QLabel("输出文件夹:", this);
    outputFolderEdit = new QLineEdit(this);
    QPushButton *selectOutputBtn = new QPushButton("浏览", this);
    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(outputFolderEdit);
    outputLayout->addWidget(selectOutputBtn);
    mainLayout->addLayout(outputLayout);

    QHBoxLayout *mergeNameLayout = new QHBoxLayout();
    QLabel *mergeNameLabel = new QLabel("合并文件名:", this);
    mergeNameEdit = new QLineEdit("merged.xml", this);
    mergeNameLayout->addWidget(mergeNameLabel);
    mergeNameLayout->addWidget(mergeNameEdit);
    mainLayout->addLayout(mergeNameLayout);

    repairCheck = new QCheckBox("修复无效文件（添加缺失标签）", this);
    mainLayout->addWidget(repairCheck);

    QHBoxLayout *actionLayout = new QHBoxLayout();
    QPushButton *mergeBtn = new QPushButton("合并", this);
    QPushButton *splitBtn = new QPushButton("分割", this);
    actionLayout->addWidget(mergeBtn);
    actionLayout->addWidget(splitBtn);
    mainLayout->addLayout(actionLayout);

    logText = new QTextEdit(this);
    logText->setReadOnly(true);
    mainLayout->addWidget(logText);

    progressBar = new QProgressBar(this);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);

    connect(selectFilesBtn, &QPushButton::clicked, this, &MainWindow::selectFiles);
    connect(selectFolderBtn, &QPushButton::clicked, this, &MainWindow::selectFolder);
    connect(selectSplitFileBtn, &QPushButton::clicked, this, &MainWindow::selectSplitFile);
    connect(selectOutputBtn, &QPushButton::clicked, this, &MainWindow::selectOutputFolder);
    connect(mergeBtn, &QPushButton::clicked, this, &MainWindow::performMerge);
    connect(splitBtn, &QPushButton::clicked, this, &MainWindow::performSplit);

    processor = new XmlProcessor();
}

void MainWindow::selectFiles()
{
    inputFiles = QFileDialog::getOpenFileNames(this, "选择XML文件", "", "XML文件 (*.xml)");
    if (!inputFiles.isEmpty()) {
        logText->append(QString("已选择文件: %1").arg(inputFiles.size()));
        inputFolder = QFileInfo(inputFiles.first()).absolutePath();
    }
}

void MainWindow::selectFolder()
{
    inputFolder = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!inputFolder.isEmpty()) {
        QDir dir(inputFolder);
        inputFiles = dir.entryList(QStringList() << "*.xml", QDir::Files);
        for (QString &file : inputFiles) {
            file = inputFolder + "/" + file;
        }
        logText->append(QString("已选择文件夹: %1 包含 %2 个XML文件").arg(inputFolder).arg(inputFiles.size()));
    }
}

void MainWindow::selectSplitFile()
{
    splitFile = QFileDialog::getOpenFileName(this, "选择要分割的XML文件", "", "XML文件 (*.xml)");
    if (!splitFile.isEmpty()) {
        logText->append(QString("已选择要分割的文件: %1").arg(splitFile));
        inputFolder = QFileInfo(splitFile).absolutePath();
    }
}

void MainWindow::selectOutputFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择输出文件夹");
    if (!dir.isEmpty()) {
        outputFolderEdit->setText(dir);
    }
}

void MainWindow::performMerge()
{
    if (inputFiles.isEmpty()) {
        QMessageBox::warning(this, "错误", "未选择要合并的文件。");
        return;
    }
    QString outputDir = outputFolderEdit->text().isEmpty() ? inputFolder : outputFolderEdit->text();
    QString outputPath = outputDir + "/" + mergeNameEdit->text();
    bool repair = repairCheck->isChecked();
    progressBar->setValue(0);
    logText->append("开始合并...");

    QThreadPool::globalInstance()->start([this, outputPath, repair]() {
        processor->mergeFiles(inputFiles, outputPath, repair, logText, progressBar);
        QMetaObject::invokeMethod(logText, "append", Qt::QueuedConnection, Q_ARG(QString, "合并完成。"));
    });
}

void MainWindow::performSplit()
{
    if (splitFile.isEmpty()) {
        QMessageBox::warning(this, "错误", "未选择要分割的文件。");
        return;
    }
    QString outputDir = outputFolderEdit->text().isEmpty() ? inputFolder : outputFolderEdit->text();
    progressBar->setValue(0);
    logText->append("开始分割...");

    QThreadPool::globalInstance()->start([this, outputDir]() {
        processor->splitFile(splitFile, outputDir, logText, progressBar);
        QMetaObject::invokeMethod(logText, "append", Qt::QueuedConnection, Q_ARG(QString, "分割完成。"));
    });
}
