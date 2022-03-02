#include "mainwindow.h"
#include <cstdio>

#include <QApplication>
#include <QDesktopWidget>
#include <QFileSystemModel>
#include <QFileIconProvider>
#include <QTreeView>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "fatxfilesystemmodel.h"

struct {
    const char *letter;
    uint64_t offset;
    uint64_t size;
    fatx_fs fs;
} partition_map[] = {
    { "C", 0x8ca80000, 0x01f400000, {} },
    { "E", 0xabe80000, 0x131f00000, {} },
    { "X", 0x00080000, 0x02ee00000, {} },
    { "Y", 0x2ee80000, 0x02ee00000, {} },
    { "Z", 0x5dc80000, 0x02ee00000, {} },
};

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationVersion(QT_VERSION_STR);
    QCommandLineParser parser;
    parser.setApplicationDescription("gfatx");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dontUseCustomDirectoryIconsOption("c", "Set QFileIconProvider::DontUseCustomDirectoryIcons");
    parser.addOption(dontUseCustomDirectoryIconsOption);
    parser.addPositionalArgument("disk", "The disk image to open.");
    parser.process(app);

    if (parser.positionalArguments().isEmpty()) {
        fprintf(stderr, "Specify path to disk\n");
        exit(1);
    }

    const QString diskPath = parser.positionalArguments().first();
    QByteArray ba = diskPath.toLocal8Bit();
    const char *disk_path = ba.data();

    FatxFileSystemModel model;
    for (int i = 0; i < ARRAY_SIZE(partition_map); i++) {
        int s;

        fatx_log_init(&partition_map[i].fs, stderr, 1);
        s = fatx_open_device(&partition_map[i].fs, disk_path,
                             partition_map[i].offset, partition_map[i].size,
                             512, FATX_READ_FROM_SUPERBLOCK);
        if (s != FATX_STATUS_SUCCESS) {
            fprintf(stderr, "Failed to open disk\n");
            continue;
        }
        model.addPartition(std::string(partition_map[i].letter), &partition_map[i].fs);
    }

    QTreeView tree;
    tree.setModel(&model);
    tree.setAnimated(false);
    tree.setIndentation(20);
    tree.setSortingEnabled(true);
    const QSize availableSize = QApplication::desktop()->availableGeometry(&tree).size();
    tree.resize(availableSize / 2);
    tree.setColumnWidth(0, tree.width() / 3);

    tree.setWindowTitle(QObject::tr("gfatx"));
    tree.show();

    return app.exec();
}
